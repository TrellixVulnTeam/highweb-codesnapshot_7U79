// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/v8.h"

#if V8_TARGET_ARCH_X87

#include "src/code-factory.h"
#include "src/codegen.h"
#include "src/deoptimizer.h"
#include "src/full-codegen/full-codegen.h"

namespace v8 {
namespace internal {


#define __ ACCESS_MASM(masm)


void Builtins::Generate_Adaptor(MacroAssembler* masm,
                                CFunctionId id,
                                BuiltinExtraArguments extra_args) {
  // ----------- S t a t e -------------
  //  -- eax                : number of arguments excluding receiver
  //  -- edi                : called function (only guaranteed when
  //                          extra_args requires it)
  //  -- esi                : context
  //  -- esp[0]             : return address
  //  -- esp[4]             : last argument
  //  -- ...
  //  -- esp[4 * argc]      : first argument (argc == eax)
  //  -- esp[4 * (argc +1)] : receiver
  // -----------------------------------

  // Insert extra arguments.
  int num_extra_args = 0;
  if (extra_args == NEEDS_CALLED_FUNCTION) {
    num_extra_args = 1;
    Register scratch = ebx;
    __ pop(scratch);  // Save return address.
    __ push(edi);
    __ push(scratch);  // Restore return address.
  } else {
    DCHECK(extra_args == NO_EXTRA_ARGUMENTS);
  }

  // JumpToExternalReference expects eax to contain the number of arguments
  // including the receiver and the extra arguments.
  __ add(eax, Immediate(num_extra_args + 1));
  __ JumpToExternalReference(ExternalReference(id, masm->isolate()));
}


static void CallRuntimePassFunction(
    MacroAssembler* masm, Runtime::FunctionId function_id) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  // Push a copy of the function.
  __ push(edi);
  // Function is also the parameter to the runtime call.
  __ push(edi);

  __ CallRuntime(function_id, 1);
  // Restore receiver.
  __ pop(edi);
}


static void GenerateTailCallToSharedCode(MacroAssembler* masm) {
  __ mov(eax, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ mov(eax, FieldOperand(eax, SharedFunctionInfo::kCodeOffset));
  __ lea(eax, FieldOperand(eax, Code::kHeaderSize));
  __ jmp(eax);
}


static void GenerateTailCallToReturnedCode(MacroAssembler* masm) {
  __ lea(eax, FieldOperand(eax, Code::kHeaderSize));
  __ jmp(eax);
}


void Builtins::Generate_InOptimizationQueue(MacroAssembler* masm) {
  // Checking whether the queued function is ready for install is optional,
  // since we come across interrupts and stack checks elsewhere.  However,
  // not checking may delay installing ready functions, and always checking
  // would be quite expensive.  A good compromise is to first check against
  // stack limit as a cue for an interrupt signal.
  Label ok;
  ExternalReference stack_limit =
      ExternalReference::address_of_stack_limit(masm->isolate());
  __ cmp(esp, Operand::StaticVariable(stack_limit));
  __ j(above_equal, &ok, Label::kNear);

  CallRuntimePassFunction(masm, Runtime::kTryInstallOptimizedCode);
  GenerateTailCallToReturnedCode(masm);

  __ bind(&ok);
  GenerateTailCallToSharedCode(masm);
}


static void Generate_JSConstructStubHelper(MacroAssembler* masm,
                                           bool is_api_function,
                                           bool create_memento) {
  // ----------- S t a t e -------------
  //  -- eax: number of arguments
  //  -- edi: constructor function
  //  -- ebx: allocation site or undefined
  //  -- edx: original constructor
  // -----------------------------------

  // Should never create mementos for api functions.
  DCHECK(!is_api_function || !create_memento);

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);

    // Preserve the incoming parameters on the stack.
    __ AssertUndefinedOrAllocationSite(ebx);
    __ push(ebx);
    __ SmiTag(eax);
    __ push(eax);
    __ push(edi);
    __ push(edx);

    // Try to allocate the object without transitioning into C code. If any of
    // the preconditions is not met, the code bails out to the runtime call.
    Label rt_call, allocated;
    if (FLAG_inline_new) {
      ExternalReference debug_step_in_fp =
          ExternalReference::debug_step_in_fp_address(masm->isolate());
      __ cmp(Operand::StaticVariable(debug_step_in_fp), Immediate(0));
      __ j(not_equal, &rt_call);

      // Fall back to runtime if the original constructor and function differ.
      __ cmp(edx, edi);
      __ j(not_equal, &rt_call);

      // Verified that the constructor is a JSFunction.
      // Load the initial map and verify that it is in fact a map.
      // edi: constructor
      __ mov(eax, FieldOperand(edi, JSFunction::kPrototypeOrInitialMapOffset));
      // Will both indicate a NULL and a Smi
      __ JumpIfSmi(eax, &rt_call);
      // edi: constructor
      // eax: initial map (if proven valid below)
      __ CmpObjectType(eax, MAP_TYPE, ebx);
      __ j(not_equal, &rt_call);

      // Check that the constructor is not constructing a JSFunction (see
      // comments in Runtime_NewObject in runtime.cc). In which case the
      // initial map's instance type would be JS_FUNCTION_TYPE.
      // edi: constructor
      // eax: initial map
      __ CmpInstanceType(eax, JS_FUNCTION_TYPE);
      __ j(equal, &rt_call);

      if (!is_api_function) {
        Label allocate;
        // The code below relies on these assumptions.
        STATIC_ASSERT(Map::Counter::kShift + Map::Counter::kSize == 32);
        // Check if slack tracking is enabled.
        __ mov(esi, FieldOperand(eax, Map::kBitField3Offset));
        __ shr(esi, Map::Counter::kShift);
        __ cmp(esi, Map::kSlackTrackingCounterEnd);
        __ j(less, &allocate);
        // Decrease generous allocation count.
        __ sub(FieldOperand(eax, Map::kBitField3Offset),
               Immediate(1 << Map::Counter::kShift));

        __ cmp(esi, Map::kSlackTrackingCounterEnd);
        __ j(not_equal, &allocate);

        __ push(eax);
        __ push(edx);
        __ push(edi);

        __ push(edi);  // constructor
        __ CallRuntime(Runtime::kFinalizeInstanceSize, 1);

        __ pop(edi);
        __ pop(edx);
        __ pop(eax);
        __ mov(esi, Map::kSlackTrackingCounterEnd - 1);

        __ bind(&allocate);
      }

      // Now allocate the JSObject on the heap.
      // edi: constructor
      // eax: initial map
      __ movzx_b(edi, FieldOperand(eax, Map::kInstanceSizeOffset));
      __ shl(edi, kPointerSizeLog2);
      if (create_memento) {
        __ add(edi, Immediate(AllocationMemento::kSize));
      }

      __ Allocate(edi, ebx, edi, no_reg, &rt_call, NO_ALLOCATION_FLAGS);

      Factory* factory = masm->isolate()->factory();

      // Allocated the JSObject, now initialize the fields.
      // eax: initial map
      // ebx: JSObject
      // edi: start of next object (including memento if create_memento)
      __ mov(Operand(ebx, JSObject::kMapOffset), eax);
      __ mov(ecx, factory->empty_fixed_array());
      __ mov(Operand(ebx, JSObject::kPropertiesOffset), ecx);
      __ mov(Operand(ebx, JSObject::kElementsOffset), ecx);
      // Set extra fields in the newly allocated object.
      // eax: initial map
      // ebx: JSObject
      // edi: start of next object (including memento if create_memento)
      // esi: slack tracking counter (non-API function case)
      __ mov(edx, factory->undefined_value());
      __ lea(ecx, Operand(ebx, JSObject::kHeaderSize));
      if (!is_api_function) {
        Label no_inobject_slack_tracking;

        // Check if slack tracking is enabled.
        __ cmp(esi, Map::kSlackTrackingCounterEnd);
        __ j(less, &no_inobject_slack_tracking);

        // Allocate object with a slack.
        __ movzx_b(esi, FieldOperand(eax, Map::kInObjectPropertiesOffset));
        __ movzx_b(eax, FieldOperand(eax, Map::kUnusedPropertyFieldsOffset));
        __ sub(esi, eax);
        __ lea(esi,
               Operand(ebx, esi, times_pointer_size, JSObject::kHeaderSize));
        // esi: offset of first field after pre-allocated fields
        if (FLAG_debug_code) {
          __ cmp(esi, edi);
          __ Assert(less_equal,
                    kUnexpectedNumberOfPreAllocatedPropertyFields);
        }
        __ InitializeFieldsWithFiller(ecx, esi, edx);
        __ mov(edx, factory->one_pointer_filler_map());
        // Fill the remaining fields with one pointer filler map.

        __ bind(&no_inobject_slack_tracking);
      }

      if (create_memento) {
        __ lea(esi, Operand(edi, -AllocationMemento::kSize));
        __ InitializeFieldsWithFiller(ecx, esi, edx);

        // Fill in memento fields if necessary.
        // esi: points to the allocated but uninitialized memento.
        __ mov(Operand(esi, AllocationMemento::kMapOffset),
               factory->allocation_memento_map());
        // Get the cell or undefined.
        __ mov(edx, Operand(esp, 3 * kPointerSize));
        __ AssertUndefinedOrAllocationSite(edx);
        __ mov(Operand(esi, AllocationMemento::kAllocationSiteOffset),
               edx);
      } else {
        __ InitializeFieldsWithFiller(ecx, edi, edx);
      }

      // Add the object tag to make the JSObject real, so that we can continue
      // and jump into the continuation code at any time from now on.
      // ebx: JSObject (untagged)
      __ or_(ebx, Immediate(kHeapObjectTag));

      // Continue with JSObject being successfully allocated
      // ebx: JSObject (tagged)
      __ jmp(&allocated);
    }

    // Allocate the new receiver object using the runtime call.
    // edx: original constructor
    __ bind(&rt_call);
    int offset = kPointerSize;
    if (create_memento) {
      // Get the cell or allocation site.
      __ mov(edi, Operand(esp, kPointerSize * 3));
      __ push(edi);  // argument 1: allocation site
      offset += kPointerSize;
    }

    // Must restore esi (context) and edi (constructor) before calling
    // runtime.
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));
    __ mov(edi, Operand(esp, offset));
    __ push(edi);  // argument 2/1: constructor function
    __ push(edx);  // argument 3/2: original constructor
    if (create_memento) {
      __ CallRuntime(Runtime::kNewObjectWithAllocationSite, 3);
    } else {
      __ CallRuntime(Runtime::kNewObject, 2);
    }
    __ mov(ebx, eax);  // store result in ebx

    // Runtime_NewObjectWithAllocationSite increments allocation count.
    // Skip the increment.
    Label count_incremented;
    if (create_memento) {
      __ jmp(&count_incremented);
    }

    // New object allocated.
    // ebx: newly allocated object
    __ bind(&allocated);

    if (create_memento) {
      __ mov(ecx, Operand(esp, 3 * kPointerSize));
      __ cmp(ecx, masm->isolate()->factory()->undefined_value());
      __ j(equal, &count_incremented);
      // ecx is an AllocationSite. We are creating a memento from it, so we
      // need to increment the memento create count.
      __ add(FieldOperand(ecx, AllocationSite::kPretenureCreateCountOffset),
             Immediate(Smi::FromInt(1)));
      __ bind(&count_incremented);
    }

    // Restore the parameters.
    __ pop(edx);  // new.target
    __ pop(edi);  // Constructor function.

    // Retrieve smi-tagged arguments count from the stack.
    __ mov(eax, Operand(esp, 0));
    __ SmiUntag(eax);

    // Push new.target onto the construct frame. This is stored just below the
    // receiver on the stack.
    __ push(edx);

    // Push the allocated receiver to the stack. We need two copies
    // because we may have to return the original one and the calling
    // conventions dictate that the called function pops the receiver.
    __ push(ebx);
    __ push(ebx);

    // Set up pointer to last argument.
    __ lea(ebx, Operand(ebp, StandardFrameConstants::kCallerSPOffset));

    // Copy arguments and receiver to the expression stack.
    Label loop, entry;
    __ mov(ecx, eax);
    __ jmp(&entry);
    __ bind(&loop);
    __ push(Operand(ebx, ecx, times_4, 0));
    __ bind(&entry);
    __ dec(ecx);
    __ j(greater_equal, &loop);

    // Call the function.
    if (is_api_function) {
      __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));
      Handle<Code> code =
          masm->isolate()->builtins()->HandleApiCallConstruct();
      __ call(code, RelocInfo::CODE_TARGET);
    } else {
      ParameterCount actual(eax);
      __ InvokeFunction(edi, actual, CALL_FUNCTION,
                        NullCallWrapper());
    }

    // Store offset of return address for deoptimizer.
    if (!is_api_function) {
      masm->isolate()->heap()->SetConstructStubDeoptPCOffset(masm->pc_offset());
    }

    // Restore context from the frame.
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

    // If the result is an object (in the ECMA sense), we should get rid
    // of the receiver and use the result; see ECMA-262 section 13.2.2-7
    // on page 74.
    Label use_receiver, exit;

    // If the result is a smi, it is *not* an object in the ECMA sense.
    __ JumpIfSmi(eax, &use_receiver);

    // If the type of the result (stored in its map) is less than
    // FIRST_SPEC_OBJECT_TYPE, it is not an object in the ECMA sense.
    __ CmpObjectType(eax, FIRST_SPEC_OBJECT_TYPE, ecx);
    __ j(above_equal, &exit);

    // Throw away the result of the constructor invocation and use the
    // on-stack receiver as the result.
    __ bind(&use_receiver);
    __ mov(eax, Operand(esp, 0));

    // Restore the arguments count and leave the construct frame. The arguments
    // count is stored below the reciever and the new.target.
    __ bind(&exit);
    __ mov(ebx, Operand(esp, 2 * kPointerSize));

    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  STATIC_ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ pop(ecx);
  __ lea(esp, Operand(esp, ebx, times_2, 1 * kPointerSize));  // 1 ~ receiver
  __ push(ecx);
  __ IncrementCounter(masm->isolate()->counters()->constructed_objects(), 1);
  __ ret(0);
}


void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  Generate_JSConstructStubHelper(masm, false, FLAG_pretenuring_call_new);
}


void Builtins::Generate_JSConstructStubApi(MacroAssembler* masm) {
  Generate_JSConstructStubHelper(masm, true, false);
}


void Builtins::Generate_JSConstructStubForDerived(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax: number of arguments
  //  -- edi: constructor function
  //  -- ebx: allocation site or undefined
  //  -- edx: original constructor
  // -----------------------------------

  {
    FrameScope frame_scope(masm, StackFrame::CONSTRUCT);

    // Preserve allocation site.
    __ AssertUndefinedOrAllocationSite(ebx);
    __ push(ebx);

    // Preserve actual arguments count.
    __ SmiTag(eax);
    __ push(eax);
    __ SmiUntag(eax);

    // Push new.target.
    __ push(edx);

    // receiver is the hole.
    __ push(Immediate(masm->isolate()->factory()->the_hole_value()));

    // Set up pointer to last argument.
    __ lea(ebx, Operand(ebp, StandardFrameConstants::kCallerSPOffset));

    // Copy arguments and receiver to the expression stack.
    Label loop, entry;
    __ mov(ecx, eax);
    __ jmp(&entry);
    __ bind(&loop);
    __ push(Operand(ebx, ecx, times_4, 0));
    __ bind(&entry);
    __ dec(ecx);
    __ j(greater_equal, &loop);

    // Handle step in.
    Label skip_step_in;
    ExternalReference debug_step_in_fp =
        ExternalReference::debug_step_in_fp_address(masm->isolate());
    __ cmp(Operand::StaticVariable(debug_step_in_fp), Immediate(0));
    __ j(equal, &skip_step_in);

    __ push(eax);
    __ push(edi);
    __ push(edi);
    __ CallRuntime(Runtime::kHandleStepInForDerivedConstructors, 1);
    __ pop(edi);
    __ pop(eax);

    __ bind(&skip_step_in);

    // Invoke function.
    ParameterCount actual(eax);
    __ InvokeFunction(edi, actual, CALL_FUNCTION, NullCallWrapper());

    // Restore context from the frame.
    __ mov(esi, Operand(ebp, StandardFrameConstants::kContextOffset));

    // Get arguments count, skipping over new.target.
    __ mov(ebx, Operand(esp, kPointerSize));
  }

  __ pop(ecx);  // Return address.
  __ lea(esp, Operand(esp, ebx, times_2, 1 * kPointerSize));
  __ push(ecx);
  __ ret(0);
}


enum IsTagged { kEaxIsSmiTagged, kEaxIsUntaggedInt };


// Clobbers ecx, edx, edi; preserves all other registers.
static void Generate_CheckStackOverflow(MacroAssembler* masm,
                                        const int calleeOffset,
                                        IsTagged eax_is_tagged) {
  // eax   : the number of items to be pushed to the stack
  //
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  Label okay;
  ExternalReference real_stack_limit =
      ExternalReference::address_of_real_stack_limit(masm->isolate());
  __ mov(edi, Operand::StaticVariable(real_stack_limit));
  // Make ecx the space we have left. The stack might already be overflowed
  // here which will cause ecx to become negative.
  __ mov(ecx, esp);
  __ sub(ecx, edi);
  // Make edx the space we need for the array when it is unrolled onto the
  // stack.
  __ mov(edx, eax);
  int smi_tag = eax_is_tagged == kEaxIsSmiTagged ? kSmiTagSize : 0;
  __ shl(edx, kPointerSizeLog2 - smi_tag);
  // Check if the arguments will overflow the stack.
  __ cmp(ecx, edx);
  __ j(greater, &okay);  // Signed comparison.

  // Out of stack space.
  __ push(Operand(ebp, calleeOffset));  // push this
  if (eax_is_tagged == kEaxIsUntaggedInt) {
    __ SmiTag(eax);
  }
  __ push(eax);
  __ InvokeBuiltin(Builtins::STACK_OVERFLOW, CALL_FUNCTION);

  __ bind(&okay);
}


static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  ProfileEntryHookStub::MaybeCallEntryHook(masm);

  // Clear the context before we push it when entering the internal frame.
  __ Move(esi, Immediate(0));

  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Load the previous frame pointer (ebx) to access C arguments
    __ mov(ebx, Operand(ebp, 0));

    // Get the function from the frame and setup the context.
    __ mov(ecx, Operand(ebx, EntryFrameConstants::kFunctionArgOffset));
    __ mov(esi, FieldOperand(ecx, JSFunction::kContextOffset));

    // Push the function and the receiver onto the stack.
    __ push(ecx);
    __ push(Operand(ebx, EntryFrameConstants::kReceiverArgOffset));

    // Load the number of arguments and setup pointer to the arguments.
    __ mov(eax, Operand(ebx, EntryFrameConstants::kArgcOffset));
    __ mov(ebx, Operand(ebx, EntryFrameConstants::kArgvOffset));

    // Check if we have enough stack space to push all arguments.
    // The function is the first thing that was pushed above after entering
    // the internal frame.
    const int kFunctionOffset =
        InternalFrameConstants::kCodeOffset - kPointerSize;
    // Expects argument count in eax. Clobbers ecx, edx, edi.
    Generate_CheckStackOverflow(masm, kFunctionOffset, kEaxIsUntaggedInt);

    // Copy arguments to the stack in a loop.
    Label loop, entry;
    __ Move(ecx, Immediate(0));
    __ jmp(&entry);
    __ bind(&loop);
    __ mov(edx, Operand(ebx, ecx, times_4, 0));  // push parameter from argv
    __ push(Operand(edx, 0));  // dereference handle
    __ inc(ecx);
    __ bind(&entry);
    __ cmp(ecx, eax);
    __ j(not_equal, &loop);

    // Get the function from the stack and call it.
    // kPointerSize for the receiver.
    __ mov(edi, Operand(esp, eax, times_4, kPointerSize));

    // Invoke the code.
    if (is_construct) {
      // No type feedback cell is available
      __ mov(ebx, masm->isolate()->factory()->undefined_value());
      CallConstructStub stub(masm->isolate(), NO_CALL_CONSTRUCTOR_FLAGS);
      __ CallStub(&stub);
    } else {
      ParameterCount actual(eax);
      __ InvokeFunction(edi, actual, CALL_FUNCTION,
                        NullCallWrapper());
    }

    // Exit the internal frame. Notice that this also removes the empty.
    // context and the function left on the stack by the code
    // invocation.
  }
  __ ret(kPointerSize);  // Remove receiver.
}


void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}


void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}


void Builtins::Generate_CompileLazy(MacroAssembler* masm) {
  CallRuntimePassFunction(masm, Runtime::kCompileLazy);
  GenerateTailCallToReturnedCode(masm);
}



static void CallCompileOptimized(MacroAssembler* masm, bool concurrent) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  // Push a copy of the function.
  __ push(edi);
  // Function is also the parameter to the runtime call.
  __ push(edi);
  // Whether to compile in a background thread.
  __ Push(masm->isolate()->factory()->ToBoolean(concurrent));

  __ CallRuntime(Runtime::kCompileOptimized, 2);
  // Restore receiver.
  __ pop(edi);
}


void Builtins::Generate_CompileOptimized(MacroAssembler* masm) {
  CallCompileOptimized(masm, false);
  GenerateTailCallToReturnedCode(masm);
}


void Builtins::Generate_CompileOptimizedConcurrent(MacroAssembler* masm) {
  CallCompileOptimized(masm, true);
  GenerateTailCallToReturnedCode(masm);
}


static void GenerateMakeCodeYoungAgainCommon(MacroAssembler* masm) {
  // For now, we are relying on the fact that make_code_young doesn't do any
  // garbage collection which allows us to save/restore the registers without
  // worrying about which of them contain pointers. We also don't build an
  // internal frame to make the code faster, since we shouldn't have to do stack
  // crawls in MakeCodeYoung. This seems a bit fragile.

  // Re-execute the code that was patched back to the young age when
  // the stub returns.
  __ sub(Operand(esp, 0), Immediate(5));
  __ pushad();
  __ mov(eax, Operand(esp, 8 * kPointerSize));
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(2, ebx);
    __ mov(Operand(esp, 1 * kPointerSize),
           Immediate(ExternalReference::isolate_address(masm->isolate())));
    __ mov(Operand(esp, 0), eax);
    __ CallCFunction(
        ExternalReference::get_make_code_young_function(masm->isolate()), 2);
  }
  __ popad();
  __ ret(0);
}

#define DEFINE_CODE_AGE_BUILTIN_GENERATOR(C)                 \
void Builtins::Generate_Make##C##CodeYoungAgainEvenMarking(  \
    MacroAssembler* masm) {                                  \
  GenerateMakeCodeYoungAgainCommon(masm);                    \
}                                                            \
void Builtins::Generate_Make##C##CodeYoungAgainOddMarking(   \
    MacroAssembler* masm) {                                  \
  GenerateMakeCodeYoungAgainCommon(masm);                    \
}
CODE_AGE_LIST(DEFINE_CODE_AGE_BUILTIN_GENERATOR)
#undef DEFINE_CODE_AGE_BUILTIN_GENERATOR


void Builtins::Generate_MarkCodeAsExecutedOnce(MacroAssembler* masm) {
  // For now, as in GenerateMakeCodeYoungAgainCommon, we are relying on the fact
  // that make_code_young doesn't do any garbage collection which allows us to
  // save/restore the registers without worrying about which of them contain
  // pointers.
  __ pushad();
  __ mov(eax, Operand(esp, 8 * kPointerSize));
  __ sub(eax, Immediate(Assembler::kCallInstructionLength));
  {  // NOLINT
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(2, ebx);
    __ mov(Operand(esp, 1 * kPointerSize),
           Immediate(ExternalReference::isolate_address(masm->isolate())));
    __ mov(Operand(esp, 0), eax);
    __ CallCFunction(
        ExternalReference::get_mark_code_as_executed_function(masm->isolate()),
        2);
  }
  __ popad();

  // Perform prologue operations usually performed by the young code stub.
  __ pop(eax);   // Pop return address into scratch register.
  __ push(ebp);  // Caller's frame pointer.
  __ mov(ebp, esp);
  __ push(esi);  // Callee's context.
  __ push(edi);  // Callee's JS Function.
  __ push(eax);  // Push return address after frame prologue.

  // Jump to point after the code-age stub.
  __ ret(0);
}


void Builtins::Generate_MarkCodeAsExecutedTwice(MacroAssembler* masm) {
  GenerateMakeCodeYoungAgainCommon(masm);
}


void Builtins::Generate_MarkCodeAsToBeExecutedOnce(MacroAssembler* masm) {
  Generate_MarkCodeAsExecutedOnce(masm);
}


static void Generate_NotifyStubFailureHelper(MacroAssembler* masm,
                                             SaveFPRegsMode save_doubles) {
  // Enter an internal frame.
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Preserve registers across notification, this is important for compiled
    // stubs that tail call the runtime on deopts passing their parameters in
    // registers.
    __ pushad();
    __ CallRuntime(Runtime::kNotifyStubFailure, 0, save_doubles);
    __ popad();
    // Tear down internal frame.
  }

  __ pop(MemOperand(esp, 0));  // Ignore state offset
  __ ret(0);  // Return to IC Miss stub, continuation still on stack.
}


void Builtins::Generate_NotifyStubFailure(MacroAssembler* masm) {
  Generate_NotifyStubFailureHelper(masm, kDontSaveFPRegs);
}


void Builtins::Generate_NotifyStubFailureSaveDoubles(MacroAssembler* masm) {
  Generate_NotifyStubFailureHelper(masm, kSaveFPRegs);
}


static void Generate_NotifyDeoptimizedHelper(MacroAssembler* masm,
                                             Deoptimizer::BailoutType type) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Pass deoptimization type to the runtime system.
    __ push(Immediate(Smi::FromInt(static_cast<int>(type))));
    __ CallRuntime(Runtime::kNotifyDeoptimized, 1);

    // Tear down internal frame.
  }

  // Get the full codegen state from the stack and untag it.
  __ mov(ecx, Operand(esp, 1 * kPointerSize));
  __ SmiUntag(ecx);

  // Switch on the state.
  Label not_no_registers, not_tos_eax;
  __ cmp(ecx, FullCodeGenerator::NO_REGISTERS);
  __ j(not_equal, &not_no_registers, Label::kNear);
  __ ret(1 * kPointerSize);  // Remove state.

  __ bind(&not_no_registers);
  __ mov(eax, Operand(esp, 2 * kPointerSize));
  __ cmp(ecx, FullCodeGenerator::TOS_REG);
  __ j(not_equal, &not_tos_eax, Label::kNear);
  __ ret(2 * kPointerSize);  // Remove state, eax.

  __ bind(&not_tos_eax);
  __ Abort(kNoCasesLeft);
}


void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  Generate_NotifyDeoptimizedHelper(masm, Deoptimizer::EAGER);
}


void Builtins::Generate_NotifySoftDeoptimized(MacroAssembler* masm) {
  Generate_NotifyDeoptimizedHelper(masm, Deoptimizer::SOFT);
}


void Builtins::Generate_NotifyLazyDeoptimized(MacroAssembler* masm) {
  Generate_NotifyDeoptimizedHelper(masm, Deoptimizer::LAZY);
}


void Builtins::Generate_FunctionCall(MacroAssembler* masm) {
  Factory* factory = masm->isolate()->factory();

  // 1. Make sure we have at least one argument.
  { Label done;
    __ test(eax, eax);
    __ j(not_zero, &done);
    __ pop(ebx);
    __ push(Immediate(factory->undefined_value()));
    __ push(ebx);
    __ inc(eax);
    __ bind(&done);
  }

  // 2. Get the function to call (passed as receiver) from the stack, check
  //    if it is a function.
  Label slow, non_function;
  // 1 ~ return address.
  __ mov(edi, Operand(esp, eax, times_4, 1 * kPointerSize));
  __ JumpIfSmi(edi, &non_function);
  __ CmpObjectType(edi, JS_FUNCTION_TYPE, ecx);
  __ j(not_equal, &slow);


  // 3a. Patch the first argument if necessary when calling a function.
  Label shift_arguments;
  __ Move(edx, Immediate(0));  // indicate regular JS_FUNCTION
  { Label convert_to_object, use_global_proxy, patch_receiver;
    // Change context eagerly in case we need the global receiver.
    __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));

    // Do not transform the receiver for strict mode functions.
    __ mov(ebx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ test_b(FieldOperand(ebx, SharedFunctionInfo::kStrictModeByteOffset),
              1 << SharedFunctionInfo::kStrictModeBitWithinByte);
    __ j(not_equal, &shift_arguments);

    // Do not transform the receiver for natives (shared already in ebx).
    __ test_b(FieldOperand(ebx, SharedFunctionInfo::kNativeByteOffset),
              1 << SharedFunctionInfo::kNativeBitWithinByte);
    __ j(not_equal, &shift_arguments);

    // Compute the receiver in sloppy mode.
    __ mov(ebx, Operand(esp, eax, times_4, 0));  // First argument.

    // Call ToObject on the receiver if it is not an object, or use the
    // global object if it is null or undefined.
    __ JumpIfSmi(ebx, &convert_to_object);
    __ cmp(ebx, factory->null_value());
    __ j(equal, &use_global_proxy);
    __ cmp(ebx, factory->undefined_value());
    __ j(equal, &use_global_proxy);
    STATIC_ASSERT(LAST_SPEC_OBJECT_TYPE == LAST_TYPE);
    __ CmpObjectType(ebx, FIRST_SPEC_OBJECT_TYPE, ecx);
    __ j(above_equal, &shift_arguments);

    __ bind(&convert_to_object);

    { // In order to preserve argument count.
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ SmiTag(eax);
      __ push(eax);

      __ push(ebx);
      __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
      __ mov(ebx, eax);
      __ Move(edx, Immediate(0));  // restore

      __ pop(eax);
      __ SmiUntag(eax);
    }

    // Restore the function to edi.
    __ mov(edi, Operand(esp, eax, times_4, 1 * kPointerSize));
    __ jmp(&patch_receiver);

    __ bind(&use_global_proxy);
    __ mov(ebx,
           Operand(esi, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
    __ mov(ebx, FieldOperand(ebx, GlobalObject::kGlobalProxyOffset));

    __ bind(&patch_receiver);
    __ mov(Operand(esp, eax, times_4, 0), ebx);

    __ jmp(&shift_arguments);
  }

  // 3b. Check for function proxy.
  __ bind(&slow);
  __ Move(edx, Immediate(1));  // indicate function proxy
  __ CmpInstanceType(ecx, JS_FUNCTION_PROXY_TYPE);
  __ j(equal, &shift_arguments);
  __ bind(&non_function);
  __ Move(edx, Immediate(2));  // indicate non-function

  // 3c. Patch the first argument when calling a non-function.  The
  //     CALL_NON_FUNCTION builtin expects the non-function callee as
  //     receiver, so overwrite the first argument which will ultimately
  //     become the receiver.
  __ mov(Operand(esp, eax, times_4, 0), edi);

  // 4. Shift arguments and return address one slot down on the stack
  //    (overwriting the original receiver).  Adjust argument count to make
  //    the original first argument the new receiver.
  __ bind(&shift_arguments);
  { Label loop;
    __ mov(ecx, eax);
    __ bind(&loop);
    __ mov(ebx, Operand(esp, ecx, times_4, 0));
    __ mov(Operand(esp, ecx, times_4, kPointerSize), ebx);
    __ dec(ecx);
    __ j(not_sign, &loop);  // While non-negative (to copy return address).
    __ pop(ebx);  // Discard copy of return address.
    __ dec(eax);  // One fewer argument (first argument is new receiver).
  }

  // 5a. Call non-function via tail call to CALL_NON_FUNCTION builtin,
  //     or a function proxy via CALL_FUNCTION_PROXY.
  { Label function, non_proxy;
    __ test(edx, edx);
    __ j(zero, &function);
    __ Move(ebx, Immediate(0));
    __ cmp(edx, Immediate(1));
    __ j(not_equal, &non_proxy);

    __ pop(edx);   // return address
    __ push(edi);  // re-add proxy object as additional argument
    __ push(edx);
    __ inc(eax);
    __ GetBuiltinEntry(edx, Builtins::CALL_FUNCTION_PROXY);
    __ jmp(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
           RelocInfo::CODE_TARGET);

    __ bind(&non_proxy);
    __ GetBuiltinEntry(edx, Builtins::CALL_NON_FUNCTION);
    __ jmp(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
           RelocInfo::CODE_TARGET);
    __ bind(&function);
  }

  // 5b. Get the code to call from the function and check that the number of
  //     expected arguments matches what we're providing.  If so, jump
  //     (tail-call) to the code in register edx without checking arguments.
  __ mov(edx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ mov(ebx,
         FieldOperand(edx, SharedFunctionInfo::kFormalParameterCountOffset));
  __ mov(edx, FieldOperand(edi, JSFunction::kCodeEntryOffset));
  __ SmiUntag(ebx);
  __ cmp(eax, ebx);
  __ j(not_equal,
       masm->isolate()->builtins()->ArgumentsAdaptorTrampoline());

  ParameterCount expected(0);
  __ InvokeCode(edx, expected, expected, JUMP_FUNCTION, NullCallWrapper());
}


static void Generate_PushAppliedArguments(MacroAssembler* masm,
                                          const int argumentsOffset,
                                          const int indexOffset,
                                          const int limitOffset) {
  // Copy all arguments from the array to the stack.
  Label entry, loop;
  Register receiver = LoadDescriptor::ReceiverRegister();
  Register key = LoadDescriptor::NameRegister();
  Register slot = LoadDescriptor::SlotRegister();
  Register vector = LoadWithVectorDescriptor::VectorRegister();
  __ mov(key, Operand(ebp, indexOffset));
  __ jmp(&entry);
  __ bind(&loop);
  __ mov(receiver, Operand(ebp, argumentsOffset));  // load arguments

  // Use inline caching to speed up access to arguments.
  FeedbackVectorSpec spec(0, Code::KEYED_LOAD_IC);
  Handle<TypeFeedbackVector> feedback_vector =
      masm->isolate()->factory()->NewTypeFeedbackVector(&spec);
  int index = feedback_vector->GetIndex(FeedbackVectorICSlot(0));
  __ mov(slot, Immediate(Smi::FromInt(index)));
  __ mov(vector, Immediate(feedback_vector));
  Handle<Code> ic =
      KeyedLoadICStub(masm->isolate(), LoadICState(kNoExtraICState)).GetCode();
  __ call(ic, RelocInfo::CODE_TARGET);
  // It is important that we do not have a test instruction after the
  // call.  A test instruction after the call is used to indicate that
  // we have generated an inline version of the keyed load.  In this
  // case, we know that we are not generating a test instruction next.

  // Push the nth argument.
  __ push(eax);

  // Update the index on the stack and in register key.
  __ mov(key, Operand(ebp, indexOffset));
  __ add(key, Immediate(1 << kSmiTagSize));
  __ mov(Operand(ebp, indexOffset), key);

  __ bind(&entry);
  __ cmp(key, Operand(ebp, limitOffset));
  __ j(not_equal, &loop);

  // On exit, the pushed arguments count is in eax, untagged
  __ Move(eax, key);
  __ SmiUntag(eax);
}


// Used by FunctionApply and ReflectApply
static void Generate_ApplyHelper(MacroAssembler* masm, bool targetIsArgument) {
  const int kFormalParameters = targetIsArgument ? 3 : 2;
  const int kStackSize = kFormalParameters + 1;

  // Stack at entry:
  // esp     : return address
  // esp[4]  : arguments
  // esp[8]  : receiver ("this")
  // esp[12] : function
  {
    FrameScope frame_scope(masm, StackFrame::INTERNAL);
    // Stack frame:
    // ebp     : Old base pointer
    // ebp[4]  : return address
    // ebp[8]  : function arguments
    // ebp[12] : receiver
    // ebp[16] : function
    static const int kArgumentsOffset = kFPOnStackSize + kPCOnStackSize;
    static const int kReceiverOffset = kArgumentsOffset + kPointerSize;
    static const int kFunctionOffset = kReceiverOffset + kPointerSize;

    __ push(Operand(ebp, kFunctionOffset));  // push this
    __ push(Operand(ebp, kArgumentsOffset));  // push arguments
    if (targetIsArgument) {
      __ InvokeBuiltin(Builtins::REFLECT_APPLY_PREPARE, CALL_FUNCTION);
    } else {
      __ InvokeBuiltin(Builtins::APPLY_PREPARE, CALL_FUNCTION);
    }

    Generate_CheckStackOverflow(masm, kFunctionOffset, kEaxIsSmiTagged);

    // Push current index and limit.
    const int kLimitOffset =
        StandardFrameConstants::kExpressionsOffset - 1 * kPointerSize;
    const int kIndexOffset = kLimitOffset - 1 * kPointerSize;
    __ push(eax);  // limit
    __ push(Immediate(0));  // index

    // Get the receiver.
    __ mov(ebx, Operand(ebp, kReceiverOffset));

    // Check that the function is a JS function (otherwise it must be a proxy).
    Label push_receiver, use_global_proxy;
    __ mov(edi, Operand(ebp, kFunctionOffset));
    __ CmpObjectType(edi, JS_FUNCTION_TYPE, ecx);
    __ j(not_equal, &push_receiver);

    // Change context eagerly to get the right global object if necessary.
    __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));

    // Compute the receiver.
    // Do not transform the receiver for strict mode functions.
    Label call_to_object;
    __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ test_b(FieldOperand(ecx, SharedFunctionInfo::kStrictModeByteOffset),
              1 << SharedFunctionInfo::kStrictModeBitWithinByte);
    __ j(not_equal, &push_receiver);

    Factory* factory = masm->isolate()->factory();

    // Do not transform the receiver for natives (shared already in ecx).
    __ test_b(FieldOperand(ecx, SharedFunctionInfo::kNativeByteOffset),
              1 << SharedFunctionInfo::kNativeBitWithinByte);
    __ j(not_equal, &push_receiver);

    // Compute the receiver in sloppy mode.
    // Call ToObject on the receiver if it is not an object, or use the
    // global object if it is null or undefined.
    __ JumpIfSmi(ebx, &call_to_object);
    __ cmp(ebx, factory->null_value());
    __ j(equal, &use_global_proxy);
    __ cmp(ebx, factory->undefined_value());
    __ j(equal, &use_global_proxy);
    STATIC_ASSERT(LAST_SPEC_OBJECT_TYPE == LAST_TYPE);
    __ CmpObjectType(ebx, FIRST_SPEC_OBJECT_TYPE, ecx);
    __ j(above_equal, &push_receiver);

    __ bind(&call_to_object);
    __ push(ebx);
    __ InvokeBuiltin(Builtins::TO_OBJECT, CALL_FUNCTION);
    __ mov(ebx, eax);
    __ jmp(&push_receiver);

    __ bind(&use_global_proxy);
    __ mov(ebx,
           Operand(esi, Context::SlotOffset(Context::GLOBAL_OBJECT_INDEX)));
    __ mov(ebx, FieldOperand(ebx, GlobalObject::kGlobalProxyOffset));

    // Push the receiver.
    __ bind(&push_receiver);
    __ push(ebx);

    // Loop over the arguments array, pushing each value to the stack
    Generate_PushAppliedArguments(
        masm, kArgumentsOffset, kIndexOffset, kLimitOffset);

    // Call the function.
    Label call_proxy;
    ParameterCount actual(eax);
    __ mov(edi, Operand(ebp, kFunctionOffset));
    __ CmpObjectType(edi, JS_FUNCTION_TYPE, ecx);
    __ j(not_equal, &call_proxy);
    __ InvokeFunction(edi, actual, CALL_FUNCTION, NullCallWrapper());

    frame_scope.GenerateLeaveFrame();
    __ ret(kStackSize * kPointerSize);  // remove this, receiver, and arguments

    // Call the function proxy.
    __ bind(&call_proxy);
    __ push(edi);  // add function proxy as last argument
    __ inc(eax);
    __ Move(ebx, Immediate(0));
    __ GetBuiltinEntry(edx, Builtins::CALL_FUNCTION_PROXY);
    __ call(masm->isolate()->builtins()->ArgumentsAdaptorTrampoline(),
            RelocInfo::CODE_TARGET);

    // Leave internal frame.
  }
  __ ret(kStackSize * kPointerSize);  // remove this, receiver, and arguments
}


// Used by ReflectConstruct
static void Generate_ConstructHelper(MacroAssembler* masm) {
  const int kFormalParameters = 3;
  const int kStackSize = kFormalParameters + 1;

  // Stack at entry:
  // esp     : return address
  // esp[4]  : original constructor (new.target)
  // esp[8]  : arguments
  // esp[16] : constructor
  {
    FrameScope frame_scope(masm, StackFrame::INTERNAL);
    // Stack frame:
    // ebp     : Old base pointer
    // ebp[4]  : return address
    // ebp[8]  : original constructor (new.target)
    // ebp[12] : arguments
    // ebp[16] : constructor
    static const int kNewTargetOffset = kFPOnStackSize + kPCOnStackSize;
    static const int kArgumentsOffset = kNewTargetOffset + kPointerSize;
    static const int kFunctionOffset = kArgumentsOffset + kPointerSize;

    // If newTarget is not supplied, set it to constructor
    Label validate_arguments;
    __ mov(eax, Operand(ebp, kNewTargetOffset));
    __ CompareRoot(eax, Heap::kUndefinedValueRootIndex);
    __ j(not_equal, &validate_arguments, Label::kNear);
    __ mov(eax, Operand(ebp, kFunctionOffset));
    __ mov(Operand(ebp, kNewTargetOffset), eax);

    // Validate arguments
    __ bind(&validate_arguments);
    __ push(Operand(ebp, kFunctionOffset));
    __ push(Operand(ebp, kArgumentsOffset));
    __ push(Operand(ebp, kNewTargetOffset));
    __ InvokeBuiltin(Builtins::REFLECT_CONSTRUCT_PREPARE, CALL_FUNCTION);

    Generate_CheckStackOverflow(masm, kFunctionOffset, kEaxIsSmiTagged);

    // Push current index and limit.
    const int kLimitOffset =
        StandardFrameConstants::kExpressionsOffset - 1 * kPointerSize;
    const int kIndexOffset = kLimitOffset - 1 * kPointerSize;
    __ Push(eax);  // limit
    __ push(Immediate(0));  // index
    // Push the constructor function as callee.
    __ push(Operand(ebp, kFunctionOffset));

    // Loop over the arguments array, pushing each value to the stack
    Generate_PushAppliedArguments(
        masm, kArgumentsOffset, kIndexOffset, kLimitOffset);

    // Use undefined feedback vector
    __ LoadRoot(ebx, Heap::kUndefinedValueRootIndex);
    __ mov(edi, Operand(ebp, kFunctionOffset));
    __ mov(ecx, Operand(ebp, kNewTargetOffset));

    // Call the function.
    CallConstructStub stub(masm->isolate(), SUPER_CONSTRUCTOR_CALL);
    __ call(stub.GetCode(), RelocInfo::CONSTRUCT_CALL);

    // Leave internal frame.
  }
  // remove this, target, arguments, and newTarget
  __ ret(kStackSize * kPointerSize);
}


void Builtins::Generate_FunctionApply(MacroAssembler* masm) {
  Generate_ApplyHelper(masm, false);
}


void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  Generate_ApplyHelper(masm, true);
}


void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  Generate_ConstructHelper(masm);
}


void Builtins::Generate_InternalArrayCode(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : argc
  //  -- esp[0] : return address
  //  -- esp[4] : last argument
  // -----------------------------------
  Label generic_array_code;

  // Get the InternalArray function.
  __ LoadGlobalFunction(Context::INTERNAL_ARRAY_FUNCTION_INDEX, edi);

  if (FLAG_debug_code) {
    // Initial map for the builtin InternalArray function should be a map.
    __ mov(ebx, FieldOperand(edi, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    __ test(ebx, Immediate(kSmiTagMask));
    __ Assert(not_zero, kUnexpectedInitialMapForInternalArrayFunction);
    __ CmpObjectType(ebx, MAP_TYPE, ecx);
    __ Assert(equal, kUnexpectedInitialMapForInternalArrayFunction);
  }

  // Run the native code for the InternalArray function called as a normal
  // function.
  // tail call a stub
  InternalArrayConstructorStub stub(masm->isolate());
  __ TailCallStub(&stub);
}


void Builtins::Generate_ArrayCode(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : argc
  //  -- esp[0] : return address
  //  -- esp[4] : last argument
  // -----------------------------------
  Label generic_array_code;

  // Get the Array function.
  __ LoadGlobalFunction(Context::ARRAY_FUNCTION_INDEX, edi);
  __ mov(edx, edi);

  if (FLAG_debug_code) {
    // Initial map for the builtin Array function should be a map.
    __ mov(ebx, FieldOperand(edi, JSFunction::kPrototypeOrInitialMapOffset));
    // Will both indicate a NULL and a Smi.
    __ test(ebx, Immediate(kSmiTagMask));
    __ Assert(not_zero, kUnexpectedInitialMapForArrayFunction);
    __ CmpObjectType(ebx, MAP_TYPE, ecx);
    __ Assert(equal, kUnexpectedInitialMapForArrayFunction);
  }

  // Run the native code for the Array function called as a normal function.
  // tail call a stub
  __ mov(ebx, masm->isolate()->factory()->undefined_value());
  ArrayConstructorStub stub(masm->isolate());
  __ TailCallStub(&stub);
}


void Builtins::Generate_StringConstructCode(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax                 : number of arguments
  //  -- edi                 : constructor function
  //  -- esp[0]              : return address
  //  -- esp[(argc - n) * 4] : arg[n] (zero-based)
  //  -- esp[(argc + 1) * 4] : receiver
  // -----------------------------------
  Counters* counters = masm->isolate()->counters();
  __ IncrementCounter(counters->string_ctor_calls(), 1);

  if (FLAG_debug_code) {
    __ LoadGlobalFunction(Context::STRING_FUNCTION_INDEX, ecx);
    __ cmp(edi, ecx);
    __ Assert(equal, kUnexpectedStringFunction);
  }

  // Load the first argument into eax and get rid of the rest
  // (including the receiver).
  Label no_arguments;
  __ test(eax, eax);
  __ j(zero, &no_arguments);
  __ mov(ebx, Operand(esp, eax, times_pointer_size, 0));
  __ pop(ecx);
  __ lea(esp, Operand(esp, eax, times_pointer_size, kPointerSize));
  __ push(ecx);
  __ mov(eax, ebx);

  // Lookup the argument in the number to string cache.
  Label not_cached, argument_is_string;
  __ LookupNumberStringCache(eax,  // Input.
                             ebx,  // Result.
                             ecx,  // Scratch 1.
                             edx,  // Scratch 2.
                             &not_cached);
  __ IncrementCounter(counters->string_ctor_cached_number(), 1);
  __ bind(&argument_is_string);
  // ----------- S t a t e -------------
  //  -- ebx    : argument converted to string
  //  -- edi    : constructor function
  //  -- esp[0] : return address
  // -----------------------------------

  // Allocate a JSValue and put the tagged pointer into eax.
  Label gc_required;
  __ Allocate(JSValue::kSize,
              eax,  // Result.
              ecx,  // New allocation top (we ignore it).
              no_reg,
              &gc_required,
              TAG_OBJECT);

  // Set the map.
  __ LoadGlobalFunctionInitialMap(edi, ecx);
  if (FLAG_debug_code) {
    __ cmpb(FieldOperand(ecx, Map::kInstanceSizeOffset),
            JSValue::kSize >> kPointerSizeLog2);
    __ Assert(equal, kUnexpectedStringWrapperInstanceSize);
    __ cmpb(FieldOperand(ecx, Map::kUnusedPropertyFieldsOffset), 0);
    __ Assert(equal, kUnexpectedUnusedPropertiesOfStringWrapper);
  }
  __ mov(FieldOperand(eax, HeapObject::kMapOffset), ecx);

  // Set properties and elements.
  Factory* factory = masm->isolate()->factory();
  __ Move(ecx, Immediate(factory->empty_fixed_array()));
  __ mov(FieldOperand(eax, JSObject::kPropertiesOffset), ecx);
  __ mov(FieldOperand(eax, JSObject::kElementsOffset), ecx);

  // Set the value.
  __ mov(FieldOperand(eax, JSValue::kValueOffset), ebx);

  // Ensure the object is fully initialized.
  STATIC_ASSERT(JSValue::kSize == 4 * kPointerSize);

  // We're done. Return.
  __ ret(0);

  // The argument was not found in the number to string cache. Check
  // if it's a string already before calling the conversion builtin.
  Label convert_argument;
  __ bind(&not_cached);
  STATIC_ASSERT(kSmiTag == 0);
  __ JumpIfSmi(eax, &convert_argument);
  Condition is_string = masm->IsObjectStringType(eax, ebx, ecx);
  __ j(NegateCondition(is_string), &convert_argument);
  __ mov(ebx, eax);
  __ IncrementCounter(counters->string_ctor_string_value(), 1);
  __ jmp(&argument_is_string);

  // Invoke the conversion builtin and put the result into ebx.
  __ bind(&convert_argument);
  __ IncrementCounter(counters->string_ctor_conversions(), 1);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ push(edi);  // Preserve the function.
    __ push(eax);
    __ InvokeBuiltin(Builtins::TO_STRING, CALL_FUNCTION);
    __ pop(edi);
  }
  __ mov(ebx, eax);
  __ jmp(&argument_is_string);

  // Load the empty string into ebx, remove the receiver from the
  // stack, and jump back to the case where the argument is a string.
  __ bind(&no_arguments);
  __ Move(ebx, Immediate(factory->empty_string()));
  __ pop(ecx);
  __ lea(esp, Operand(esp, kPointerSize));
  __ push(ecx);
  __ jmp(&argument_is_string);

  // At this point the argument is already a string. Call runtime to
  // create a string wrapper.
  __ bind(&gc_required);
  __ IncrementCounter(counters->string_ctor_gc_required(), 1);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ push(ebx);
    __ CallRuntime(Runtime::kNewStringWrapper, 1);
  }
  __ ret(0);
}


static void ArgumentsAdaptorStackCheck(MacroAssembler* masm,
                                       Label* stack_overflow) {
  // ----------- S t a t e -------------
  //  -- eax : actual number of arguments
  //  -- ebx : expected number of arguments
  //  -- edi : function (passed through to callee)
  // -----------------------------------
  // Check the stack for overflow. We are not trying to catch
  // interruptions (e.g. debug break and preemption) here, so the "real stack
  // limit" is checked.
  ExternalReference real_stack_limit =
      ExternalReference::address_of_real_stack_limit(masm->isolate());
  __ mov(edx, Operand::StaticVariable(real_stack_limit));
  // Make ecx the space we have left. The stack might already be overflowed
  // here which will cause ecx to become negative.
  __ mov(ecx, esp);
  __ sub(ecx, edx);
  // Make edx the space we need for the array when it is unrolled onto the
  // stack.
  __ mov(edx, ebx);
  __ shl(edx, kPointerSizeLog2);
  // Check if the arguments will overflow the stack.
  __ cmp(ecx, edx);
  __ j(less_equal, stack_overflow);  // Signed comparison.
}


static void EnterArgumentsAdaptorFrame(MacroAssembler* masm) {
  __ push(ebp);
  __ mov(ebp, esp);

  // Store the arguments adaptor context sentinel.
  __ push(Immediate(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));

  // Push the function on the stack.
  __ push(edi);

  // Preserve the number of arguments on the stack. Must preserve eax,
  // ebx and ecx because these registers are used when copying the
  // arguments and the receiver.
  STATIC_ASSERT(kSmiTagSize == 1);
  __ lea(edi, Operand(eax, eax, times_1, kSmiTag));
  __ push(edi);
}


static void LeaveArgumentsAdaptorFrame(MacroAssembler* masm) {
  // Retrieve the number of arguments from the stack.
  __ mov(ebx, Operand(ebp, ArgumentsAdaptorFrameConstants::kLengthOffset));

  // Leave the frame.
  __ leave();

  // Remove caller arguments from the stack.
  STATIC_ASSERT(kSmiTagSize == 1 && kSmiTag == 0);
  __ pop(ecx);
  __ lea(esp, Operand(esp, ebx, times_2, 1 * kPointerSize));  // 1 ~ receiver
  __ push(ecx);
}


void Builtins::Generate_ArgumentsAdaptorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : actual number of arguments
  //  -- ebx : expected number of arguments
  //  -- edi : function (passed through to callee)
  // -----------------------------------

  Label invoke, dont_adapt_arguments;
  __ IncrementCounter(masm->isolate()->counters()->arguments_adaptors(), 1);

  Label stack_overflow;
  ArgumentsAdaptorStackCheck(masm, &stack_overflow);

  Label enough, too_few;
  __ mov(edx, FieldOperand(edi, JSFunction::kCodeEntryOffset));
  __ cmp(eax, ebx);
  __ j(less, &too_few);
  __ cmp(ebx, SharedFunctionInfo::kDontAdaptArgumentsSentinel);
  __ j(equal, &dont_adapt_arguments);

  {  // Enough parameters: Actual >= expected.
    __ bind(&enough);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all expected arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(eax, Operand(ebp, eax, times_4, offset));
    __ mov(edi, -1);  // account for receiver

    Label copy;
    __ bind(&copy);
    __ inc(edi);
    __ push(Operand(eax, 0));
    __ sub(eax, Immediate(kPointerSize));
    __ cmp(edi, ebx);
    __ j(less, &copy);
    __ jmp(&invoke);
  }

  {  // Too few parameters: Actual < expected.
    __ bind(&too_few);

    // If the function is strong we need to throw an error.
    Label no_strong_error;
    __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ test_b(FieldOperand(ecx, SharedFunctionInfo::kStrongModeByteOffset),
              1 << SharedFunctionInfo::kStrongModeBitWithinByte);
    __ j(equal, &no_strong_error, Label::kNear);

    // What we really care about is the required number of arguments.
    __ mov(ecx, FieldOperand(ecx, SharedFunctionInfo::kLengthOffset));
    __ SmiUntag(ecx);
    __ cmp(eax, ecx);
    __ j(greater_equal, &no_strong_error, Label::kNear);

    {
      FrameScope frame(masm, StackFrame::MANUAL);
      EnterArgumentsAdaptorFrame(masm);
      __ CallRuntime(Runtime::kThrowStrongModeTooFewArguments, 0);
    }

    __ bind(&no_strong_error);
    EnterArgumentsAdaptorFrame(masm);

    // Copy receiver and all actual arguments.
    const int offset = StandardFrameConstants::kCallerSPOffset;
    __ lea(edi, Operand(ebp, eax, times_4, offset));
    // ebx = expected - actual.
    __ sub(ebx, eax);
    // eax = -actual - 1
    __ neg(eax);
    __ sub(eax, Immediate(1));

    Label copy;
    __ bind(&copy);
    __ inc(eax);
    __ push(Operand(edi, 0));
    __ sub(edi, Immediate(kPointerSize));
    __ test(eax, eax);
    __ j(not_zero, &copy);

    // Fill remaining expected arguments with undefined values.
    Label fill;
    __ bind(&fill);
    __ inc(eax);
    __ push(Immediate(masm->isolate()->factory()->undefined_value()));
    __ cmp(eax, ebx);
    __ j(less, &fill);
  }

  // Call the entry point.
  __ bind(&invoke);
  // Restore function pointer.
  __ mov(edi, Operand(ebp, JavaScriptFrameConstants::kFunctionOffset));
  __ call(edx);

  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetArgumentsAdaptorDeoptPCOffset(masm->pc_offset());

  // Leave frame and return.
  LeaveArgumentsAdaptorFrame(masm);
  __ ret(0);

  // -------------------------------------------
  // Dont adapt arguments.
  // -------------------------------------------
  __ bind(&dont_adapt_arguments);
  __ jmp(edx);

  __ bind(&stack_overflow);
  {
    FrameScope frame(masm, StackFrame::MANUAL);
    EnterArgumentsAdaptorFrame(masm);
    __ InvokeBuiltin(Builtins::STACK_OVERFLOW, CALL_FUNCTION);
    __ int3();
  }
}


void Builtins::Generate_OnStackReplacement(MacroAssembler* masm) {
  // Lookup the function in the JavaScript frame.
  __ mov(eax, Operand(ebp, JavaScriptFrameConstants::kFunctionOffset));
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Pass function as argument.
    __ push(eax);
    __ CallRuntime(Runtime::kCompileForOnStackReplacement, 1);
  }

  Label skip;
  // If the code object is null, just return to the unoptimized code.
  __ cmp(eax, Immediate(0));
  __ j(not_equal, &skip, Label::kNear);
  __ ret(0);

  __ bind(&skip);

  // Load deoptimization data from the code object.
  __ mov(ebx, Operand(eax, Code::kDeoptimizationDataOffset - kHeapObjectTag));

  // Load the OSR entrypoint offset from the deoptimization data.
  __ mov(ebx, Operand(ebx, FixedArray::OffsetOfElementAt(
      DeoptimizationInputData::kOsrPcOffsetIndex) - kHeapObjectTag));
  __ SmiUntag(ebx);

  // Compute the target address = code_obj + header_size + osr_offset
  __ lea(eax, Operand(eax, ebx, times_1, Code::kHeaderSize - kHeapObjectTag));

  // Overwrite the return address on the stack.
  __ mov(Operand(esp, 0), eax);

  // And "return" to the OSR entry point of the function.
  __ ret(0);
}


void Builtins::Generate_OsrAfterStackCheck(MacroAssembler* masm) {
  // We check the stack limit as indicator that recompilation might be done.
  Label ok;
  ExternalReference stack_limit =
      ExternalReference::address_of_stack_limit(masm->isolate());
  __ cmp(esp, Operand::StaticVariable(stack_limit));
  __ j(above_equal, &ok, Label::kNear);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kStackGuard, 0);
  }
  __ jmp(masm->isolate()->builtins()->OnStackReplacement(),
         RelocInfo::CODE_TARGET);

  __ bind(&ok);
  __ ret(0);
}

#undef __
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X87

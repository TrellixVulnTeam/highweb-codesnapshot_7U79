// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Top include for all V8 .cc files.
//

#ifndef V8_V8_H_
#define V8_V8_H_

#if defined(GOOGLE3) || defined(DCHECK_ALWAYS_ON)
// Google3 and Chromium special flag handling.
#if defined(DEBUG) && defined(NDEBUG)
// V8 only uses DEBUG and whenever it is set we are building a debug
// version of V8. We do not use NDEBUG and simply undef it here for
// consistency.
#undef NDEBUG
#endif
#endif  // defined(GOOGLE3)

// V8 only uses DEBUG, but included external files
// may use NDEBUG - make sure they are consistent.
#if defined(DEBUG) && defined(NDEBUG)
#error both DEBUG and NDEBUG are set
#endif

// VMOLAB : macro for code snapshot
#define VMOLAB_CODESNAPSHOT true
#define VMOLAB_TIME true

// Basic includes
#include "include/v8.h"
#include "include/v8-platform.h"
#include "src/checks.h"  // NOLINT
#include "src/allocation.h"  // NOLINT
#include "src/assert-scope.h"  // NOLINT
#include "src/utils.h"  // NOLINT
#include "src/flags.h"  // NOLINT
#include "src/globals.h"  // NOLINT

// Objects & heap
#include "src/objects-inl.h"  // NOLINT
#include "src/heap/spaces-inl.h"               // NOLINT
#include "src/heap/heap-inl.h"                 // NOLINT
#include "src/heap/incremental-marking-inl.h"  // NOLINT
#include "src/heap/mark-compact-inl.h"         // NOLINT
#include "src/log-inl.h"  // NOLINT
#include "src/handles-inl.h"  // NOLINT
#include "src/types-inl.h"  // NOLINT

namespace v8 {
namespace internal {

class V8 : public AllStatic {
 public:
  // Global actions.

  static bool Initialize();
  static void TearDown();

  // Report process out of memory. Implementation found in api.cc.
  // This function will not return, but will terminate the execution.
  static void FatalProcessOutOfMemory(const char* location,
                                      bool take_snapshot = false);

  // Allows an entropy source to be provided for use in random number
  // generation.
  static void SetEntropySource(EntropySource source);
  // Support for return-address rewriting profilers.
  static void SetReturnAddressLocationResolver(
      ReturnAddressLocationResolver resolver);
  // Support for entry hooking JITed code.
  static void SetFunctionEntryHook(FunctionEntryHook entry_hook);

  static void InitializePlatform(v8::Platform* platform);
  static void ShutdownPlatform();
  static v8::Platform* GetCurrentPlatform();

  static void SetNativesBlob(StartupData* natives_blob);
  static void SetSnapshotBlob(StartupData* snapshot_blob);

 private:
  static void InitializeOncePerProcessImpl();
  static void InitializeOncePerProcess();

  // v8::Platform to use.
  static v8::Platform* platform_;
};


// JavaScript defines two kinds of 'nil'.
enum NilValue { kNullValue, kUndefinedValue };


} }  // namespace v8::internal

#endif  // V8_V8_H_

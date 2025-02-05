// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/unittests/compiler/node-test-utils.h"

#include <vector>

#include "src/assembler.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/unique.h"

using testing::_;
using testing::MakeMatcher;
using testing::MatcherInterface;
using testing::MatchResultListener;
using testing::StringMatchResultListener;

namespace v8 {
namespace internal {
namespace compiler {

namespace {

template <typename T>
bool PrintMatchAndExplain(const T& value, const std::string& value_name,
                          const Matcher<T>& value_matcher,
                          MatchResultListener* listener) {
  StringMatchResultListener value_listener;
  if (!value_matcher.MatchAndExplain(value, &value_listener)) {
    *listener << "whose " << value_name << " " << value << " doesn't match";
    if (value_listener.str() != "") {
      *listener << ", " << value_listener.str();
    }
    return false;
  }
  return true;
}


class NodeMatcher : public MatcherInterface<Node*> {
 public:
  explicit NodeMatcher(IrOpcode::Value opcode) : opcode_(opcode) {}

  void DescribeTo(std::ostream* os) const override {
    *os << "is a " << IrOpcode::Mnemonic(opcode_) << " node";
  }

  bool MatchAndExplain(Node* node,
                       MatchResultListener* listener) const override {
    if (node == NULL) {
      *listener << "which is NULL";
      return false;
    }
    if (node->opcode() != opcode_) {
      *listener << "whose opcode is " << IrOpcode::Mnemonic(node->opcode())
                << " but should have been " << IrOpcode::Mnemonic(opcode_);
      return false;
    }
    return true;
  }

 private:
  const IrOpcode::Value opcode_;
};


class IsBranchMatcher final : public NodeMatcher {
 public:
  IsBranchMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kBranch),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsSwitchMatcher final : public NodeMatcher {
 public:
  IsSwitchMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kSwitch),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsIfValueMatcher final : public NodeMatcher {
 public:
  IsIfValueMatcher(const Matcher<int32_t>& value_matcher,
                   const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kIfValue),
        value_matcher_(value_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<int32_t>(node->op()), "value",
                                 value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<int32_t> value_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsControl1Matcher final : public NodeMatcher {
 public:
  IsControl1Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control_matcher)
      : NodeMatcher(opcode), control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> control_matcher_;
};


class IsControl2Matcher final : public NodeMatcher {
 public:
  IsControl2Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control0_matcher,
                    const Matcher<Node*>& control1_matcher)
      : NodeMatcher(opcode),
        control0_matcher_(control0_matcher),
        control1_matcher_(control1_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose control0 (";
    control0_matcher_.DescribeTo(os);
    *os << ") and control1 (";
    control1_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 0),
                                 "control0", control0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 1),
                                 "control1", control1_matcher_, listener));
  }

 private:
  const Matcher<Node*> control0_matcher_;
  const Matcher<Node*> control1_matcher_;
};


class IsControl3Matcher final : public NodeMatcher {
 public:
  IsControl3Matcher(IrOpcode::Value opcode,
                    const Matcher<Node*>& control0_matcher,
                    const Matcher<Node*>& control1_matcher,
                    const Matcher<Node*>& control2_matcher)
      : NodeMatcher(opcode),
        control0_matcher_(control0_matcher),
        control1_matcher_(control1_matcher),
        control2_matcher_(control2_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose control0 (";
    control0_matcher_.DescribeTo(os);
    *os << ") and control1 (";
    control1_matcher_.DescribeTo(os);
    *os << ") and control2 (";
    control2_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 0),
                                 "control0", control0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 1),
                                 "control1", control1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node, 2),
                                 "control2", control2_matcher_, listener));
  }

 private:
  const Matcher<Node*> control0_matcher_;
  const Matcher<Node*> control1_matcher_;
  const Matcher<Node*> control2_matcher_;
};


class IsFinishMatcher final : public NodeMatcher {
 public:
  IsFinishMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& effect_matcher)
      : NodeMatcher(IrOpcode::kFinish),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and effect (";
    effect_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
};


class IsReturnMatcher final : public NodeMatcher {
 public:
  IsReturnMatcher(const Matcher<Node*>& value_matcher,
                  const Matcher<Node*>& effect_matcher,
                  const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kReturn),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ") and effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsTerminateMatcher final : public NodeMatcher {
 public:
  IsTerminateMatcher(const Matcher<Node*>& effect_matcher,
                     const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kTerminate),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


template <typename T>
class IsConstantMatcher final : public NodeMatcher {
 public:
  IsConstantMatcher(IrOpcode::Value opcode, const Matcher<T>& value_matcher)
      : NodeMatcher(opcode), value_matcher_(value_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose value (";
    value_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<T>(node), "value", value_matcher_,
                                 listener));
  }

 private:
  const Matcher<T> value_matcher_;
};


class IsSelectMatcher final : public NodeMatcher {
 public:
  IsSelectMatcher(const Matcher<MachineType>& type_matcher,
                  const Matcher<Node*>& value0_matcher,
                  const Matcher<Node*>& value1_matcher,
                  const Matcher<Node*>& value2_matcher)
      : NodeMatcher(IrOpcode::kSelect),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        value2_matcher_(value2_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose type (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << ") and value2 (";
    value2_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<MachineType>(node), "type",
                                 type_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value0", value0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value1", value1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value2", value2_matcher_, listener));
  }

 private:
  const Matcher<MachineType> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> value2_matcher_;
};


class IsPhiMatcher final : public NodeMatcher {
 public:
  IsPhiMatcher(const Matcher<MachineType>& type_matcher,
               const Matcher<Node*>& value0_matcher,
               const Matcher<Node*>& value1_matcher,
               const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kPhi),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose type (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<MachineType>(node), "type",
                                 type_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value0", value0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value1", value1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<MachineType> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsPhi2Matcher final : public NodeMatcher {
 public:
  IsPhi2Matcher(const Matcher<MachineType>& type_matcher,
                const Matcher<Node*>& value0_matcher,
                const Matcher<Node*>& value1_matcher,
                const Matcher<Node*>& value2_matcher,
                const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kPhi),
        type_matcher_(type_matcher),
        value0_matcher_(value0_matcher),
        value1_matcher_(value1_matcher),
        value2_matcher_(value2_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose type (";
    type_matcher_.DescribeTo(os);
    *os << "), value0 (";
    value0_matcher_.DescribeTo(os);
    *os << "), value1 (";
    value1_matcher_.DescribeTo(os);
    *os << "), value2 (";
    value2_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<MachineType>(node), "type",
                                 type_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "value0", value0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value1", value1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value2", value2_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<MachineType> type_matcher_;
  const Matcher<Node*> value0_matcher_;
  const Matcher<Node*> value1_matcher_;
  const Matcher<Node*> value2_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsEffectPhiMatcher final : public NodeMatcher {
 public:
  IsEffectPhiMatcher(const Matcher<Node*>& effect0_matcher,
                     const Matcher<Node*>& effect1_matcher,
                     const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kEffectPhi),
        effect0_matcher_(effect0_matcher),
        effect1_matcher_(effect1_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << "), effect0 (";
    effect0_matcher_.DescribeTo(os);
    *os << "), effect1 (";
    effect1_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node, 0),
                                 "effect0", effect0_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node, 1),
                                 "effect1", effect1_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> effect0_matcher_;
  const Matcher<Node*> effect1_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsEffectSetMatcher final : public NodeMatcher {
 public:
  IsEffectSetMatcher(const Matcher<Node*>& effect0_matcher,
                     const Matcher<Node*>& effect1_matcher)
      : NodeMatcher(IrOpcode::kEffectSet),
        effect0_matcher_(effect0_matcher),
        effect1_matcher_(effect1_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << "), effect0 (";
    effect0_matcher_.DescribeTo(os);
    *os << ") and effect1 (";
    effect1_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    if (!NodeMatcher::MatchAndExplain(node, listener)) return false;

    Node* effect0 = NodeProperties::GetEffectInput(node, 0);
    Node* effect1 = NodeProperties::GetEffectInput(node, 1);

    {
      // Try matching in the reverse order first.
      StringMatchResultListener value_listener;
      if (effect0_matcher_.MatchAndExplain(effect1, &value_listener) &&
          effect1_matcher_.MatchAndExplain(effect0, &value_listener)) {
        return true;
      }
    }

    return PrintMatchAndExplain(effect0, "effect0", effect0_matcher_,
                                listener) &&
           PrintMatchAndExplain(effect1, "effect1", effect1_matcher_, listener);
  }

 private:
  const Matcher<Node*> effect0_matcher_;
  const Matcher<Node*> effect1_matcher_;
};


class IsProjectionMatcher final : public NodeMatcher {
 public:
  IsProjectionMatcher(const Matcher<size_t>& index_matcher,
                      const Matcher<Node*>& base_matcher)
      : NodeMatcher(IrOpcode::kProjection),
        index_matcher_(index_matcher),
        base_matcher_(base_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose index (";
    index_matcher_.DescribeTo(os);
    *os << ") and base (";
    base_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<size_t>(node), "index",
                                 index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener));
  }

 private:
  const Matcher<size_t> index_matcher_;
  const Matcher<Node*> base_matcher_;
};


class IsCallMatcher final : public NodeMatcher {
 public:
  IsCallMatcher(const Matcher<CallDescriptor*>& descriptor_matcher,
                const std::vector<Matcher<Node*>>& value_matchers,
                const Matcher<Node*>& effect_matcher,
                const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kCall),
        descriptor_matcher_(descriptor_matcher),
        value_matchers_(value_matchers),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      if (i == 0) {
        *os << " whose value0 (";
      } else {
        *os << "), value" << i << " (";
      }
      value_matchers_[i].DescribeTo(os);
    }
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    if (!NodeMatcher::MatchAndExplain(node, listener) ||
        !PrintMatchAndExplain(OpParameter<CallDescriptor*>(node), "descriptor",
                              descriptor_matcher_, listener)) {
      return false;
    }
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      std::ostringstream ost;
      ost << "value" << i;
      if (!PrintMatchAndExplain(
              NodeProperties::GetValueInput(node, static_cast<int>(i)),
              ost.str(), value_matchers_[i], listener)) {
        return false;
      }
    }
    return (PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<CallDescriptor*> descriptor_matcher_;
  const std::vector<Matcher<Node*>> value_matchers_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsTailCallMatcher final : public NodeMatcher {
 public:
  IsTailCallMatcher(const Matcher<CallDescriptor const*>& descriptor_matcher,
                    const std::vector<Matcher<Node*>>& value_matchers,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kTailCall),
        descriptor_matcher_(descriptor_matcher),
        value_matchers_(value_matchers),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      if (i == 0) {
        *os << " whose value0 (";
      } else {
        *os << "), value" << i << " (";
      }
      value_matchers_[i].DescribeTo(os);
    }
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    if (!NodeMatcher::MatchAndExplain(node, listener) ||
        !PrintMatchAndExplain(OpParameter<CallDescriptor const*>(node),
                              "descriptor", descriptor_matcher_, listener)) {
      return false;
    }
    for (size_t i = 0; i < value_matchers_.size(); ++i) {
      std::ostringstream ost;
      ost << "value" << i;
      if (!PrintMatchAndExplain(
              NodeProperties::GetValueInput(node, static_cast<int>(i)),
              ost.str(), value_matchers_[i], listener)) {
        return false;
      }
    }
    return (PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<CallDescriptor const*> descriptor_matcher_;
  const std::vector<Matcher<Node*>> value_matchers_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsReferenceEqualMatcher final : public NodeMatcher {
 public:
  IsReferenceEqualMatcher(const Matcher<Type*>& type_matcher,
                          const Matcher<Node*>& lhs_matcher,
                          const Matcher<Node*>& rhs_matcher)
      : NodeMatcher(IrOpcode::kReferenceEqual),
        type_matcher_(type_matcher),
        lhs_matcher_(lhs_matcher),
        rhs_matcher_(rhs_matcher) {}

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            // TODO(bmeurer): The type parameter is currently ignored.
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "rhs",
                                 rhs_matcher_, listener));
  }

 private:
  const Matcher<Type*> type_matcher_;
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> rhs_matcher_;
};


class IsAllocateMatcher final : public NodeMatcher {
 public:
  IsAllocateMatcher(const Matcher<Node*>& size_matcher,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kAllocate),
        size_matcher_(size_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "size",
                                 size_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> size_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsLoadFieldMatcher final : public NodeMatcher {
 public:
  IsLoadFieldMatcher(const Matcher<FieldAccess>& access_matcher,
                     const Matcher<Node*>& base_matcher,
                     const Matcher<Node*>& effect_matcher,
                     const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kLoadField),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<FieldAccess>(node), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<FieldAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsStoreFieldMatcher final : public NodeMatcher {
 public:
  IsStoreFieldMatcher(const Matcher<FieldAccess>& access_matcher,
                      const Matcher<Node*>& base_matcher,
                      const Matcher<Node*>& value_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kStoreField),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<FieldAccess>(node), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<FieldAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsLoadBufferMatcher final : public NodeMatcher {
 public:
  IsLoadBufferMatcher(const Matcher<BufferAccess>& access_matcher,
                      const Matcher<Node*>& buffer_matcher,
                      const Matcher<Node*>& offset_matcher,
                      const Matcher<Node*>& length_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kLoadBuffer),
        access_matcher_(access_matcher),
        buffer_matcher_(buffer_matcher),
        offset_matcher_(offset_matcher),
        length_matcher_(length_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), buffer (";
    buffer_matcher_.DescribeTo(os);
    *os << "), offset (";
    offset_matcher_.DescribeTo(os);
    *os << "), length (";
    length_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(BufferAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "buffer", buffer_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "offset", offset_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "length", length_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<BufferAccess> access_matcher_;
  const Matcher<Node*> buffer_matcher_;
  const Matcher<Node*> offset_matcher_;
  const Matcher<Node*> length_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsStoreBufferMatcher final : public NodeMatcher {
 public:
  IsStoreBufferMatcher(const Matcher<BufferAccess>& access_matcher,
                       const Matcher<Node*>& buffer_matcher,
                       const Matcher<Node*>& offset_matcher,
                       const Matcher<Node*>& length_matcher,
                       const Matcher<Node*>& value_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kStoreBuffer),
        access_matcher_(access_matcher),
        buffer_matcher_(buffer_matcher),
        offset_matcher_(offset_matcher),
        length_matcher_(length_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), buffer (";
    buffer_matcher_.DescribeTo(os);
    *os << "), offset (";
    offset_matcher_.DescribeTo(os);
    *os << "), length (";
    length_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(BufferAccessOf(node->op()), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "buffer", buffer_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "offset", offset_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "length", length_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 3),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<BufferAccess> access_matcher_;
  const Matcher<Node*> buffer_matcher_;
  const Matcher<Node*> offset_matcher_;
  const Matcher<Node*> length_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsLoadElementMatcher final : public NodeMatcher {
 public:
  IsLoadElementMatcher(const Matcher<ElementAccess>& access_matcher,
                       const Matcher<Node*>& base_matcher,
                       const Matcher<Node*>& index_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kLoadElement),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<ElementAccess>(node), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<ElementAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsStoreElementMatcher final : public NodeMatcher {
 public:
  IsStoreElementMatcher(const Matcher<ElementAccess>& access_matcher,
                        const Matcher<Node*>& base_matcher,
                        const Matcher<Node*>& index_matcher,
                        const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher,
                        const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kStoreElement),
        access_matcher_(access_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<ElementAccess>(node), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<ElementAccess> access_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsLoadMatcher final : public NodeMatcher {
 public:
  IsLoadMatcher(const Matcher<LoadRepresentation>& rep_matcher,
                const Matcher<Node*>& base_matcher,
                const Matcher<Node*>& index_matcher,
                const Matcher<Node*>& effect_matcher,
                const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kLoad),
        rep_matcher_(rep_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose rep (";
    rep_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<LoadRepresentation>(node), "rep",
                                 rep_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<LoadRepresentation> rep_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsStoreMatcher final : public NodeMatcher {
 public:
  IsStoreMatcher(const Matcher<StoreRepresentation>& rep_matcher,
                 const Matcher<Node*>& base_matcher,
                 const Matcher<Node*>& index_matcher,
                 const Matcher<Node*>& value_matcher,
                 const Matcher<Node*>& effect_matcher,
                 const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kStore),
        rep_matcher_(rep_matcher),
        base_matcher_(base_matcher),
        index_matcher_(index_matcher),
        value_matcher_(value_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose rep (";
    rep_matcher_.DescribeTo(os);
    *os << "), base (";
    base_matcher_.DescribeTo(os);
    *os << "), index (";
    index_matcher_.DescribeTo(os);
    *os << "), value (";
    value_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<StoreRepresentation>(node), "rep",
                                 rep_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1),
                                 "index", index_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 2),
                                 "value", value_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<StoreRepresentation> rep_matcher_;
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> index_matcher_;
  const Matcher<Node*> value_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsToNumberMatcher final : public NodeMatcher {
 public:
  IsToNumberMatcher(const Matcher<Node*>& base_matcher,
                    const Matcher<Node*>& context_matcher,
                    const Matcher<Node*>& effect_matcher,
                    const Matcher<Node*>& control_matcher)
      : NodeMatcher(IrOpcode::kJSToNumber),
        base_matcher_(base_matcher),
        context_matcher_(context_matcher),
        effect_matcher_(effect_matcher),
        control_matcher_(control_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose base (";
    base_matcher_.DescribeTo(os);
    *os << "), context (";
    context_matcher_.DescribeTo(os);
    *os << "), effect (";
    effect_matcher_.DescribeTo(os);
    *os << ") and control (";
    control_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "base",
                                 base_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetContextInput(node),
                                 "context", context_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetEffectInput(node), "effect",
                                 effect_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetControlInput(node),
                                 "control", control_matcher_, listener));
  }

 private:
  const Matcher<Node*> base_matcher_;
  const Matcher<Node*> context_matcher_;
  const Matcher<Node*> effect_matcher_;
  const Matcher<Node*> control_matcher_;
};


class IsLoadContextMatcher final : public NodeMatcher {
 public:
  IsLoadContextMatcher(const Matcher<ContextAccess>& access_matcher,
                       const Matcher<Node*>& context_matcher)
      : NodeMatcher(IrOpcode::kJSLoadContext),
        access_matcher_(access_matcher),
        context_matcher_(context_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose access (";
    access_matcher_.DescribeTo(os);
    *os << ") and context (";
    context_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(OpParameter<ContextAccess>(node), "access",
                                 access_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetContextInput(node),
                                 "context", context_matcher_, listener));
  }

 private:
  const Matcher<ContextAccess> access_matcher_;
  const Matcher<Node*> context_matcher_;
};


class IsBinopMatcher final : public NodeMatcher {
 public:
  IsBinopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& lhs_matcher,
                 const Matcher<Node*>& rhs_matcher)
      : NodeMatcher(opcode),
        lhs_matcher_(lhs_matcher),
        rhs_matcher_(rhs_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose lhs (";
    lhs_matcher_.DescribeTo(os);
    *os << ") and rhs (";
    rhs_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0), "lhs",
                                 lhs_matcher_, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 1), "rhs",
                                 rhs_matcher_, listener));
  }

 private:
  const Matcher<Node*> lhs_matcher_;
  const Matcher<Node*> rhs_matcher_;
};


class IsUnopMatcher final : public NodeMatcher {
 public:
  IsUnopMatcher(IrOpcode::Value opcode, const Matcher<Node*>& input_matcher)
      : NodeMatcher(opcode), input_matcher_(input_matcher) {}

  void DescribeTo(std::ostream* os) const final {
    NodeMatcher::DescribeTo(os);
    *os << " whose input (";
    input_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(NodeProperties::GetValueInput(node, 0),
                                 "input", input_matcher_, listener));
  }

 private:
  const Matcher<Node*> input_matcher_;
};

class IsParameterMatcher final : public NodeMatcher {
 public:
  explicit IsParameterMatcher(const Matcher<int>& index_matcher)
      : NodeMatcher(IrOpcode::kParameter), index_matcher_(index_matcher) {}

  void DescribeTo(std::ostream* os) const override {
    *os << "is a Parameter node with index(";
    index_matcher_.DescribeTo(os);
    *os << ")";
  }

  bool MatchAndExplain(Node* node, MatchResultListener* listener) const final {
    return (NodeMatcher::MatchAndExplain(node, listener) &&
            PrintMatchAndExplain(ParameterIndexOf(node->op()), "index",
                                 index_matcher_, listener));
  }

 private:
  const Matcher<int> index_matcher_;
};

}  // namespace


Matcher<Node*> IsDead() {
  return MakeMatcher(new NodeMatcher(IrOpcode::kDead));
}


Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher) {
  return MakeMatcher(new IsControl1Matcher(IrOpcode::kEnd, control0_matcher));
}


Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher,
                     const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kEnd, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsEnd(const Matcher<Node*>& control0_matcher,
                     const Matcher<Node*>& control1_matcher,
                     const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kEnd, control0_matcher,
                                           control1_matcher, control2_matcher));
}


Matcher<Node*> IsBranch(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsBranchMatcher(value_matcher, control_matcher));
}


Matcher<Node*> IsMerge(const Matcher<Node*>& control0_matcher,
                       const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kMerge, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsMerge(const Matcher<Node*>& control0_matcher,
                       const Matcher<Node*>& control1_matcher,
                       const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kMerge, control0_matcher,
                                           control1_matcher, control2_matcher));
}


Matcher<Node*> IsLoop(const Matcher<Node*>& control0_matcher,
                      const Matcher<Node*>& control1_matcher) {
  return MakeMatcher(new IsControl2Matcher(IrOpcode::kLoop, control0_matcher,
                                           control1_matcher));
}


Matcher<Node*> IsLoop(const Matcher<Node*>& control0_matcher,
                      const Matcher<Node*>& control1_matcher,
                      const Matcher<Node*>& control2_matcher) {
  return MakeMatcher(new IsControl3Matcher(IrOpcode::kLoop, control0_matcher,
                                           control1_matcher, control2_matcher));
}


Matcher<Node*> IsIfTrue(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsControl1Matcher(IrOpcode::kIfTrue, control_matcher));
}


Matcher<Node*> IsIfFalse(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfFalse, control_matcher));
}


Matcher<Node*> IsIfSuccess(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfSuccess, control_matcher));
}


Matcher<Node*> IsSwitch(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsSwitchMatcher(value_matcher, control_matcher));
}


Matcher<Node*> IsIfValue(const Matcher<int32_t>& value_matcher,
                         const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsIfValueMatcher(value_matcher, control_matcher));
}


Matcher<Node*> IsIfDefault(const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsControl1Matcher(IrOpcode::kIfDefault, control_matcher));
}


Matcher<Node*> IsValueEffect(const Matcher<Node*>& value_matcher) {
  return MakeMatcher(new IsUnopMatcher(IrOpcode::kValueEffect, value_matcher));
}


Matcher<Node*> IsFinish(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher) {
  return MakeMatcher(new IsFinishMatcher(value_matcher, effect_matcher));
}


Matcher<Node*> IsReturn(const Matcher<Node*>& value_matcher,
                        const Matcher<Node*>& effect_matcher,
                        const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsReturnMatcher(value_matcher, effect_matcher, control_matcher));
}


Matcher<Node*> IsTerminate(const Matcher<Node*>& effect_matcher,
                           const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsTerminateMatcher(effect_matcher, control_matcher));
}


Matcher<Node*> IsExternalConstant(
    const Matcher<ExternalReference>& value_matcher) {
  return MakeMatcher(new IsConstantMatcher<ExternalReference>(
      IrOpcode::kExternalConstant, value_matcher));
}


Matcher<Node*> IsHeapConstant(
    const Matcher<Unique<HeapObject> >& value_matcher) {
  return MakeMatcher(new IsConstantMatcher<Unique<HeapObject> >(
      IrOpcode::kHeapConstant, value_matcher));
}


Matcher<Node*> IsInt32Constant(const Matcher<int32_t>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<int32_t>(IrOpcode::kInt32Constant, value_matcher));
}


Matcher<Node*> IsInt64Constant(const Matcher<int64_t>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<int64_t>(IrOpcode::kInt64Constant, value_matcher));
}


Matcher<Node*> IsFloat32Constant(const Matcher<float>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<float>(IrOpcode::kFloat32Constant, value_matcher));
}


Matcher<Node*> IsFloat64Constant(const Matcher<double>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<double>(IrOpcode::kFloat64Constant, value_matcher));
}


Matcher<Node*> IsNumberConstant(const Matcher<double>& value_matcher) {
  return MakeMatcher(
      new IsConstantMatcher<double>(IrOpcode::kNumberConstant, value_matcher));
}


Matcher<Node*> IsSelect(const Matcher<MachineType>& type_matcher,
                        const Matcher<Node*>& value0_matcher,
                        const Matcher<Node*>& value1_matcher,
                        const Matcher<Node*>& value2_matcher) {
  return MakeMatcher(new IsSelectMatcher(type_matcher, value0_matcher,
                                         value1_matcher, value2_matcher));
}


Matcher<Node*> IsPhi(const Matcher<MachineType>& type_matcher,
                     const Matcher<Node*>& value0_matcher,
                     const Matcher<Node*>& value1_matcher,
                     const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(new IsPhiMatcher(type_matcher, value0_matcher,
                                      value1_matcher, merge_matcher));
}


Matcher<Node*> IsPhi(const Matcher<MachineType>& type_matcher,
                     const Matcher<Node*>& value0_matcher,
                     const Matcher<Node*>& value1_matcher,
                     const Matcher<Node*>& value2_matcher,
                     const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(new IsPhi2Matcher(type_matcher, value0_matcher,
                                       value1_matcher, value2_matcher,
                                       merge_matcher));
}


Matcher<Node*> IsEffectPhi(const Matcher<Node*>& effect0_matcher,
                           const Matcher<Node*>& effect1_matcher,
                           const Matcher<Node*>& merge_matcher) {
  return MakeMatcher(
      new IsEffectPhiMatcher(effect0_matcher, effect1_matcher, merge_matcher));
}


Matcher<Node*> IsEffectSet(const Matcher<Node*>& effect0_matcher,
                           const Matcher<Node*>& effect1_matcher) {
  return MakeMatcher(new IsEffectSetMatcher(effect0_matcher, effect1_matcher));
}


Matcher<Node*> IsProjection(const Matcher<size_t>& index_matcher,
                            const Matcher<Node*>& base_matcher) {
  return MakeMatcher(new IsProjectionMatcher(index_matcher, base_matcher));
}


Matcher<Node*> IsCall(const Matcher<CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& value4_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(const Matcher<CallDescriptor*>& descriptor_matcher,
                      const Matcher<Node*>& value0_matcher,
                      const Matcher<Node*>& value1_matcher,
                      const Matcher<Node*>& value2_matcher,
                      const Matcher<Node*>& value3_matcher,
                      const Matcher<Node*>& value4_matcher,
                      const Matcher<Node*>& value5_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsCall(
    const Matcher<CallDescriptor*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& value3_matcher,
    const Matcher<Node*>& value4_matcher, const Matcher<Node*>& value5_matcher,
    const Matcher<Node*>& value6_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  value_matchers.push_back(value3_matcher);
  value_matchers.push_back(value4_matcher);
  value_matchers.push_back(value5_matcher);
  value_matchers.push_back(value6_matcher);
  return MakeMatcher(new IsCallMatcher(descriptor_matcher, value_matchers,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsTailCall(
    const Matcher<CallDescriptor const*>& descriptor_matcher,
    const Matcher<Node*>& value0_matcher, const Matcher<Node*>& value1_matcher,
    const Matcher<Node*>& value2_matcher, const Matcher<Node*>& effect_matcher,
    const Matcher<Node*>& control_matcher) {
  std::vector<Matcher<Node*>> value_matchers;
  value_matchers.push_back(value0_matcher);
  value_matchers.push_back(value1_matcher);
  value_matchers.push_back(value2_matcher);
  return MakeMatcher(new IsTailCallMatcher(descriptor_matcher, value_matchers,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsReferenceEqual(const Matcher<Type*>& type_matcher,
                                const Matcher<Node*>& lhs_matcher,
                                const Matcher<Node*>& rhs_matcher) {
  return MakeMatcher(
      new IsReferenceEqualMatcher(type_matcher, lhs_matcher, rhs_matcher));
}


Matcher<Node*> IsAllocate(const Matcher<Node*>& size_matcher,
                          const Matcher<Node*>& effect_matcher,
                          const Matcher<Node*>& control_matcher) {
  return MakeMatcher(
      new IsAllocateMatcher(size_matcher, effect_matcher, control_matcher));
}


Matcher<Node*> IsLoadField(const Matcher<FieldAccess>& access_matcher,
                           const Matcher<Node*>& base_matcher,
                           const Matcher<Node*>& effect_matcher,
                           const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadFieldMatcher(access_matcher, base_matcher,
                                            effect_matcher, control_matcher));
}


Matcher<Node*> IsStoreField(const Matcher<FieldAccess>& access_matcher,
                            const Matcher<Node*>& base_matcher,
                            const Matcher<Node*>& value_matcher,
                            const Matcher<Node*>& effect_matcher,
                            const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreFieldMatcher(access_matcher, base_matcher,
                                             value_matcher, effect_matcher,
                                             control_matcher));
}


Matcher<Node*> IsLoadBuffer(const Matcher<BufferAccess>& access_matcher,
                            const Matcher<Node*>& buffer_matcher,
                            const Matcher<Node*>& offset_matcher,
                            const Matcher<Node*>& length_matcher,
                            const Matcher<Node*>& effect_matcher,
                            const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadBufferMatcher(access_matcher, buffer_matcher,
                                             offset_matcher, length_matcher,
                                             effect_matcher, control_matcher));
}


Matcher<Node*> IsStoreBuffer(const Matcher<BufferAccess>& access_matcher,
                             const Matcher<Node*>& buffer_matcher,
                             const Matcher<Node*>& offset_matcher,
                             const Matcher<Node*>& length_matcher,
                             const Matcher<Node*>& value_matcher,
                             const Matcher<Node*>& effect_matcher,
                             const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreBufferMatcher(
      access_matcher, buffer_matcher, offset_matcher, length_matcher,
      value_matcher, effect_matcher, control_matcher));
}


Matcher<Node*> IsLoadElement(const Matcher<ElementAccess>& access_matcher,
                             const Matcher<Node*>& base_matcher,
                             const Matcher<Node*>& index_matcher,
                             const Matcher<Node*>& effect_matcher,
                             const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadElementMatcher(access_matcher, base_matcher,
                                              index_matcher, effect_matcher,
                                              control_matcher));
}


Matcher<Node*> IsStoreElement(const Matcher<ElementAccess>& access_matcher,
                              const Matcher<Node*>& base_matcher,
                              const Matcher<Node*>& index_matcher,
                              const Matcher<Node*>& value_matcher,
                              const Matcher<Node*>& effect_matcher,
                              const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreElementMatcher(
      access_matcher, base_matcher, index_matcher, value_matcher,
      effect_matcher, control_matcher));
}


Matcher<Node*> IsLoad(const Matcher<LoadRepresentation>& rep_matcher,
                      const Matcher<Node*>& base_matcher,
                      const Matcher<Node*>& index_matcher,
                      const Matcher<Node*>& effect_matcher,
                      const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsLoadMatcher(rep_matcher, base_matcher, index_matcher,
                                       effect_matcher, control_matcher));
}


Matcher<Node*> IsStore(const Matcher<StoreRepresentation>& rep_matcher,
                       const Matcher<Node*>& base_matcher,
                       const Matcher<Node*>& index_matcher,
                       const Matcher<Node*>& value_matcher,
                       const Matcher<Node*>& effect_matcher,
                       const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsStoreMatcher(rep_matcher, base_matcher,
                                        index_matcher, value_matcher,
                                        effect_matcher, control_matcher));
}


Matcher<Node*> IsToNumber(const Matcher<Node*>& base_matcher,
                          const Matcher<Node*>& context_matcher,
                          const Matcher<Node*>& effect_matcher,
                          const Matcher<Node*>& control_matcher) {
  return MakeMatcher(new IsToNumberMatcher(base_matcher, context_matcher,
                                           effect_matcher, control_matcher));
}


Matcher<Node*> IsLoadContext(const Matcher<ContextAccess>& access_matcher,
                             const Matcher<Node*>& context_matcher) {
  return MakeMatcher(new IsLoadContextMatcher(access_matcher, context_matcher));
}


Matcher<Node*> IsParameter(const Matcher<int> index_matcher) {
  return MakeMatcher(new IsParameterMatcher(index_matcher));
}


Matcher<Node*> IsLoadFramePointer() {
  return MakeMatcher(new NodeMatcher(IrOpcode::kLoadFramePointer));
}


#define IS_BINOP_MATCHER(Name)                                            \
  Matcher<Node*> Is##Name(const Matcher<Node*>& lhs_matcher,              \
                          const Matcher<Node*>& rhs_matcher) {            \
    return MakeMatcher(                                                   \
        new IsBinopMatcher(IrOpcode::k##Name, lhs_matcher, rhs_matcher)); \
  }
IS_BINOP_MATCHER(NumberEqual)
IS_BINOP_MATCHER(NumberLessThan)
IS_BINOP_MATCHER(NumberSubtract)
IS_BINOP_MATCHER(NumberMultiply)
IS_BINOP_MATCHER(NumberShiftLeft)
IS_BINOP_MATCHER(NumberShiftRight)
IS_BINOP_MATCHER(NumberShiftRightLogical)
IS_BINOP_MATCHER(Word32And)
IS_BINOP_MATCHER(Word32Sar)
IS_BINOP_MATCHER(Word32Shl)
IS_BINOP_MATCHER(Word32Shr)
IS_BINOP_MATCHER(Word32Ror)
IS_BINOP_MATCHER(Word32Equal)
IS_BINOP_MATCHER(Word64And)
IS_BINOP_MATCHER(Word64Sar)
IS_BINOP_MATCHER(Word64Shl)
IS_BINOP_MATCHER(Word64Equal)
IS_BINOP_MATCHER(Int32AddWithOverflow)
IS_BINOP_MATCHER(Int32Add)
IS_BINOP_MATCHER(Int32Sub)
IS_BINOP_MATCHER(Int32Mul)
IS_BINOP_MATCHER(Int32MulHigh)
IS_BINOP_MATCHER(Int32LessThan)
IS_BINOP_MATCHER(Uint32LessThan)
IS_BINOP_MATCHER(Uint32LessThanOrEqual)
IS_BINOP_MATCHER(Int64Add)
IS_BINOP_MATCHER(Float32Max)
IS_BINOP_MATCHER(Float32Min)
IS_BINOP_MATCHER(Float32Equal)
IS_BINOP_MATCHER(Float32LessThan)
IS_BINOP_MATCHER(Float32LessThanOrEqual)
IS_BINOP_MATCHER(Float64Max)
IS_BINOP_MATCHER(Float64Min)
IS_BINOP_MATCHER(Float64Sub)
IS_BINOP_MATCHER(Float64InsertLowWord32)
IS_BINOP_MATCHER(Float64InsertHighWord32)
#undef IS_BINOP_MATCHER


#define IS_UNOP_MATCHER(Name)                                                \
  Matcher<Node*> Is##Name(const Matcher<Node*>& input_matcher) {             \
    return MakeMatcher(new IsUnopMatcher(IrOpcode::k##Name, input_matcher)); \
  }
IS_UNOP_MATCHER(BooleanNot)
IS_UNOP_MATCHER(ChangeFloat64ToInt32)
IS_UNOP_MATCHER(ChangeFloat64ToUint32)
IS_UNOP_MATCHER(ChangeInt32ToFloat64)
IS_UNOP_MATCHER(ChangeInt32ToInt64)
IS_UNOP_MATCHER(ChangeUint32ToFloat64)
IS_UNOP_MATCHER(ChangeUint32ToUint64)
IS_UNOP_MATCHER(TruncateFloat64ToFloat32)
IS_UNOP_MATCHER(TruncateFloat64ToInt32)
IS_UNOP_MATCHER(TruncateInt64ToInt32)
IS_UNOP_MATCHER(Float32Abs)
IS_UNOP_MATCHER(Float64Abs)
IS_UNOP_MATCHER(Float64Sqrt)
IS_UNOP_MATCHER(Float64RoundDown)
IS_UNOP_MATCHER(Float64RoundTruncate)
IS_UNOP_MATCHER(Float64RoundTiesAway)
IS_UNOP_MATCHER(Float64ExtractLowWord32)
IS_UNOP_MATCHER(Float64ExtractHighWord32)
IS_UNOP_MATCHER(NumberToInt32)
IS_UNOP_MATCHER(NumberToUint32)
IS_UNOP_MATCHER(ObjectIsSmi)
IS_UNOP_MATCHER(ObjectIsNonNegativeSmi)
IS_UNOP_MATCHER(Word32Clz)
#undef IS_UNOP_MATCHER

}  // namespace compiler
}  // namespace internal
}  // namespace v8

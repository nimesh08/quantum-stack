// spinor/dialect/lib/Verify.cpp
//
// Structural verification (M1). Different layer from M3's W1-W6
// semantic verifier: this layer only checks the IR is internally
// consistent — operand types, result uniqueness, attribute presence,
// the target attribute being set.
//
// Spec: M1_dialect.md §5 (algorithms).

#include "spinor/dialect/Spinor.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <variant>

namespace spinor::dialect {

namespace {

struct Sig {
  int qOperands;        // -1 => variable
  int qResults;
  bool producesBit;
  std::vector<std::string> requiredAttrs;
};

Sig sigFor(OpKind k) {
  switch (k) {
    case OpKind::AllocQubit: return {0, 1, false, {}};
    case OpKind::AllocBit:   return {0, 0, true, {}};
    case OpKind::Measure:    return {1, 0, true, {}};
    case OpKind::Reset:      return {1, 1, false, {}};
    case OpKind::Barrier:    return {-1, 0, false, {}};
    case OpKind::Rx:
    case OpKind::Ry:
    case OpKind::Rz:
    case OpKind::Gpi:
    case OpKind::Gpi2:
      return {1, 1, false, {"angle"}};
    case OpKind::Rzz:
      return {2, 2, false, {"angle"}};
    case OpKind::U1q:
      return {1, 1, false, {"theta", "phi"}};
    default:
      return {qubitArity(k), qubitArity(k), false, {}};
  }
}

bool hasAttr(const Op& op, std::string_view name) {
  for (const auto& a : op.attributes) {
    if (a.name == name) return true;
  }
  return false;
}

}  // namespace

void verify(const Module& m, Diagnostics& diag) {
  if (m.targetAttr.empty()) {
    diag.error("module attribute 'target' is empty (must be 'generic' or a device id)");
  }

  std::set<std::uint32_t> defined;
  std::set<std::uint32_t> resultsSeen;

  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    OpId id{i};
    const Op& op = m.op(id);
    Sig s = sigFor(op.kind);

    // operand count
    if (s.qOperands >= 0 &&
        static_cast<int>(op.operands.size()) != s.qOperands) {
      diag.error(std::string(opMnemonic(op.kind)) + " expects " +
                     std::to_string(s.qOperands) +
                     " operand(s), got " +
                     std::to_string(op.operands.size()),
                 op.loc, id);
    }

    // every operand must be defined and a Qubit
    for (auto v : op.operands) {
      if (v.v >= m.numValues()) {
        diag.error("operand " + std::to_string(v.v) +
                       " is out of range for op " +
                       std::string(opMnemonic(op.kind)),
                   op.loc, id);
        continue;
      }
      if (!defined.contains(v.v)) {
        diag.error("operand %v" + std::to_string(v.v) +
                       " is used before it is defined",
                   op.loc, id);
      }
      if (m.typeOf(v) != qubitType()) {
        diag.error(std::string(opMnemonic(op.kind)) +
                       " expects qubit operand, got " +
                       std::string(typeName(m.typeOf(v))),
                   op.loc, id);
      }
    }

    // result count and types
    int expectedResults = s.qResults + (s.producesBit ? 1 : 0);
    if (static_cast<int>(op.results.size()) != expectedResults) {
      diag.error(std::string(opMnemonic(op.kind)) + " expects " +
                     std::to_string(expectedResults) +
                     " result(s), got " +
                     std::to_string(op.results.size()),
                 op.loc, id);
    } else {
      for (int j = 0; j < s.qResults; ++j) {
        if (m.typeOf(op.results[j]) != qubitType()) {
          diag.error(std::string(opMnemonic(op.kind)) +
                         " result " + std::to_string(j) +
                         " must be a qubit",
                     op.loc, id);
        }
      }
      if (s.producesBit) {
        if (m.typeOf(op.results.back()) != bitType()) {
          diag.error(std::string(opMnemonic(op.kind)) +
                         " last result must be a bit",
                     op.loc, id);
        }
      }
    }

    // unique result IDs (no aliasing)
    for (auto v : op.results) {
      if (!resultsSeen.insert(v.v).second) {
        diag.error("duplicate result id %v" + std::to_string(v.v),
                   op.loc, id);
      }
      defined.insert(v.v);
    }

    // required attributes
    for (const auto& key : s.requiredAttrs) {
      if (!hasAttr(op, key)) {
        diag.error(std::string(opMnemonic(op.kind)) +
                       " is missing required attribute '" + key + "'",
                   op.loc, id);
      }
    }
  }
}

}  // namespace spinor::dialect

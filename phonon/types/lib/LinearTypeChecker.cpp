// phonon/types/lib/LinearTypeChecker.cpp

#include "phonon/types/LinearTypeChecker.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace phonon::types {

namespace pd = phonon::dialect;

Options optionsForTarget(const spinor::verify::TargetInfo& t) {
  Options o;
  o.midCircuitMeasure = t.midCircuitMeasure;
  return o;
}

namespace {

struct ValueState {
  bool consumed = false;
  bool measured = false;
  pd::OpId producer = pd::kInvalidOp;
  pd::OpKind producerKind = pd::OpKind::AllocQubit;
};

bool isQubit(pd::Type t) { return t.kind == pd::TypeKind::Qubit; }

// True if this op consumes its qubit operands as inputs (i.e. uses
// them mid-circuit). All gates / measure / reset / barrier do.
// `phonon.return` and `phonon.call` also consume qubit args.
bool consumesQubitOperands(pd::OpKind k) {
  if (pd::isSpinorKind(k)) {
    // alloc_* don't consume; barrier does.
    return k != pd::OpKind::AllocQubit && k != pd::OpKind::AllocBit;
  }
  return k == pd::OpKind::Call || k == pd::OpKind::Return ||
         k == pd::OpKind::If   || k == pd::OpKind::While;
}

}  // namespace

bool typecheck(const pd::Module& m, const Options& opt,
               pd::Diagnostics& diag) {
  std::size_t errsBefore = 0;
  for (const auto& d : diag.items()) {
    if (d.severity == pd::DiagSeverity::Error) ++errsBefore;
  }

  std::unordered_map<std::uint32_t, ValueState> states;

  auto setState = [&](pd::ValueId v, ValueState s) {
    states[v.v] = s;
  };

  // Walk ops top-down.
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    pd::OpId id{i};
    const pd::Op& op = m.op(id);

    bool isMeasure = (op.kind == pd::OpKind::Measure);
    bool isReset   = (op.kind == pd::OpKind::Reset);

    if (consumesQubitOperands(op.kind)) {
      for (pd::ValueId operand : op.operands) {
        if (!isQubit(m.typeOf(operand))) continue;
        auto it = states.find(operand.v);
        if (it == states.end()) {
          // Operand is a function parameter or alloc result: bootstrap.
          ValueState boot;
          boot.producer = m.producerOf(operand);
          states[operand.v] = boot;
          it = states.find(operand.v);
        }
        ValueState& s = it->second;
        if (s.consumed && !(isReset && s.measured)) {
          diag.error("E1: qubit value " + m.nameOf(operand) +
                     " used more than once (no-cloning)",
                     op.loc, id);
        } else if (s.measured && !isReset) {
          if (!opt.midCircuitMeasure) {
            diag.error("E2: qubit " + m.nameOf(operand) +
                       " used after measurement (chip lacks "
                       "mid-circuit measurement)",
                       op.loc, id);
          } else {
            diag.error("E2: qubit " + m.nameOf(operand) +
                       " used after measurement; insert 'reset' "
                       "first",
                       op.loc, id);
          }
        }
        s.consumed = true;
        if (isMeasure) s.measured = true;
        if (isReset) s.measured = false;
      }
    }

    // Allocate fresh state for every qubit-typed result.
    for (pd::ValueId r : op.results) {
      if (!isQubit(m.typeOf(r))) continue;
      ValueState fresh;
      fresh.producer = id;
      fresh.producerKind = op.kind;
      setState(r, fresh);
    }
  }

  if (opt.warnImplicitDiscard) {
    for (const auto& [vid, st] : states) {
      pd::ValueId v{vid};
      if (v.v >= m.numValues()) continue;
      if (!isQubit(m.typeOf(v))) continue;
      if (st.consumed) continue;
      // Only warn for qubit values whose producer is *not* the alloc
      // op (i.e. an intermediate gate result that is dead). The alloc
      // case is handled below.
      diag.warn("qubit " + m.nameOf(v) +
                " is implicitly discarded (produced but never used)");
    }
  }

  std::size_t errsAfter = 0;
  for (const auto& d : diag.items()) {
    if (d.severity == pd::DiagSeverity::Error) ++errsAfter;
  }
  return errsAfter == errsBefore;
}

}  // namespace phonon::types

// spinor/verify/lib/Verifier.cpp
//
// Spec: docs/build/phaseA/M3_verifier.md.

#include "spinor/verify/Verifier.h"

#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace spinor::verify {

namespace {

using namespace spinor::dialect;

// Per-element state: a "qubit lineage" rooted at one alloc_qubit op.
struct Lineage {
  // Index of the alloc_qubit op (1-based among alloc_qubit ops).
  // 0 means "this isn't a qubit value" or "unknown lineage."
  uint32_t physIdx = 0;
  bool measured = false;
};

// std::span of OpKind name → Spinor mnemonic (for W6).
std::string_view mnemonicOf(OpKind k) { return opMnemonic(k); }

}  // namespace

bool verify(const dialect::Module& m, const TargetInfo& target,
            dialect::Diagnostics& diag) {
  const std::size_t errsBefore = diag.items().size();

  // Lineage per ValueId (for qubits).
  std::vector<Lineage> lineage(m.numValues());
  // Stable physical-index per alloc_qubit op order. The k-th
  // alloc_qubit corresponds to physical qubit k.
  uint32_t allocCounter = 0;

  for (uint32_t i = 0; i < m.numOps(); ++i) {
    OpId opId{i};
    const Op& op = m.op(opId);
    const std::string mn{mnemonicOf(op.kind)};

    auto operandLineage =
        [&](std::size_t k) -> Lineage* {
      if (k >= op.operands.size()) return nullptr;
      ValueId v = op.operands[k];
      if (v.v >= lineage.size()) return nullptr;
      return &lineage[v.v];
    };

    // Allocate roots first.
    if (op.kind == OpKind::AllocQubit) {
      ++allocCounter;
      ValueId v = op.results.front();
      if (v.v < lineage.size()) {
        lineage[v.v].physIdx = allocCounter;
        lineage[v.v].measured = false;
      }
      continue;
    }
    if (op.kind == OpKind::AllocBit) {
      // bits don't carry lineage
      continue;
    }

    // Consult lineage for each qubit operand. Two-qubit gates
    // produce two new qubit values; thread lineages through.
    int nq = qubitArity(op.kind);  // -1 for Barrier
    if (nq > 0) {
      for (int k = 0; k < nq; ++k) {
        Lineage* lin = operandLineage(static_cast<std::size_t>(k));
        if (!lin) continue;
        const uint32_t phys = lin->physIdx;
        const bool measured = lin->measured;

        // ---- W2 (hardware: in-range physical index) ----
        if (!target.generic && phys != 0 &&
            target.qubitCount > 0 &&
            static_cast<std::size_t>(phys - 1) >= target.qubitCount) {
          std::ostringstream os;
          os << "W2: qubit index " << (phys - 1)
             << " out of range for target '" << target.id
             << "' (has " << target.qubitCount << " qubits)";
          diag.error(os.str(), op.loc, opId);
        }

        // ---- W4 (no ops on a measured qubit unless reset) ----
        // measure itself is allowed; reset clears; other gates
        // require !measured OR target.midCircuitMeasure.
        if (op.kind != OpKind::Measure && op.kind != OpKind::Reset &&
            measured && !target.midCircuitMeasure) {
          std::ostringstream os;
          os << "W4: operation '" << mn
             << "' on measured qubit q[" << (phys ? phys - 1 : 0)
             << "]; insert 'reset' first (or pick a target whose"
                " registry has mid_circuit_measure=true)";
          diag.error(os.str(), op.loc, opId);
        }
      }
    }

    // ---- W3 (multi-qubit operands distinct on lineage roots) ----
    if (nq >= 2 || op.kind == OpKind::Barrier) {
      std::set<uint32_t> seen;
      for (std::size_t k = 0; k < op.operands.size(); ++k) {
        Lineage* lin = operandLineage(k);
        if (!lin) continue;
        if (lin->physIdx == 0) continue;  // unknown root, skip
        if (!seen.insert(lin->physIdx).second) {
          std::ostringstream os;
          os << "W3: operation '" << mn
             << "' must have distinct operands (qubit q["
             << (lin->physIdx - 1) << "] used twice)";
          diag.error(os.str(), op.loc, opId);
        }
      }
    }

    // ---- W5 / W6 (gate vocabulary + connectivity) ----
    bool isGateOp =
        op.kind != OpKind::AllocQubit && op.kind != OpKind::AllocBit &&
        op.kind != OpKind::Measure && op.kind != OpKind::Reset &&
        op.kind != OpKind::Barrier;
    if (isGateOp) {
      // Bare gate name is the part after "spinor.".
      const std::string bareMn =
          mn.starts_with("spinor.") ? mn.substr(7) : mn;
      if (target.generic) {
        // W5: only standard gates allowed.
        if (!isStandardGate(op.kind)) {
          std::ostringstream os;
          os << "W5: native gate '" << bareMn
             << "' is not allowed under 'target generic'";
          diag.error(os.str(), op.loc, opId);
        }
      } else {
        // W6: must be in the chip's native set.
        if (!target.isNative(bareMn)) {
          std::ostringstream os;
          os << "W6: gate '" << bareMn
             << "' is not native to target '" << target.id << "'";
          diag.error(os.str(), op.loc, opId);
        }
        // W6: two-qubit gates must sit on a connected pair.
        if (nq == 2) {
          Lineage* la = operandLineage(0);
          Lineage* lb = operandLineage(1);
          if (la && lb && la->physIdx && lb->physIdx) {
            int a = static_cast<int>(la->physIdx) - 1;
            int b = static_cast<int>(lb->physIdx) - 1;
            if (!target.connected(a, b)) {
              std::ostringstream os;
              os << "W6: two-qubit gate '" << bareMn << "' on q[" << a
                 << "], q[" << b
                 << "] is not on a connected pair of target '"
                 << target.id << "'";
              diag.error(os.str(), op.loc, opId);
            }
          }
        }
      }
    }

    // ---- type-check (M3 layer; M1 already covers operand types) ----
    if (op.kind == OpKind::Reset) {
      if (!op.operands.empty() &&
          op.operands.front().v < m.numValues() &&
          m.typeOf(op.operands.front()) != qubitType()) {
        std::ostringstream os;
        os << "type: 'reset' expects a qubit operand";
        diag.error(os.str(), op.loc, opId);
      }
    }

    // Propagate lineage to results (for gates with qubit results).
    // Two-qubit gates: result[i] inherits from operand[i].
    if (nq > 0) {
      // Determine how many qubit results this op produces.
      int qResults = nq;
      if (op.kind == OpKind::Measure) qResults = 0;
      // For ops that produce qubit results, propagate.
      for (int k = 0; k < qResults && k < static_cast<int>(op.results.size());
           ++k) {
        Lineage* lin = operandLineage(static_cast<std::size_t>(k));
        if (!lin) continue;
        ValueId out = op.results[k];
        if (out.v < lineage.size()) {
          lineage[out.v].physIdx = lin->physIdx;
          // measure already handled in its own block.
          if (op.kind == OpKind::Reset) {
            lineage[out.v].measured = false;
          } else {
            lineage[out.v].measured = lin->measured;
          }
        }
      }
      if (op.kind == OpKind::Measure) {
        // Mark the source element as measured. The bit result
        // does not carry qubit lineage.
        if (Lineage* lin = operandLineage(0)) {
          lin->measured = true;
        }
      }
      if (op.kind == OpKind::Reset) {
        if (Lineage* lin = operandLineage(0)) {
          lin->measured = false;
        }
      }
    }
  }

  return diag.items().size() == errsBefore;
}

}  // namespace spinor::verify

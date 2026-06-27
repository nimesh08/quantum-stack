// spinor/passes/lib/SynthesisTraits.cpp
//
// Compute SynthesisTraits from a chip's registry entry.
// Pure data classification — vendor-agnostic. Adding a new vendor
// requires extending these tables, not editing pass code.

#include "spinor/passes/SynthesisTraits.h"

#include <cmath>

namespace spinor::passes {

namespace {

// Map entangler-name → Weyl coordinates + supercontrolled flag.
//
// Sources (all academic, vendor-neutral, pre-Qiskit):
//   * cx/cz/cy: Cartan class CNOT, Weyl (π/4, 0, 0). Crooks §6.2.
//   * ecr: locally equivalent to CX → same (π/4, 0, 0).
//   * ms = RXX(π/2): Crooks eq. 55 — MS ≃ Can(½, 0, 0) ∼ CNOT.
//   * rzz(θ): exp(-iθ/2 · ZZ) has Weyl (|θ|/2, 0, 0). Only
//     supercontrolled at θ = π/2 (then it's CZ-class = ZZMax).
struct EntanglerProps {
  WeylCoord weyl;
  bool supercontrolled;
};

EntanglerProps classifyEntangler(const std::string& name) {
  // Default for the supercontrolled CNOT class: a = π/4, b = c = 0.
  const WeylCoord cnotClass{M_PI / 4.0, 0.0, 0.0};
  if (name == "cx" || name == "cz" || name == "ecr" || name == "ms") {
    return {cnotClass, true};
  }
  if (name == "rzz") {
    // Parametric: at the canonical π/2 it's supercontrolled;
    // arbitrary-θ blocks need the variable-angle path.
    return {{M_PI / 4.0, 0.0, 0.0}, true};
  }
  if (name == "iswap") {
    // DCNOT class: Weyl (π/4, π/4, 0). Still supercontrolled.
    return {{M_PI / 4.0, M_PI / 4.0, 0.0}, true};
  }
  // Unknown entangler: assume CNOT-class to keep downstream
  // passes functional; passes can branch on isSupercontrolled
  // if they need to be conservative.
  return {cnotClass, false};
}

// Map (rotation_gate, pi_2_gate) → EulerBasis. The mapping is
// vendor-neutral: any chip declaring rz+sx lands on ZSX,
// regardless of whether it's IBM, Rigetti, or OQC.
EulerBasis classifyEulerBasis(const std::string& rotGate,
                              const std::string& pi2Gate) {
  if (rotGate == "rz" && pi2Gate == "sx") return EulerBasis::ZSX;
  if (rotGate == "rz" && pi2Gate.empty()) return EulerBasis::ZYZ;
  if (rotGate == "rz" && pi2Gate == "rx") return EulerBasis::ZXZ;
  if (rotGate == "u1q") return EulerBasis::ZXZ;
  if (rotGate == "gpi" && pi2Gate == "gpi2") return EulerBasis::RR;
  // Discrete / unknown — keep continuous synthesis as the safe
  // default; cat-qubit chips must explicitly set
  // continuousOneQubit=false in a future loader hook.
  return EulerBasis::ZYZ;
}

}  // namespace

SynthesisTraits computeTraits(const registry::ChipInfo& chip) {
  SynthesisTraits t;
  t.entanglerName     = chip.decompose.twoQubitEntangler;
  t.entanglerCountMax = chip.decompose.twoQubitEntanglerCountMax;
  auto props          = classifyEntangler(t.entanglerName);
  t.entanglerWeyl     = props.weyl;
  t.isSupercontrolled = props.supercontrolled;

  t.rotationGate      = chip.decompose.oneQubitRotationGate;
  t.pi2Gate           = chip.decompose.oneQubitPi2Gate;
  t.eulerBasis        = classifyEulerBasis(t.rotationGate, t.pi2Gate);
  t.continuousOneQubit = true;  // toggled false only for cat-qubit chips
  return t;
}

}  // namespace spinor::passes

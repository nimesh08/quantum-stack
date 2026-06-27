// spinor/passes/include/spinor/passes/SynthesisTraits.h
//
// SynthesisTraits — per-chip synthesis metadata derived from the
// YAML registry. Every optimization pass that needs to know about
// the target's gate set takes a `const SynthesisTraits&`. This is
// the single abstraction that keeps the passes vendor-modular:
// adding a new vendor adds a row to `computeTraits`, not a branch
// inside each pass.
//
// Mapping (from the existing YAML strings):
//   IBM Heron r2/r3, Nighthawk: entangler="cz",  eulerBasis=ZSX
//   IBM Eagle (retired):        entangler="ecr", eulerBasis=ZSX
//   IonQ Aria/Forte, AQT:       entangler="ms",  eulerBasis=RR
//   Quantinuum H1/H2/Helios:    entangler="rzz", eulerBasis=ZXZ
//                               (parametric — non-supercontrolled)
//   Rigetti/IQM/OQC/QCI/Anyon/TII: entangler="cz", eulerBasis=ZXZ
//   Alice & Bob Boson 4:        entangler="cx",  eulerBasis=Discrete
//
// Source for the math:
//   Tucci 1999 quant-ph/9902062, Khaneja-Glaser 2001,
//   Kraus-Cirac 2001, Vidal-Dawson 2004, Vatan-Williams 2004,
//   Shende-Markov-Bullock 2004. Same Cartan/Weyl theorem every
//   major compiler (Qiskit, Cirq, Pytket, BQSKit) implements.

#pragma once

#include "spinor/registry/Registry.h"

#include <string>

namespace spinor::passes {

// Cartan/Weyl coordinates of a 2-qubit unitary basis gate.
// A basis is "supercontrolled" iff (a ≈ π/4, c ≈ 0).
// CX, CZ, ECR, MS, ZZMax all satisfy this.
struct WeylCoord {
  double a = 0.0;
  double b = 0.0;
  double c = 0.0;
};

// One-qubit Euler basis names that Qiskit's
// OneQubitEulerDecomposer accepts. The Spinor pass implementations
// will reuse the same closed-form expressions.
enum class EulerBasis {
  ZSX,       // IBM Heron/Nighthawk/Eagle: {Rz, Sx} (and X = SxSx).
  ZXZ,       // Rigetti/IQM/OQC/QCI/Anyon/TII: {Rz, Rx}.
  XZX,       // Alternate XZX flavour for the same {Rz, Rx} basis.
  ZYZ,       // Generic Euler-ZYZ with {Rz, Ry}.
  RR,        // IonQ/AQT: R(θ, φ) ≡ GPi/GPi2 closed form.
  Discrete,  // Alice & Bob Boson 4: only Clifford+T realisable.
};

// SynthesisTraits is the vendor-parameter bundle threaded through
// every optimization pass. Populated once per chip at registry
// load time (or lazily on first access).
struct SynthesisTraits {
  // 2-qubit (KAK basis-gate) section.
  std::string entanglerName;     // "cz" | "ecr" | "ms" | "rzz" | "cx"
  WeylCoord   entanglerWeyl;     // (π/4, 0, 0) for cx/cz/ecr/ms;
                                 // (θ/2, 0, 0) for rzz(θ).
  bool        isSupercontrolled = false;  // a ≈ π/4 && c ≈ 0.
  int         entanglerCountMax = 3;      // worst-case 2Q uses.

  // 1-qubit Euler section.
  EulerBasis  eulerBasis = EulerBasis::ZSX;
  std::string rotationGate;      // "rz" | "u1q" | "gpi"
  std::string pi2Gate;           // "sx" | "gpi2" | ""
  bool        continuousOneQubit = true;  // false for cat-qubit etc.
};

// Derive SynthesisTraits from a chip's registry entry. Pure
// function — no hidden vendor branches. Reads
// chip.decompose.twoQubitEntangler and oneQubit{RotationGate,Pi2Gate}
// to populate the struct. Unknown entanglers default to a
// best-effort (a=π/4, c=0) supercontrolled assumption; downstream
// passes are free to handle unsupercontrolled cases (e.g. RZZ(θ))
// via the isSupercontrolled flag.
SynthesisTraits computeTraits(const registry::ChipInfo& chip);

}  // namespace spinor::passes

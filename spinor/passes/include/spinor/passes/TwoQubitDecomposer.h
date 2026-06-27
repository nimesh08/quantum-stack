// spinor/passes/include/spinor/passes/TwoQubitDecomposer.h
//
// TwoQubitDecomposer — general SU(4) Cartan/KAK decomposer
// parameterized on the chip's entangler (via SynthesisTraits).
// Mirrors Qiskit's TwoQubitBasisDecomposer; same math from
// Tucci 1999, Kraus-Cirac 2001, Vidal-Dawson 2004,
// Vatan-Williams 2004, Shende-Markov-Bullock 2004.
//
// Phase A: header carries the API surface; the closed-form math
// (decomp0/1/2/3 from the cited papers) will land in a follow-up.
// Until then, KakResynthesis is a no-op and the per-input-gate
// recipes in Decomposition.cpp::emitCX carry the load.

#pragma once

#include "spinor/passes/SynthesisTraits.h"

#include <array>

namespace spinor::passes {

// A 4×4 complex unitary on (q_c, q_t). Row-major.
using U4 = std::array<std::array<std::pair<double, double>, 4>, 4>;

// Result of one KAK decomposition: a sequence of native ops on
// the two qubits, alternating 1Q layers (six values: αc, βc, γc,
// αt, βt, γt) and zero, one, two, or three entanglers.
struct KakResult {
  int entanglerUses = 0;  // 0 | 1 | 2 | 3
  // Phase A: we don't yet capture the explicit 1Q angles here —
  // the API is reserved. Downstream passes will read whichever
  // representation we settle on (likely a small vector of ops).
};

class TwoQubitDecomposer {
 public:
  // Decompose `u` into at most `traits.entanglerCountMax` uses
  // of the chip's entangler plus surrounding 1Q rotations.
  // Phase A: returns {0} unconditionally (placeholder).
  KakResult decompose(const U4& u, const SynthesisTraits& traits) const;
};

}  // namespace spinor::passes

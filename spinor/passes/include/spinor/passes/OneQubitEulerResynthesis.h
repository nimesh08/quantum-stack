// spinor/passes/include/spinor/passes/OneQubitEulerResynthesis.h
//
// OneQubitEulerResynthesis — collapse each 1Q run to its 2×2
// matrix and re-emit minimum-length in traits.eulerBasis.
// Vendor-modular: reads traits.eulerBasis (ZSX|ZXZ|XZX|ZYZ|RR).
// Mirrors Qiskit's Optimize1qGatesDecomposition.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/SynthesisTraits.h"

namespace spinor::passes {

class OneQubitEulerResynthesis {
 public:
  // Phase A: pass-through. The existing emitZYZ recipes in
  // Decomposition.cpp already emit native 1Q ops; full block-
  // composition + Euler-basis-aware re-emission lands later.
  dialect::Module run(const dialect::Module& m,
                      const SynthesisTraits& traits) const;
};

}  // namespace spinor::passes

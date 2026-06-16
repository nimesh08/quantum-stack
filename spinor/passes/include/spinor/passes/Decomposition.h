// spinor/passes/include/spinor/passes/Decomposition.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

namespace spinor::passes {

class Decomposition {
 public:
  // Lower every standard gate in `m` into the chip's native gate
  // set per the registry-driven recipes. Single-qubit lowering is
  // Euler-ZYZ; two-qubit lowering uses closed-form per-entangler
  // recipes (M6 spec §5).
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip,
                      dialect::Diagnostics& diag) const;
};

class Cleanup {
 public:
  // Local peephole: merge adjacent rz/rz; annihilate sx/sxdg
  // pairs; drop rz(0) modulo 2π. Idempotent. Does not cross
  // measure/reset/barrier ops.
  dialect::Module run(const dialect::Module& m) const;
};

}  // namespace spinor::passes

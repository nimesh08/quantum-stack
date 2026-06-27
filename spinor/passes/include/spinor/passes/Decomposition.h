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

}  // namespace spinor::passes

// Cleanup is declared in Cleanup.h. Re-include here for
// backward-compatibility with call sites that only included
// Decomposition.h.
#include "spinor/passes/Cleanup.h"

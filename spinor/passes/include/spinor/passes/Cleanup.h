// spinor/passes/include/spinor/passes/Cleanup.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

namespace spinor::passes {

// Exact native peephole cancellation at O1 and above. Matrix products detect
// inverse pairs across the supported one- and two-qubit bases, including
// GPI/GPI2 and U1q, and retain their scalar phase. Measurement, reset, barriers
// and control flow remain fences. Euler resynthesis handles rotation merging.
class Cleanup {
 public:
  // Target-aware overload retained for the shared pass pipeline.
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip) const;

  // Target-independent matrix cancellation uses the same exact rules.
  dialect::Module run(const dialect::Module& m) const;
};

}  // namespace spinor::passes

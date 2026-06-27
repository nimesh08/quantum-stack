// spinor/passes/include/spinor/passes/VF2PostLayout.h
//
// VF2PostLayout — after routing succeeded, attempt a subgraph
// isomorphism into a higher-fidelity sub-graph using chip
// calibration data as the tiebreaker. Vendor-agnostic — uses
// only coupling map and per-edge error rates from ChipInfo.
//
// Phase A: pass-through scaffold; the VF2 search lands later.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

namespace spinor::passes {

class VF2PostLayout {
 public:
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip) const;
};

}  // namespace spinor::passes

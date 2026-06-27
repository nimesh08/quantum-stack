// spinor/passes/include/spinor/passes/CommutativeCancellation.h
//
// CommutativeCancellation — cancel inverse-pair gates separated
// only by gates that commute with them. Vendor-modular via a
// commutation-rule table seeded from SynthesisTraits.
//
// Phase A: scaffolded. Full rule table + analysis pass lands as
// a follow-up; this header reserves the API so PassManager wires
// can be made stable.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/SynthesisTraits.h"

namespace spinor::passes {

class CommutativeCancellation {
 public:
  dialect::Module run(const dialect::Module& m,
                      const SynthesisTraits& traits) const;
};

}  // namespace spinor::passes

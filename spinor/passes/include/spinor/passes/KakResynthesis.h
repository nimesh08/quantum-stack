// spinor/passes/include/spinor/passes/KakResynthesis.h
//
// KakResynthesis — for each TwoQBlock, compose to 4×4, invoke
// TwoQubitDecomposer, and re-emit in native ops. Vendor-modular
// via SynthesisTraits.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/Collect2qBlocks.h"
#include "spinor/passes/SynthesisTraits.h"

namespace spinor::passes {

class KakResynthesis {
 public:
  // Phase A: pass-through. The per-input-gate emitCX recipes
  // already produce native-basis output; turning on block-level
  // resynthesis is the goal of the follow-up phase, where this
  // pass will replace each block with KAK-optimal native ops.
  dialect::Module run(const dialect::Module& m,
                      const std::vector<TwoQBlock>& blocks,
                      const SynthesisTraits& traits) const;
};

}  // namespace spinor::passes

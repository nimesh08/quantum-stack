#pragma once

#include "spinor/passes/ConsolidateBlocks.h"

namespace spinor::passes {

class KakResynthesis {
 public:
  // Replace a block only after exact matrix verification and a strict
  // improvement in (native entangler count, total native operation count),
  // without increasing either count.
  dialect::Module run(const dialect::Module& m,
                      const std::vector<ConsolidatedBlock>& blocks,
                      const SynthesisTraits& traits,
                      const registry::ChipInfo* chip = nullptr) const;
  dialect::Module run(const dialect::Module& m,
                      const std::vector<TwoQBlock>& blocks,
                      const SynthesisTraits& traits,
                      const registry::ChipInfo* chip = nullptr) const;
};

}  // namespace spinor::passes

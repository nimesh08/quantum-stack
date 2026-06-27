// spinor/passes/lib/KakResynthesis.cpp

#include "spinor/passes/KakResynthesis.h"

namespace spinor::passes {

dialect::Module KakResynthesis::run(const dialect::Module& m,
                                    const std::vector<TwoQBlock>& blocks,
                                    const SynthesisTraits& traits) const {
  (void)blocks;
  (void)traits;
  return m;
}

}  // namespace spinor::passes

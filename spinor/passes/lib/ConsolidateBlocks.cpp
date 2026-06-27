// spinor/passes/lib/ConsolidateBlocks.cpp

#include "spinor/passes/ConsolidateBlocks.h"

namespace spinor::passes {

dialect::Module ConsolidateBlocks::run(const dialect::Module& m,
                                       const std::vector<TwoQBlock>& blocks) const {
  // Phase A: scaffolded — defer to a future patch that introduces
  // a UnitaryGate IR op carrying a 4×4 matrix. For now we return
  // the input unchanged; KakResynthesis will then be a no-op too
  // and the per-input-gate emitCX recipes (Step 5) carry the
  // load. This keeps O2/O3 stable while the SU(4) KAK arrives.
  (void)blocks;
  return m;
}

}  // namespace spinor::passes

#pragma once

#include "spinor/passes/Collect2qBlocks.h"
#include "spinor/passes/TwoQubitDecomposer.h"

namespace spinor::passes {

struct ConsolidatedBlock {
  TwoQBlock block;
  U4 unitary;
  std::size_t entanglerUses = 0;
};

class ConsolidateBlocks {
 public:
  // Compose each block in program order, retaining original operation IDs.
  // The side table avoids introducing an opaque unitary into executable IR.
  // Reject malformed, overlapping, or nonunitary blocks.
  std::vector<ConsolidatedBlock> run(const dialect::Module& m,
                                    const std::vector<TwoQBlock>& blocks) const;
};

}  // namespace spinor::passes

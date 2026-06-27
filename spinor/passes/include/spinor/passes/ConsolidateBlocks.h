// spinor/passes/include/spinor/passes/ConsolidateBlocks.h
//
// ConsolidateBlocks — replace each TwoQBlock from Collect2qBlocks
// with a single UnitaryGate carrying the composed 4×4 matrix.
// Vendor-agnostic. Mirrors Qiskit's ConsolidateBlocks pass.
//
// Phase A: scaffolded as identity (no consolidation). The block
// list is computed but not folded — KakResynthesis (next pass)
// is the one that ingests blocks and re-emits in native basis.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/Collect2qBlocks.h"

namespace spinor::passes {

class ConsolidateBlocks {
 public:
  // Replace each block with a virtual unitary op (Phase A: no-op,
  // returns the input module unchanged).
  dialect::Module run(const dialect::Module& m,
                      const std::vector<TwoQBlock>& blocks) const;
};

}  // namespace spinor::passes

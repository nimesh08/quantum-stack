// phonon/optimizer/include/phonon/optimizer/Optimizer.h
//
// Phonon's optimizer entry points. The "build whatever determines
// quality, borrow the deep math" principle: the built passes are
// here; the borrowed adapters live in BorrowedPasses.h (M6).

#pragma once

#include "phonon/dialect/Phonon.h"

#include <cstddef>

namespace phonon::optimizer {

struct Stats {
  std::size_t gatesRemoved = 0;
  std::size_t rotationsMerged = 0;
  std::size_t reorderingsApplied = 0;
  std::size_t depthBefore = 0;
  std::size_t depthAfter = 0;
};

// Built-only pipeline (M5). Useful in tests; the full pipeline
// composing built and borrowed passes is in M7.
Stats runBuiltPipeline(dialect::Module& m);

// Per-pass entry points (also useful directly in tests).
Stats cancelInversePairs(dialect::Module& m);
Stats mergeRotations(dialect::Module& m);
Stats commuteAndReduce(dialect::Module& m);
Stats schedule(dialect::Module& m);

}  // namespace phonon::optimizer

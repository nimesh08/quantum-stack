// phonon/optimizer/include/phonon/optimizer/Pipeline.h
//
// The optimize-once pipeline. Composes the M5 built passes and
// the M6 borrowed adapters into a single entry point.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/BorrowedPasses.h"
#include "phonon/optimizer/Optimizer.h"

#include <memory>

namespace phonon::optimizer {

struct PipelineConfig {
  std::unique_ptr<ISynthesizer>  synth = nullptr;  // null → makeNullSynthesizer
  std::unique_ptr<IZxSimplifier> zx    = nullptr;  // null → makeNullZxSimplifier
  // Re-run the built passes after borrowed passes to clean up
  // any patterns the synthesis / ZX simplifier may have
  // exposed. Default: true.
  bool runBuiltAfterBorrowed = true;
};

// Run the full pipeline on `m`. Returns aggregated stats.
Stats runPipeline(dialect::Module& m, PipelineConfig cfg = {});

}  // namespace phonon::optimizer

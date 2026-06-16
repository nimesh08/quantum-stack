// phonon/lower/include/phonon/lower/Lowering.h
//
// Lower a Phonon module to a flat Spinor module.
// See docs/build/phaseB/M4_lowering.md.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "spinor/dialect/Spinor.h"
#include "spinor/verify/TargetInfo.h"

#include <optional>

namespace phonon::lower {

struct Result {
  std::optional<spinor::dialect::Module> module;
  spinor::dialect::Diagnostics diag;
};

// Lower `m` to a flat Spinor module. If `target` is supplied, the
// lowering uses its capability flags (e.g. midCircuitMeasure) to
// decide how to handle conditional / measurement-after-gate cases.
Result lower(const dialect::Module& m,
             const spinor::verify::TargetInfo* target = nullptr);

}  // namespace phonon::lower

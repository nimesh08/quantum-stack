// phonon/types/include/phonon/types/LinearTypeChecker.h
//
// Phonon's linear type system: makes the no-cloning theorem a
// compile error. See docs/build/phaseB/M3_linear_types.md.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "spinor/verify/TargetInfo.h"

namespace phonon::types {

struct Options {
  // Implicit discard: a fresh qubit value that is never consumed
  // (no gate uses it; no measure / reset). Default: emit a
  // warning. Set to false to silence.
  bool warnImplicitDiscard = true;
  // Honour the W4 relaxation: when true, a `reset` after a
  // measure restores the qubit for further gate use. When false,
  // any post-measure use is rejected.
  bool midCircuitMeasure = true;
};

// Derive checker options from a Phase A TargetInfo. Reads the
// `midCircuitMeasure` capability flag.
Options optionsForTarget(const spinor::verify::TargetInfo& t);

// Run the linear type checker on `m`. Returns true iff `diag`
// gained no new errors.
bool typecheck(const dialect::Module& m,
               const Options& opt,
               dialect::Diagnostics& diag);

}  // namespace phonon::types

// spinor/verify/include/spinor/verify/Verifier.h
//
// W1-W6 + quantum/classical type-check semantic verifier.
// See docs/build/phaseA/M3_verifier.md.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/verify/TargetInfo.h"

namespace spinor::verify {

// Run the full W1-W6 + type-check pipeline on `m`. Diagnostics
// are appended to `diag`. Returns true iff `diag` has no
// new errors (existing errors in `diag` are not cleared).
bool verify(const dialect::Module& m, const TargetInfo& target,
            dialect::Diagnostics& diag);

}  // namespace spinor::verify

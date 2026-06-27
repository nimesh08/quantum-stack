// spinor/passes/include/spinor/passes/Cleanup.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

namespace spinor::passes {

// Cleanup peephole pass — the only local optimization that
// always runs after Decomposition.
//
// Rule 2 (verbatim mode): Spinor does not re-route or re-transpile;
// it just removes the redundancies that become visible once gates
// are in native form.
//
// Mergeable patterns by 1Q basis:
//   * {rz, sx}        (IBM Heron/Eagle/Nighthawk):
//       merge adjacent Rz angles; cancel Sx · Sxdg pairs.
//   * {gpi2, ...}     (IonQ Aria/Forte, AQT Pine):
//       merge adjacent Gpi2 of equal angle (TODO).
//   * {u1q, ...}      (Quantinuum H1/H2/Helios):
//       pass-through today (TODO).
//
// The pass reads chip.decompose.oneQubitRotationGate and
// oneQubitPi2Gate to decide which lane of the peephole to enable.
// Unhandled 1Q natives still emit verbatim with no merging.
class Cleanup {
 public:
  // Vendor-aware overload — preferred. Reads chip.decompose to
  // decide which 1Q peephole patterns are valid for this target.
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip) const;

  // Backward-compatible overload: assumes IBM-style {rz, sx, sxdg}
  // peephole. Kept so existing call sites compile unchanged.
  dialect::Module run(const dialect::Module& m) const;
};

}  // namespace spinor::passes

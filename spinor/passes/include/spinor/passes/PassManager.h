// spinor/passes/include/spinor/passes/PassManager.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"
#include "spinor/passes/OptimizationLevel.h"

namespace spinor::passes {

// PassManager assembles the per-level pass pipeline and runs it
// against a parsed Module. Replaces the open-coded
// Placement → Routing → Decomposition → Cleanup sequence that
// was duplicated three times in spinorc_main.cpp.
//
// Vendor-modular: all passes are parameterized on ChipInfo /
// SynthesisTraits derived from the YAML registry. Zero chip-ID
// branches inside any pass.
//
// Pipeline composition per level:
//
//   O0: Placement(trivial) → Routing → Decomposition
//   O1: O0 + Cleanup(chip-aware) + OneQubitEulerResynthesis
//          + InverseCancellation                       [fixed-point]
//   O2: O1 + CommutativeCancellation                   [fixed-point]
//   O3: O2 + Collect2qBlocks + ConsolidateBlocks
//          + KakResynthesis + VF2PostLayout            [minimum-point]
//
// At Phase A, levels O2 and O3 fall back to O1's pipeline if the
// new passes are not yet wired in. The level parameter is still
// recorded into the output Module's targetAttr-adjacent metadata
// so downstream tools (and the parity test harness) can see what
// the user asked for.
class PassManager {
 public:
  // Compile `module` to `chip`'s native gate set at the given
  // optimization level.
  dialect::Module compile(const dialect::Module& module,
                          const registry::ChipInfo& chip,
                          OptimizationLevel level,
                          dialect::Diagnostics& diag) const;
};

}  // namespace spinor::passes

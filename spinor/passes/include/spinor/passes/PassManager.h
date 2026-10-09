// spinor/passes/include/spinor/passes/PassManager.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"
#include "spinor/passes/OptimizationLevel.h"
#include "spinor/passes/CompilationReport.h"

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
//   O0: Placement → Routing → Decomposition
//   O1: O0 + Cleanup(chip-aware) + OneQubitEulerResynthesis
//          + InverseCancellation                       [fixed-point]
//   O2: O1 + commutative cancellation + exact two-qubit block synthesis
//   O3: bounded alternative placements + repeated exact block synthesis
//          + calibrated VF2 post-layout search        [minimum-point]
//
// Runtime conditional regions retain their branch fences and phase
// semantics; block resynthesis does not flatten them into a unitary.
bool validateCompiled(const dialect::Module& module, const registry::ChipInfo& chip,
                      dialect::Diagnostics& diag);
// Canonicalize native angle ranges and symmetric operand order without
// placement, routing, or changes to physical wire identities.
dialect::Module canonicalizeNative(const dialect::Module& module,
                                  const registry::ChipInfo& chip);

class PassManager {
 public:
  // Compile `module` to `chip`'s native gate set at the given
  // optimization level.
  dialect::Module compile(const dialect::Module& module,
                          const registry::ChipInfo& chip,
                          OptimizationLevel level,
                          dialect::Diagnostics& diag,
                          CompilationReport* report = nullptr) const;
  // Internal target-remapping entry: input has already been verified,
  // individually bounded and logically simplified. Target checks still run.
  dialect::Module compilePrepared(const dialect::Module& module,
                          const registry::ChipInfo& chip,
                          OptimizationLevel level,
                          dialect::Diagnostics& diag,
                          CompilationReport* report = nullptr) const;
};

}  // namespace spinor::passes

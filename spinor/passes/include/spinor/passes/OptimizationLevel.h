// spinor/passes/include/spinor/passes/OptimizationLevel.h

#pragma once

namespace spinor::passes {

// Optimization-level enum mirroring Qiskit's transpile()
// optimization_level argument (0/1/2/3).
//
// Semantics (same as IBM's official docs at
// https://quantum.cloud.ibm.com/docs/en/guides/set-optimization):
//
//   O0 — No optimization. Translation + trivial layout/routing
//        only. Used for hardware characterization.
//
//   O1 — Light optimization (default). Adds 1Q Euler resynthesis
//        + InverseCancellation. Loop to fixed point.
//
//   O2 — Medium optimization. Adds CommutativeCancellation.
//
//   O3 — Heavy optimization. Adds 2Q-block resynthesis via
//        Cartan KAK + VF2PostLayout. Loop to minimum point
//        (allows temporary worsening to find a deeper minimum).
//
// All passes invoked by each level are vendor-modular: they read
// chip metadata from SynthesisTraits (derived from the YAML),
// never branch on chip ID directly.
enum class OptimizationLevel : int {
  O0 = 0,
  O1 = 1,
  O2 = 2,
  O3 = 3,
};

// Parse a string like "0", "1", "2", "3", "O0", "O1" etc.
// Returns OptimizationLevel::O1 (the default) if input is empty
// or invalid. The caller is responsible for emitting a warning
// on invalid input if desired.
OptimizationLevel parseOptimizationLevel(const char* s);

// Canonical default — matches Qiskit's
// generate_preset_pass_manager() default (level 1).
constexpr OptimizationLevel kDefaultOptimizationLevel = OptimizationLevel::O1;

}  // namespace spinor::passes

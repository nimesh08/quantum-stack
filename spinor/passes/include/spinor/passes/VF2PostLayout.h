#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"
#include <map>

namespace spinor::passes {

// Error probabilities from one identified calibration snapshot. Missing
// values are unknown, never assumed to be perfect. Empty categories are
// omitted from the objective; a partially populated category must cover
// every operation used by both the old and proposed layouts.
struct CalibrationCosts {
  std::map<int,double> oneQubitError;
  std::map<int,double> readoutError;
  std::map<std::pair<int,int>,double> twoQubitError;
};
struct VF2Statistics {
  std::size_t visitedStates = 0;
  bool budgetExhausted = false;
  bool improved = false;
  double originalCost = 0;
  double selectedCost = 0;
};

class VF2PostLayout {
 public:
  // Bounded, deterministic subgraph monomorphism search. Only a verified
  // lower-error embedding is installed. Native gate directions and
  // classical measurement destinations are preserved. Without calibrated
  // errors the existing valid layout is retained.
  dialect::Module run(const dialect::Module& m,
                      const registry::ChipInfo& chip,
                      const CalibrationCosts& costs = {},
                      std::size_t stateBudget = 50000,
                      VF2Statistics* statistics = nullptr) const;
};

}  // namespace spinor::passes

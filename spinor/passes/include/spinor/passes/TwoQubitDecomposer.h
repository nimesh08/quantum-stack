#pragma once

#include "spinor/dialect/Circuit.h"
#include "spinor/passes/SynthesisTraits.h"
#include <array>

namespace spinor::passes {

// Row-major matrix in the |00>, |01>, |10>, |11> basis.
using U4 = std::array<std::array<std::pair<double, double>, 4>, 4>;

struct KakResult {
  int entanglerUses = 0;
  double globalPhase = 0;
  // Native operations on local wires 0 and 1. The complete matrix is
  // exp(i * globalPhase) times their time-ordered product.
  std::vector<dialect::WireOp> operations;
  // Construction metadata describes synthesis trials, not accepted optimizer
  // rewrites. Callers must keep it outside residual totals.
  std::string construction = "generic";
  bool analyticalAttempted = false;
  std::string analyticalRejection;
};

class TwoQubitDecomposer {
 public:
  // Exact magic-basis Cartan factorization followed by native synthesis.
  // At most three CX-equivalent entanglers; fewer for local/controlled
  // and two-entangler classes. Throws for nonunitary input, a numerical
  // validation failure, or a basis that cannot represent the result.
  // An optional two-wire chip supplies directed connectivity constraints.
  KakResult decompose(const U4& u, const SynthesisTraits& traits,
                      const registry::ChipInfo* twoWireChip = nullptr) const;
};

}  // namespace spinor::passes

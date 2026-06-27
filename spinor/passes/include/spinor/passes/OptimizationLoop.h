// spinor/passes/include/spinor/passes/OptimizationLoop.h
//
// OptimizationLoop — generic fixed-point / minimum-point loop
// over a body functor. *Pure pipeline scheduling — vendor-
// independent.* Identical to Qiskit's DoWhileController (fixed
// point) and MinimumPoint controller.
//
// Templated on a Criterion so the same loop body serves O1/O2
// (FixedPointCriterion) and O3 (MinimumPointCriterion).

#pragma once

#include "spinor/dialect/Spinor.h"

#include <cstddef>
#include <functional>

namespace spinor::passes {

// Cheap circuit metric used by both criteria.
struct CircuitMetric {
  std::size_t size = 0;
  std::size_t depth = 0;  // Phase A: approximated as `size`.
  bool operator==(const CircuitMetric& o) const {
    return size == o.size && depth == o.depth;
  }
};

CircuitMetric computeMetric(const dialect::Module& m);

// FixedPointCriterion: stop when (size, depth) stops changing
// for one full iteration. Identical to Qiskit's
// DoWhileController(do_while = lambda ps: not converged).
// Vendor-independent — only inspects metric values.
class FixedPointCriterion {
 public:
  bool shouldStop(const CircuitMetric& prev,
                  const CircuitMetric& cur,
                  int iter, int maxIters) const;
};

// MinimumPointCriterion: stop when no improvement after N
// consecutive iterations, OR maxIters reached. Identical to
// Qiskit's MinimumPoint controller. More aggressive than fixed-
// point — allows temporary worsening if it leads to a deeper
// minimum. Vendor-independent.
class MinimumPointCriterion {
 public:
  explicit MinimumPointCriterion(int patience = 2)
      : patience_(patience) {}
  bool shouldStop(const CircuitMetric& best,
                  const CircuitMetric& cur,
                  int iter, int maxIters);
 private:
  int patience_;
  int stagnant_ = 0;
};

// Run `body` repeatedly until `criterion.shouldStop` returns
// true. `body` is a pure function Module -> Module. Vendor-
// independent.
template <typename Criterion>
class OptimizationLoop {
 public:
  using Body = std::function<dialect::Module(const dialect::Module&)>;
  dialect::Module run(const dialect::Module& initial,
                      Body body,
                      Criterion criterion,
                      int maxIters = 10) const;
};

}  // namespace spinor::passes

// spinor/passes/lib/OptimizationLoop.cpp

#include "spinor/passes/OptimizationLoop.h"

namespace spinor::passes {

CircuitMetric computeMetric(const dialect::Module& m) {
  CircuitMetric x;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const auto& op = m.op(dialect::OpId{i});
    if (op.kind == dialect::OpKind::AllocQubit ||
        op.kind == dialect::OpKind::AllocBit) continue;
    ++x.size;
  }
  x.depth = x.size;  // approximation; depth-DAG pass lands later
  return x;
}

bool FixedPointCriterion::shouldStop(const CircuitMetric& prev,
                                     const CircuitMetric& cur,
                                     int iter, int maxIters) const {
  if (iter >= maxIters) return true;
  return iter > 0 && prev == cur;
}

bool MinimumPointCriterion::shouldStop(const CircuitMetric& best,
                                       const CircuitMetric& cur,
                                       int iter, int maxIters) {
  if (iter >= maxIters) return true;
  if (cur.size < best.size || cur.depth < best.depth) {
    stagnant_ = 0;
    return false;
  }
  ++stagnant_;
  return stagnant_ >= patience_;
}

// Explicit-instantiation friendly template body.
template <typename C>
dialect::Module OptimizationLoop<C>::run(const dialect::Module& initial,
                                         Body body,
                                         C criterion,
                                         int maxIters) const {
  dialect::Module cur = initial;
  CircuitMetric prev = computeMetric(cur);
  for (int it = 0; it < maxIters; ++it) {
    dialect::Module next = body(cur);
    CircuitMetric m = computeMetric(next);
    if (criterion.shouldStop(prev, m, it + 1, maxIters)) {
      return next;
    }
    cur = std::move(next);
    prev = m;
  }
  return cur;
}

// Explicit instantiations for the two stock criteria.
template class OptimizationLoop<FixedPointCriterion>;
template class OptimizationLoop<MinimumPointCriterion>;

}  // namespace spinor::passes

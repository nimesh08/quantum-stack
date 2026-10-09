// spinor/passes/lib/OptimizationLoop.cpp

#include "spinor/passes/OptimizationLoop.h"
#include "spinor/dialect/Circuit.h"
#include <type_traits>
#include <algorithm>

namespace spinor::passes {

CircuitMetric computeMetric(const dialect::Module& m) {
  CircuitMetric x;
  auto circuit=dialect::flatten(m);std::vector<std::size_t> depth(circuit.numQubits);
  for(const auto& op:circuit.instructions){
    ++x.size;
    if(op.qubits.empty()){
      if(op.kind==dialect::OpKind::Barrier||dialect::isControl(op.kind))std::fill(depth.begin(),depth.end(),x.depth);
      continue;
    }
    std::size_t previous=0;for(int q:op.qubits)previous=std::max(previous,depth.at(q));
    for(int q:op.qubits)depth.at(q)=previous+1;
    x.depth=std::max(x.depth,previous+1);
  }
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
  dialect::Module cur=initial,best=initial;
  auto bestMetric=computeMetric(best);
  for(int it=0;it<maxIters;++it){
    auto next=body(cur);auto metric=computeMetric(next);
    if constexpr(std::is_same_v<C,FixedPointCriterion>){
      if(dialect::print(cur)==dialect::print(next))return next;
    }else{
      bool better=metric.size<bestMetric.size||(metric.size==bestMetric.size&&metric.depth<bestMetric.depth);
      auto previousBest=bestMetric;
      if(better){best=next;bestMetric=metric;}
      if(criterion.shouldStop(previousBest,metric,it+1,maxIters))return best;
    }
    cur=std::move(next);
  }
  if constexpr(std::is_same_v<C,FixedPointCriterion>)return cur;
  else return best;
}

// Explicit instantiations for the two stock criteria.
template class OptimizationLoop<FixedPointCriterion>;
template class OptimizationLoop<MinimumPointCriterion>;

}  // namespace spinor::passes

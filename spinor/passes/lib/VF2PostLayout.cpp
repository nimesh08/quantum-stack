#include "spinor/passes/VF2PostLayout.h"
#include "spinor/dialect/Circuit.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numeric>
#include <set>
#include <tuple>

namespace spinor::passes {
dialect::Module VF2PostLayout::run(const dialect::Module& m,
    const registry::ChipInfo& chip,const CalibrationCosts& costs,
    std::size_t stateBudget,VF2Statistics* statistics) const {
  using namespace dialect;
  VF2Statistics stats;
  auto finish=[&](const Module& module){if(statistics)*statistics=stats;return module;};
  if(hasControlFlow(m)||stateBudget==0)return finish(m);
  if(costs.oneQubitError.empty()&&costs.readoutError.empty()&&costs.twoQubitError.empty())return finish(m);
  auto validateErrors=[](const auto& values){
    for(const auto& [key,error]:values)
      if(!std::isfinite(error)||error<0||error>=1)
        throw std::invalid_argument("calibration error probability must be finite and in [0, 1)");
  };
  validateErrors(costs.oneQubitError);validateErrors(costs.readoutError);validateErrors(costs.twoQubitError);
  auto circuit=flatten(m);
  if(circuit.numQubits>chip.qubits)throw std::invalid_argument("post-layout circuit exceeds target qubits");
  std::vector<std::set<int>> physical(chip.qubits);
  for(auto [a,b]:chip.coupling){
    if(a<0||b<0||std::size_t(a)>=chip.qubits||std::size_t(b)>=chip.qubits||a==b)
      throw std::invalid_argument("invalid post-layout coupling edge");
    physical[a].insert(b);physical[b].insert(a);
  }
  std::set<std::pair<int,int>> directed(chip.coupling.begin(),chip.coupling.end());
  std::vector<std::set<int>> adjacency(circuit.numQubits);
  std::vector<double> oneWeight(circuit.numQubits),readWeight(circuit.numQubits);
  struct Edge { int a,b; bool directed; std::size_t weight; };
  std::map<std::tuple<int,int,bool>,std::size_t> weights;
  std::set<int> used;
  for(const auto& op:circuit.instructions){
    for(int q:op.qubits)used.insert(q);
    if(op.kind==OpKind::Barrier||op.kind==OpKind::GlobalPhase)continue;
    if(op.kind==OpKind::Measure){++readWeight[op.qubits.at(0)];continue;}
    if(op.qubits.size()==1){++oneWeight[op.qubits[0]];continue;}
    if(op.qubits.size()!=2)throw std::invalid_argument("post-layout requires one- or two-qubit instructions");
    int a=op.qubits[0],b=op.qubits[1];
    const bool directional=chip.directedConnectivity&&(op.kind==OpKind::Cx||op.kind==OpKind::Ecr);
    adjacency[a].insert(b);adjacency[b].insert(a);
    if(!directional&&a>b)std::swap(a,b);
    ++weights[{a,b,directional}];
  }
  if(used.empty())return finish(m);
  std::vector<Edge> edges;
  for(auto [key,weight]:weights){auto [a,b,direction]=key;edges.push_back({a,b,direction,weight});}
  const double infinity=std::numeric_limits<double>::infinity();
  auto nodeCost=[&](int logical,int physicalQ){
    double value=0;
    for(const auto category:{0,1}){
      const auto& errors=category==0?costs.oneQubitError:costs.readoutError;
      const double weight=category==0?oneWeight[logical]:readWeight[logical];
      if(errors.empty()||weight==0)continue;
      const auto found=errors.find(physicalQ);if(found==errors.end())return infinity;
      value+=weight*-std::log1p(-found->second);
    }
    return value;
  };
  auto edgeCost=[&](const Edge& edge,int a,int b){
    if(a==b)return infinity;
    if(!chip.allToAll){
      if(edge.directed){if(!directed.contains({a,b}))return infinity;}
      else if(!physical[a].contains(b))return infinity;
    }
    if(costs.twoQubitError.empty())return 0.0;
    auto found=costs.twoQubitError.find({a,b});
    if(found==costs.twoQubitError.end()&&!edge.directed)found=costs.twoQubitError.find({b,a});
    if(found==costs.twoQubitError.end())return infinity;
    return double(edge.weight)*-std::log1p(-found->second);
  };
  std::vector<int> mapping(circuit.numQubits,-1),bestMapping(circuit.numQubits);
  std::iota(bestMapping.begin(),bestMapping.end(),0);
  double best=0;
  for(int q:used)best+=nodeCost(q,q);
  for(const auto& edge:edges)best+=edgeCost(edge,edge.a,edge.b);
  stats.originalCost=stats.selectedCost=best;
  // Comparing a fully measured new layout against unknown old errors would
  // be misleading. Retain the input until the baseline is calibrated too.
  if(!std::isfinite(best))return finish(m);
  std::vector<int> order(used.begin(),used.end());
  std::sort(order.begin(),order.end(),[&](int a,int b){
    return std::tuple{-int(adjacency[a].size()),-(oneWeight[a]+readWeight[a]),a}<
           std::tuple{-int(adjacency[b].size()),-(oneWeight[b]+readWeight[b]),b};
  });
  std::vector<bool> occupied(chip.qubits);
  std::function<void(std::size_t,double)> search;
  search=[&](std::size_t depth,double partial){
    if(stats.visitedStates>=stateBudget){stats.budgetExhausted=true;return;}
    if(depth==order.size()){
      if(partial+1e-14<best){best=partial;bestMapping=mapping;stats.improved=true;}
      return;
    }
    const int q=order[depth];
    std::vector<std::pair<double,int>> candidates;
    for(std::size_t p=0;p<chip.qubits;++p){
      if(occupied[p]||(!chip.allToAll&&physical[p].size()<adjacency[q].size()))continue;
      double increment=nodeCost(q,int(p));
      for(const auto& edge:edges){
        if(edge.a==q&&mapping[edge.b]>=0)increment+=edgeCost(edge,int(p),mapping[edge.b]);
        else if(edge.b==q&&mapping[edge.a]>=0)increment+=edgeCost(edge,mapping[edge.a],int(p));
      }
      if(std::isfinite(increment)&&partial+increment+1e-14<best)
        candidates.emplace_back(increment,int(p));
    }
    std::sort(candidates.begin(),candidates.end());
    for(auto [increment,p]:candidates){
      if(stats.visitedStates>=stateBudget){stats.budgetExhausted=true;break;}
      ++stats.visitedStates;mapping[q]=p;occupied[p]=true;
      // VF2 frontier feasibility: enough free physical neighbors must remain
      // for the unassigned logical neighbors of this newly mapped vertex.
      std::size_t needed=0,available=0;
      for(int neighbor:adjacency[q])if(mapping[neighbor]<0)++needed;
      if(chip.allToAll)available=chip.qubits-depth-1;
      else for(int neighbor:physical[p])if(!occupied[neighbor])++available;
      if(available>=needed)search(depth+1,partial+increment);
      occupied[p]=false;mapping[q]=-1;
    }
  };
  search(0,0);stats.selectedCost=best;
  if(!stats.improved)return finish(m);
  // Validate the completed embedding independently before changing the IR.
  std::set<int> unique;
  for(int q:used)if(bestMapping[q]<0||!unique.insert(bestMapping[q]).second)
    throw std::runtime_error("post-layout search returned a noninjective mapping");
  for(const auto& edge:edges)if(!std::isfinite(edgeCost(edge,bestMapping[edge.a],bestMapping[edge.b])))
    throw std::runtime_error("post-layout search returned an invalid native edge");
  // Extend the active-wire embedding to a permutation so inactive logical
  // wires and arbitrary-input initial/final layouts remain well defined.
  std::vector<int> permutation(chip.qubits,-1);
  for(int q:used)permutation[q]=bestMapping[q];
  for(std::size_t p=0;p<chip.qubits;++p)
    if(permutation[p]<0&&!unique.contains(int(p))){permutation[p]=int(p);unique.insert(int(p));}
  int next=0;
  for(auto& p:permutation)if(p<0){while(unique.contains(next))++next;p=next;unique.insert(next);}
  for(auto& op:circuit.instructions)for(auto& q:op.qubits)q=bestMapping[q];
  for(int q:used)circuit.numQubits=std::max(circuit.numQubits,std::size_t(bestMapping[q]+1));
  for(auto* layout:{&circuit.initialLayout,&circuit.finalLayout})
    for(auto& p:*layout){p=permutation.at(p);circuit.numQubits=std::max(circuit.numQubits,std::size_t(p+1));}
  return finish(rebuild(circuit));
}
} // namespace spinor::passes

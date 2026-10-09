#include "spinor/passes/ResonatorRouting.h"
#include "spinor/dialect/Resonators.h"
#include "spinor/registry/ComponentTopology.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <tuple>

namespace spinor::passes {
namespace {
using namespace dialect;
struct Route {
  int moved=-1, resonator=-1;
  std::pair<int,int> cz;
  std::optional<double> error;
};
auto routeCost(const Route& route) {
  return std::tuple{route.resonator<0 ? 1 : 3, route.error ? 0 : 1,
                    route.error.value_or(0.0),route.resonator,route.moved,route.cz};
}
std::pair<int,int> edge(int a,int b) { return std::minmax(a,b); }
std::optional<double> errorFor(const registry::ChipInfo& chip,int a,int b) {
  auto it=chip.calibrationTwoQubitError.find({a,b});
  if(it==chip.calibrationTwoQubitError.end())it=chip.calibrationTwoQubitError.find({b,a});
  if(it==chip.calibrationTwoQubitError.end())return std::nullopt;
  return it->second;
}
auto physicalCost(const Module& module) {
  std::size_t two=0,total=0;
  for(const auto& op:flatten(module).instructions) {
    if(isControl(op.kind)||op.kind==OpKind::GlobalPhase||op.kind==OpKind::Barrier||op.kind==OpKind::Measure)continue;
    ++total;if(op.qubits.size()==2)++two;
  }
  return std::pair{two,total};
}
}

Module compileResonatorCircuit(const Module& input,const registry::ChipInfo& chip,
    OptimizationLevel level,Diagnostics& diagnostics) {
  const auto physical=registry::computationalComponents(chip);
  const auto original=flatten(input);
  if(!original.resonatorQubits.empty() || std::any_of(original.instructions.begin(),original.instructions.end(),
      [](const auto& op){return op.kind==OpKind::Move;}))
    throw std::runtime_error("physical MOVE IR must be validated/emitted with --compiled; MOVE is not a logical SWAP");
  if(original.numQubits>physical.size())throw std::runtime_error("circuit exceeds computational-qubit capacity (resonators are reserved)");
  if(chip.decompose.twoQubitEntangler!="cz")throw std::runtime_error("resonator routing requires a native CZ synthesis basis");
  std::map<int,int> compact;
  for(std::size_t q=0;q<physical.size();++q)compact[physical[q]]=static_cast<int>(q);
  std::map<std::pair<int,int>,Route> routes;
  auto offer=[&](int a,int b,Route route) {
    if(a==b||!compact.contains(a)||!compact.contains(b))return;
    auto pair=edge(a,b);auto existing=routes.find(pair);
    if(existing==routes.end()||routeCost(route)<routeCost(existing->second))routes[pair]=std::move(route);
  };
  for(auto [a,b]:chip.czLoci)if(compact.contains(a)&&compact.contains(b))
    offer(a,b,Route{-1,-1,{a,b},errorFor(chip,a,b)});
  for(auto [moved,resonator]:chip.moveLoci)for(auto [a,b]:chip.czLoci) {
    int other=a==resonator?b:b==resonator?a:-1;
    if(other<0||!compact.contains(other)||other==moved)continue;
    std::optional<double> error;
    auto moveError=errorFor(chip,moved,resonator),czError=errorFor(chip,a,b);
    if(moveError&&czError)error=1-(1-*moveError)*(1-*moveError)*(1-*czError);
    offer(moved,other,Route{moved,resonator,{a,b},error});
  }

  // The existing optimizer sees only exact CZ operations on computational
  // qubits. SWAP routing between effective neighbors is synthesized by us into
  // three CZs; no physical SWAP is ever applied to a resonator.
  auto effective=chip;
  effective.qubits=physical.size();effective.allToAll=false;effective.directedConnectivity=false;
  effective.computationalQubits.clear();effective.resonatorQubits.clear();
  // The effective graph has computational indices, not original physical
  // component indices. Discovery constraints must use that same coordinate system.
  effective.availableQubits.reset();effective.unavailableQubits.clear();effective.singleQubitGateLoci.clear();
  if(chip.availableQubits) {
    effective.availableQubits.emplace();
    for(int q:*chip.availableQubits)if(compact.contains(q))effective.availableQubits->push_back(compact.at(q));
  }
  for(int q:chip.unavailableQubits)if(compact.contains(q))effective.unavailableQubits.push_back(compact.at(q));
  for(const auto& [gate,loci]:chip.singleQubitGateLoci) {
    auto& mapped=effective.singleQubitGateLoci[gate];
    for(int q:loci)if(compact.contains(q))mapped.push_back(compact.at(q));
  }
  effective.moveLoci.clear();effective.czLoci.clear();effective.coupling.clear();
  effective.nativeGates.erase(std::remove(effective.nativeGates.begin(),effective.nativeGates.end(),"move"),effective.nativeGates.end());
  effective.calibrationOneQubitError.clear();effective.calibrationReadoutError.clear();effective.calibrationTwoQubitError.clear();
  for(const auto& [q,error]:chip.calibrationOneQubitError)if(compact.contains(q))effective.calibrationOneQubitError[compact.at(q)]=error;
  for(const auto& [q,error]:chip.calibrationReadoutError)if(compact.contains(q))effective.calibrationReadoutError[compact.at(q)]=error;
  for(const auto& [pair,route]:routes) {
    auto logical=edge(compact.at(pair.first),compact.at(pair.second));
    effective.coupling.push_back(logical);
    if(route.error)effective.calibrationTwoQubitError[logical]=*route.error;
  }

  auto expand=[&](const Module& logical,bool optimize) {
    auto circuit=flatten(logical);auto out=circuit;
    out.numQubits=chip.qubits;out.resonatorQubits=chip.resonatorQubits;out.instructions.clear();
    for(auto& q:out.initialLayout)q=physical.at(q);
    for(auto& q:out.finalLayout)q=physical.at(q);
    auto append=[&](WireOp op) {
      // Adjacent closing/opening MOVE pairs cancel on the allowed subspace.
      // Keep all other instructions as fences, including branch markers.
      if(optimize&&op.kind==OpKind::Move&&!out.instructions.empty()&&
          out.instructions.back().kind==OpKind::Move&&out.instructions.back().qubits==op.qubits) {
        out.instructions.pop_back();return;
      }
      out.instructions.push_back(std::move(op));
    };
    for(auto op:circuit.instructions) {
      for(auto& q:op.qubits)q=physical.at(q);
      if(op.kind!=OpKind::Cz){append(std::move(op));continue;}
      const auto& route=routes.at(edge(op.qubits.at(0),op.qubits.at(1)));
      if(route.resonator>=0)append({OpKind::Move,{route.moved,route.resonator},{},op.loc});
      append({OpKind::Cz,{route.cz.first,route.cz.second},{},op.loc});
      if(route.resonator>=0)append({OpKind::Move,{route.moved,route.resonator},{},op.loc});
    }
    validateResonatorCircuit(out);
    return rebuild(out);
  };
  auto optimized=PassManager{}.compile(input,effective,level,diagnostics);
  if(diagnostics.hasErrors())return input;
  auto result=expand(optimized,level!=OptimizationLevel::O0);
  // Optimization on the effective graph must not conceal growth in physical
  // MOVE count on mixed direct/mediated architectures.
  if(level!=OptimizationLevel::O0) {
    Diagnostics baselineDiagnostics;
    auto baseline=PassManager{}.compile(input,effective,OptimizationLevel::O0,baselineDiagnostics);
    if(!baselineDiagnostics.hasErrors()) {
      baseline=expand(baseline,false);
      const auto currentCost=physicalCost(result),baselineCost=physicalCost(baseline);
      if(currentCost.first>baselineCost.first||currentCost.second>baselineCost.second)result=std::move(baseline);
    }
  }
  validateCompiled(result,chip,diagnostics);
  return result;
}
}

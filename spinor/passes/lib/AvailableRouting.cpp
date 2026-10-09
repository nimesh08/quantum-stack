#include "spinor/passes/AvailableRouting.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/registry/ComponentTopology.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>

namespace spinor::passes {
namespace {
using namespace dialect;
std::set<int> available(const registry::ChipInfo& chip) {
  std::set<int> result;
  if (chip.availableQubits) result.insert(chip.availableQubits->begin(),chip.availableQubits->end());
  else for(std::size_t q=0;q<chip.qubits;++q)result.insert(static_cast<int>(q));
  for(int q:result)if(q<0||static_cast<std::size_t>(q)>=chip.qubits)
    throw std::runtime_error("availability contains an invalid physical qubit");
  for(int q:chip.unavailableQubits)result.erase(q);
  return result;
}
std::string name(OpKind kind) { return std::string(opMnemonic(kind)).substr(7); }
}

void validateAvailableCircuit(const dialect::WireCircuit& circuit,const registry::ChipInfo& chip) {
  const auto active=available(chip);
  auto check=[&](int q){if(!active.contains(q))throw std::runtime_error("compiled circuit uses an unavailable physical qubit");};
  for(int q:circuit.initialLayout)check(q);
  for(int q:circuit.finalLayout)check(q);
  for(const auto& op:circuit.instructions) {
    for(int q:op.qubits)check(q);
    if(op.qubits.size()!=1||op.kind==OpKind::Barrier)continue;
    const auto locus=chip.singleQubitGateLoci.find(name(op.kind));
    if(locus!=chip.singleQubitGateLoci.end()&&
        std::find(locus->second.begin(),locus->second.end(),op.qubits[0])==locus->second.end())
      throw std::runtime_error("native operation '"+name(op.kind)+"' is unavailable on its physical qubit");
  }
}

std::optional<dialect::Module> compileAvailableCircuit(const dialect::Module& input,
    const registry::ChipInfo& chip,OptimizationLevel level,dialect::Diagnostics& diagnostics) {
  if(!chip.availableQubits&&!chip.unavailableQubits.size()&&chip.singleQubitGateLoci.empty())return std::nullopt;
  const auto computers=registry::computationalComponents(chip);
  const std::set<int> resonators(chip.resonatorQubits.begin(),chip.resonatorQubits.end());
  auto active=available(chip);
  std::set<std::string> needed;
  auto collect=[&](const Module& module) {
    for(const auto& op:flatten(module).instructions)
      if(op.qubits.size()==1&&op.kind!=OpKind::Barrier)needed.insert(name(op.kind));
  };
  if(!chip.singleQubitGateLoci.empty()) {
    // Required operations come from the owned synthesis basis, not every gate
    // the device happens to advertise. Routing may also introduce SWAP recipes.
    auto basis=chip;basis.allToAll=true;basis.directedConnectivity=false;
    auto decomposed=Decomposition{}.run(input,basis,diagnostics);
    if(diagnostics.hasErrors())return input;
    collect(decomposed);
    const auto source=flatten(input);
    if(!chip.allToAll&&std::any_of(source.instructions.begin(),source.instructions.end(),
        [](const auto& op){return op.qubits.size()==2&&op.kind!=OpKind::Barrier;})) {
      WireCircuit probe;probe.numQubits=2;probe.initialLayout={0,1};probe.finalLayout={0,1};
      probe.instructions.push_back({OpKind::Swap,{0,1},{},{}});
      collect(Decomposition{}.run(rebuild(probe),basis,diagnostics));
      if(diagnostics.hasErrors())return input;
    }
    for(const auto& gate:needed) {
      const auto found=chip.singleQubitGateLoci.find(gate);
      if(found==chip.singleQubitGateLoci.end())continue;
      const std::set<int> loci(found->second.begin(),found->second.end());
      std::erase_if(active,[&](int q){return !resonators.contains(q)&&!loci.contains(q);});
    }
  }
  // Even an identity embedding needs the legal-O0 fallback below when an
  // optimized variant introduces a gate with a narrower advertised locus.
  if(active.size()==chip.qubits&&chip.singleQubitGateLoci.empty())return std::nullopt;
  const auto capacity=std::count_if(computers.begin(),computers.end(),[&](int q){return active.contains(q);});
  if(flatten(input).numQubits>static_cast<std::size_t>(capacity))
    throw std::runtime_error("no usable physical subgraph has enough qubits for the required native gates and readout; refresh target capabilities or select a compatible region");
  if(active.empty())throw std::runtime_error("target has no available physical qubits");
  std::vector<int> physical(active.begin(),active.end());
  std::map<int,int> compact;
  for(std::size_t i=0;i<physical.size();++i)compact[physical[i]]=static_cast<int>(i);
  auto effective=chip;
  effective.qubits=physical.size();effective.availableQubits.reset();effective.unavailableQubits.clear();
  effective.singleQubitGateLoci.clear();effective.computationalQubits.clear();effective.resonatorQubits.clear();
  for(int q:computers)if(compact.contains(q))effective.computationalQubits.push_back(compact.at(q));
  for(int q:chip.resonatorQubits)if(compact.contains(q))effective.resonatorQubits.push_back(compact.at(q));
  auto edges=[&](const auto& original,auto& destination) {
    destination.clear();
    for(auto [a,b]:original)if(compact.contains(a)&&compact.contains(b))destination.emplace_back(compact.at(a),compact.at(b));
  };
  edges(chip.coupling,effective.coupling);edges(chip.moveLoci,effective.moveLoci);edges(chip.czLoci,effective.czLoci);
  if(effective.resonatorQubits.empty())std::erase(effective.nativeGates,"move");
  effective.calibrationOneQubitError.clear();effective.calibrationReadoutError.clear();effective.calibrationTwoQubitError.clear();
  for(const auto& [q,error]:chip.calibrationOneQubitError)if(compact.contains(q))effective.calibrationOneQubitError[compact.at(q)]=error;
  for(const auto& [q,error]:chip.calibrationReadoutError)if(compact.contains(q))effective.calibrationReadoutError[compact.at(q)]=error;
  for(const auto& [pair,error]:chip.calibrationTwoQubitError)
    if(compact.contains(pair.first)&&compact.contains(pair.second))effective.calibrationTwoQubitError[{compact.at(pair.first),compact.at(pair.second)}]=error;
  auto lift=[&](const Module& compiled) {
    auto circuit=flatten(compiled);circuit.numQubits=chip.qubits;circuit.resonatorQubits=chip.resonatorQubits;
    for(auto& q:circuit.initialLayout)q=physical.at(q);
    for(auto& q:circuit.finalLayout)q=physical.at(q);
    for(auto& op:circuit.instructions)for(auto& q:op.qubits)q=physical.at(q);
    return rebuild(circuit);
  };
  auto compiled=PassManager{}.compile(input,effective,level,diagnostics);
  if(diagnostics.hasErrors())return input;
  auto result=lift(compiled);
  // Resynthesis may introduce another native gate. Fall back to legal O0
  // before rejecting a program when its optimized variant violates a locus.
  Diagnostics validation;
  if(!validateCompiled(result,chip,validation)&&level!=OptimizationLevel::O0) {
    Diagnostics baseline;
    auto candidate=PassManager{}.compile(input,effective,OptimizationLevel::O0,baseline);
    if(!baseline.hasErrors())result=lift(candidate);
  }
  validateCompiled(result,chip,diagnostics);
  return result;
}
}

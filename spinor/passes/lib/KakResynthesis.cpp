#include "spinor/passes/KakResynthesis.h"
#include "TwoQubitMath.h"
#include "spinor/passes/CompilationReport.h"
#include <map>
#include <set>

namespace spinor::passes {
dialect::Module KakResynthesis::run(const dialect::Module& m,
    const std::vector<TwoQBlock>& blocks,const SynthesisTraits& traits,
    const registry::ChipInfo* chip) const {
  return run(m,ConsolidateBlocks{}.run(m,blocks),traits,chip);
}

dialect::Module KakResynthesis::run(const dialect::Module& m,
    const std::vector<ConsolidatedBlock>& blocks,const SynthesisTraits& traits,
    const registry::ChipInfo* chip) const {
  if(dialect::hasControlFlow(m))return m;
  auto circuit=dialect::flatten(m);
  const auto indices=twoq::instructionIndices(m);
  std::map<std::size_t,KakResult> replacements;
  std::set<std::size_t> removed;
  std::vector<std::size_t> regionFor(circuit.instructions.size());
  for(std::size_t i=0,region=0;i<circuit.instructions.size();++i){regionFor[i]=region;if(isNumericalRegionBoundary(circuit.instructions[i]))++region;}
  for(const auto& consolidated:blocks){
    const auto& block=consolidated.block;
    if(block.ops.empty())continue;
    registry::ChipInfo localChip;
    if(chip){
      localChip=*chip;localChip.qubits=2;localChip.coupling.clear();
      for(auto [a,b]:chip->coupling){
        if(a==block.qa&&b==block.qb)localChip.coupling.emplace_back(0,1);
        if(a==block.qb&&b==block.qa)localChip.coupling.emplace_back(1,0);
      }
    }
    try {
      const auto candidate=[&]{CompilationReportScope temporary(nullptr);
        return TwoQubitDecomposer{}.decompose(consolidated.unitary,traits,chip?&localChip:nullptr);}();
      if(auto* report=currentCompilationReport()){
        ++report->counters["two_qubit_synthesis_trials"];
        ++report->counters["two_qubit_construction_"+candidate.construction];
        if(candidate.analyticalAttempted){
          ++report->counters["analytical_native_trials"];
          ++report->counters[candidate.analyticalRejection.empty()?"analytical_native_selected":"analytical_native_rejected_"+candidate.analyticalRejection];
        }
      }
      const auto candidateCost=std::pair{std::size_t(candidate.entanglerUses),candidate.operations.size()};
      const auto originalCost=std::pair{consolidated.entanglerUses,block.ops.size()};
      if(candidateCost>=originalCost||candidate.operations.size()>block.ops.size()){
        if(auto* report=currentCompilationReport())++report->counters["two_qubit_rewrite_non_improving"];
        continue;
      }
      // Do not trust a stale side table: verify against the current block.
      auto original=la::identity4();
      for(auto id:block.ops){
        const auto& op=circuit.instructions.at(indices.at(id.v));
        original=la::mul4(twoq::operationMatrix(op,block.qa,block.qb),original);
      }
      auto actual=twoq::scale(twoq::compose(candidate.operations),std::polar(1.0,candidate.globalPhase));
      if(twoq::distance(original,actual)>1e-9)
        throw std::runtime_error("KAK replacement changed a block's matrix or global phase");
      auto placed=candidate;
      for(auto& op:placed.operations){
        for(auto& q:op.qubits)q=q==0?block.qa:block.qb;
        op.loc=circuit.instructions[indices[block.ops.front().v]].loc;
      }
      const auto first=indices.at(block.ops.front().v);
      std::vector<dialect::WireOp> originalOps;
      for(auto id:block.ops)originalOps.push_back(circuit.instructions.at(indices.at(id.v)));
      observeRewrite("two-qubit-kak","native-block",originalOps,placed.operations,0,placed.globalPhase,1e-9,numericalRegion(regionFor[first]));
      if(auto* report=currentCompilationReport())++report->counters["two_qubit_rewrites_accepted"];
      replacements.emplace(first,std::move(placed));
      for(auto id:block.ops)removed.insert(indices.at(id.v));
    }catch(const std::runtime_error&){
      if(auto* report=currentCompilationReport())++report->counters["two_qubit_synthesis_fallbacks"];
      // Optimization is optional: a nonrepresentable exact candidate (for
      // example a discrete Clifford+T target) keeps the existing native block.
      // The original compilation remains valid; no approximate fallback.
    }
  }
  if(replacements.empty())return m;
  auto output=circuit;output.instructions.clear();
  for(std::size_t i=0;i<circuit.instructions.size();++i){
    if(auto replacement=replacements.find(i);replacement!=replacements.end()){
      output.instructions.insert(output.instructions.end(),replacement->second.operations.begin(),replacement->second.operations.end());
      output.globalPhase+=replacement->second.globalPhase;
    }
    if(!removed.contains(i))output.instructions.push_back(circuit.instructions[i]);
  }
  return dialect::rebuild(output);
}
} // namespace spinor::passes

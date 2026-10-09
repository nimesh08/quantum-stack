#include "spinor/passes/ConsolidateBlocks.h"
#include "TwoQubitMath.h"
#include <set>

namespace spinor::passes {
std::vector<ConsolidatedBlock> ConsolidateBlocks::run(
    const dialect::Module& m,const std::vector<TwoQBlock>& blocks) const {
  if(dialect::hasControlFlow(m))return {};
  const auto circuit=dialect::flatten(m);
  const auto indices=twoq::instructionIndices(m);
  std::set<std::uint32_t> occupied;
  std::vector<ConsolidatedBlock> result;
  for(const auto& block:blocks){
    if(block.qa<0||block.qb<=block.qa||std::size_t(block.qb)>=circuit.numQubits||block.ops.empty())
      throw std::invalid_argument("invalid two-qubit block");
    std::set<std::uint32_t> members;
    auto matrix=la::identity4();std::size_t entanglers=0;
    std::uint32_t previous=0;bool first=true;
    for(const auto id:block.ops){
      if(id.v>=indices.size()||(!first&&id.v<=previous)||!occupied.insert(id.v).second)
        throw std::invalid_argument("unordered or overlapping two-qubit blocks");
      const auto index=indices[id.v];
      if(index>=circuit.instructions.size())throw std::invalid_argument("allocation inside a two-qubit block");
      const auto& op=circuit.instructions[index];
      matrix=la::mul4(twoq::operationMatrix(op,block.qa,block.qb),matrix);
      if(op.qubits.size()==2)++entanglers;
      members.insert(id.v);previous=id.v;first=false;
    }
    // Interleaved operations may be moved past a block only when disjoint.
    for(auto id=block.ops.front().v;id<=block.ops.back().v;++id){
      if(members.contains(id)||indices[id]>=circuit.instructions.size())continue;
      const auto& op=circuit.instructions[indices[id]];
      if(dialect::isControl(op.kind))throw std::invalid_argument("control flow inside a two-qubit block");
      for(int q:op.qubits)if(q==block.qa||q==block.qb)
        throw std::invalid_argument("two-qubit block crosses a dependent operation");
    }
    twoq::requireUnitary(matrix);
    result.push_back({block,twoq::pack(matrix),entanglers});
  }
  return result;
}
} // namespace spinor::passes

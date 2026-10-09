#include "spinor/passes/OneQubitEulerResynthesis.h"
#include "NativeSynthesis.h"

namespace spinor::passes {
using namespace dialect;
Module OneQubitEulerResynthesis::run(const Module& input,const SynthesisTraits& traits) const {
  if(hasControlFlow(input))return input;
  auto in=flatten(input),out=in;out.instructions.clear();
  registry::ChipInfo chip;chip.nativeGates=traits.nativeGates;
  // Compatibility with callers constructing traits directly.
  if(chip.nativeGates.empty()){
    if(!traits.rotationGate.empty())chip.nativeGates.push_back(traits.rotationGate);
    if(!traits.pi2Gate.empty())chip.nativeGates.push_back(traits.pi2Gate);
  }
  for(std::size_t i=0;i<in.instructions.size();){
    const auto& first=in.instructions[i];
    if(first.qubits.size()!=1||first.kind==OpKind::Measure||first.kind==OpKind::Reset||first.kind==OpKind::Barrier){out.instructions.push_back(first);++i;continue;}
    std::size_t end=i;auto u=la::identity2();
    while(end<in.instructions.size()){
      const auto& op=in.instructions[end];
      if(op.qubits!=first.qubits||op.kind==OpKind::Measure||op.kind==OpKind::Reset||op.kind==OpKind::Barrier)break;
      u=la::mul2(matrix1(op),u);++end;
    }
    double phase=0;
    try{
      auto candidate=synthesizeOne(u,first.qubits[0],chip,phase);
      if(candidate.size()<end-i){out.instructions.insert(out.instructions.end(),candidate.begin(),candidate.end());out.globalPhase+=phase;i=end;continue;}
    }catch(const std::runtime_error&){/* restricted/discrete basis: retain exact original sequence */}
    out.instructions.insert(out.instructions.end(),in.instructions.begin()+i,in.instructions.begin()+end);i=end;
  }
  return rebuild(out);
}
} // namespace spinor::passes

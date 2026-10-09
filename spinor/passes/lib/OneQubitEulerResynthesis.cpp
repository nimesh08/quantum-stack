#include "spinor/passes/OneQubitEulerResynthesis.h"
#include "NativeSynthesis.h"
#include "spinor/passes/CompilationReport.h"

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
  struct Pending {std::vector<WireOp> ops;la::Mat2 matrix=la::identity2();};
  std::vector<Pending> pending(in.numQubits);
  std::size_t region=0;
  auto flush=[&](int q){
    auto& block=pending.at(q);if(block.ops.empty())return;
    bool accepted=false;
    try{
      double phase=0;auto candidate=synthesizeOne(block.matrix,q,chip,phase);
      if(candidate.size()<block.ops.size()){
        observeRewrite("one-qubit-euler","disjoint-wire-block",block.ops,candidate,0,phase,1e-9,numericalRegion(region));
        out.instructions.insert(out.instructions.end(),candidate.begin(),candidate.end());
        out.globalPhase+=phase;accepted=true;
      }
    }catch(const std::runtime_error&){
      if(auto* report=currentCompilationReport())++report->counters["one_qubit_synthesis_fallbacks"];
    }
    if(!accepted)out.instructions.insert(out.instructions.end(),block.ops.begin(),block.ops.end());
    block.ops.clear();block.matrix=la::identity2();
  };
  auto flushAll=[&]{for(std::size_t q=0;q<pending.size();++q)flush(static_cast<int>(q));};
  for(const auto& op:in.instructions){
    if(op.qubits.size()==1&&isUnitaryInstruction(op)){
      auto& block=pending.at(op.qubits[0]);
      if(block.ops.size()==256)flush(op.qubits[0]);
      block.matrix=la::mul2(matrix1(op),block.matrix);block.ops.push_back(op);
    }else{
      // Classical/unknown instructions, measurement, reset, MOVE and scalar
      // phases flush all wires; unitary entanglers flush only participants.
      if(op.qubits.size()==2&&isUnitaryInstruction(op))for(int q:op.qubits)flush(q);
      else flushAll();
      out.instructions.push_back(op);
      if(isNumericalRegionBoundary(op))++region;
    }
  }
  flushAll();return rebuild(out);
}
} // namespace spinor::passes

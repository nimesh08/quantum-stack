#include "spinor/passes/CommutativeCancellation.h"
#include "spinor/passes/Cleanup.h"
#include "GateMatrices.h"
#include <optional>

namespace spinor::passes {
using namespace dialect;
namespace {
bool diagonal(OpKind k){return k==OpKind::Rz||k==OpKind::Z||k==OpKind::S||k==OpKind::Sdg||k==OpKind::T||k==OpKind::Tdg||k==OpKind::Cz||k==OpKind::Rzz;}
bool commute(const WireOp& a,const WireOp& b){
  if(a.kind==OpKind::Barrier||b.kind==OpKind::Barrier)return false;
  bool overlap=false;for(int x:a.qubits)for(int y:b.qubits)overlap|=x==y;
  if(!overlap)return true;
  if(diagonal(a.kind)&&diagonal(b.kind))return true;
  if(a.kind==OpKind::Cx&&b.qubits.size()==1){
    if(b.qubits[0]==a.qubits[0]&&diagonal(b.kind))return true;
    if(b.qubits[0]==a.qubits[1]&&(b.kind==OpKind::X||b.kind==OpKind::Rx||b.kind==OpKind::Sx||b.kind==OpKind::Sxdg))return true;
  }
  return false;
}
}
Module CommutativeCancellation::run(const Module& input,const SynthesisTraits&) const {
  if(hasControlFlow(input))return input;
  auto c=flatten(input);std::vector<std::optional<WireOp>> ops;
  for(auto op:c.instructions){
    bool erased=false;
    if(op.kind!=OpKind::Measure&&op.kind!=OpKind::Reset&&op.kind!=OpKind::Barrier)
      for(std::size_t j=ops.size();j-->0;){
        if(!ops[j])continue;auto& prev=*ops[j];
        if(prev.qubits==op.qubits&&prev.kind==op.kind){
          try{
            double phase=op.qubits.size()==1?phaseDifference(la::mul2(matrix1(op),matrix1(prev)),la::identity2()):phaseDifference(la::mul4(matrix2(op),matrix2(prev)),la::identity4());
            c.globalPhase+=phase;ops[j].reset();erased=true;
          }catch(const std::runtime_error&){}
          if(erased)break;
        }
        if(!commute(op,prev))break;
      }
    if(!erased)ops.push_back(std::move(op));
  }
  c.instructions.clear();for(auto& op:ops)if(op)c.instructions.push_back(std::move(*op));
  return Cleanup{}.run(rebuild(c));
}
} // namespace spinor::passes

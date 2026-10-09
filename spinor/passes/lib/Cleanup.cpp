#include "spinor/passes/Cleanup.h"
#include "GateMatrices.h"
#include <optional>

namespace spinor::passes {
using namespace dialect;
Module Cleanup::run(const Module& input) const {
  if(hasControlFlow(input))return input;
  auto in=flatten(input),out=in;out.instructions.clear();
  std::vector<std::optional<WireOp>> ops;
  for(auto op:in.instructions){
    bool consumed=false;
    if(op.kind!=OpKind::Measure&&op.kind!=OpKind::Reset&&op.kind!=OpKind::Barrier){
      for(std::size_t j=ops.size();j-->0;){
        if(!ops[j])continue;auto& prev=*ops[j];
        if(prev.kind==OpKind::Barrier&&prev.qubits.empty())break;
        bool overlap=false;for(int q:op.qubits)for(int p:prev.qubits)overlap|=q==p;
        if(!overlap)continue;
        if(prev.qubits!=op.qubits||prev.kind==OpKind::Measure||prev.kind==OpKind::Reset||prev.kind==OpKind::Barrier)break;
        if(prev.kind==op.kind&&(op.kind==OpKind::Rz||op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rxx||op.kind==OpKind::Rzz)){
          double a=parameter(prev)+parameter(op);prev.attributes={angleAttr(a)};
          if(std::abs(a)<kRecognitionTolerance)ops[j].reset();consumed=true;break;
        }
        try{
          double phase=op.qubits.size()==1?phaseDifference(la::mul2(matrix1(op),matrix1(prev)),la::identity2(),kRecognitionTolerance):phaseDifference(la::mul4(matrix2(op),matrix2(prev)),la::identity4(),kRecognitionTolerance);
          out.globalPhase+=phase;ops[j].reset();consumed=true;
        }catch(const std::runtime_error&){ }
        break;
      }
    }
    if(!consumed)ops.push_back(std::move(op));
  }
  for(auto& op:ops)if(op)out.instructions.push_back(std::move(*op));
  return rebuild(out);
}
Module Cleanup::run(const Module& input,const registry::ChipInfo&) const{return run(input);}
} // namespace spinor::passes

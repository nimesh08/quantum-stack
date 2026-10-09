#include "spinor/passes/Cleanup.h"
#include "GateMatrices.h"
#include "spinor/passes/CompilationReport.h"
#include <optional>

namespace spinor::passes {
using namespace dialect;
Module Cleanup::run(const Module& input) const {
  if(hasControlFlow(input))return input;
  auto in=flatten(input),out=in;out.instructions.clear();
  std::vector<std::optional<WireOp>> ops;
  std::size_t region=0;
  for(auto op:in.instructions){
    bool consumed=false;
    if(isUnitaryInstruction(op)&&!op.qubits.empty()){
      std::size_t visited=0;
      for(std::size_t j=ops.size();j-->0&&visited++<256;){
        if(!ops[j])continue;auto& prev=*ops[j];
        if(isNumericalRegionBoundary(prev))break;
        bool overlap=false;for(int q:op.qubits)for(int p:prev.qubits)overlap|=q==p;
        if(!overlap)continue;
        if(prev.qubits!=op.qubits||prev.kind==OpKind::Measure||prev.kind==OpKind::Reset||prev.kind==OpKind::Barrier)break;
        if(prev.kind==op.kind&&(op.kind==OpKind::Rz||op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rxx||op.kind==OpKind::Rzz)){
          const auto original=prev;
          // Bound each represented angle before addition, including when this
          // pass is invoked directly rather than through PassManager.
          auto bounded=[](double v){return 2*std::atan2(std::sin(v/2),std::cos(v/2));};
          const auto sum=bounded(parameter(prev))+bounded(parameter(op));
          const double a=bounded(sum);auto candidate=prev;candidate.attributes={angleAttr(a)};
          const bool erased=std::abs(a)<kRecognitionTolerance;
          // Identity classification stays strict. The separate reconstruction
          // guard validates an otherwise exact, retained rotation recipe.
          const double guard=erased?kRecognitionTolerance:1e-9;
          try{
            const double phase=op.qubits.size()==1
              ?phaseDifference(la::mul2(matrix1(op),matrix1(original)),erased?la::identity2():matrix1(candidate),guard)
              :phaseDifference(la::mul4(matrix2(op),matrix2(original)),erased?la::identity4():matrix2(candidate),guard);
            observeRewrite("cleanup","rotation-merge",{original,op},erased?std::vector<WireOp>{}:std::vector<WireOp>{candidate},0,phase,guard,numericalRegion(region));
            out.globalPhase+=phase;
            if(erased)ops[j].reset();else prev=std::move(candidate);
            consumed=true;
          }catch(const std::runtime_error&){
            if(auto* report=currentCompilationReport())++report->counters["rotation_merge_reconstruction_fallbacks"];
          }
          break;
        }
        try{
          double phase=op.qubits.size()==1?phaseDifference(la::mul2(matrix1(op),matrix1(prev)),la::identity2(),kRecognitionTolerance):phaseDifference(la::mul4(matrix2(op),matrix2(prev)),la::identity4(),kRecognitionTolerance);
          observeRewrite("cleanup","inverse-cancellation",{prev,op},{},0,phase,kRecognitionTolerance,numericalRegion(region));
          out.globalPhase+=phase;ops[j].reset();consumed=true;
        }catch(const std::runtime_error&){ }
        break;
      }
    }
    if(!consumed)ops.push_back(std::move(op));
    if(!consumed&&isNumericalRegionBoundary(*ops.back()))++region;
  }
  for(auto& op:ops)if(op)out.instructions.push_back(std::move(*op));
  return rebuild(out);
}
Module Cleanup::run(const Module& input,const registry::ChipInfo&) const{return run(input);}
} // namespace spinor::passes

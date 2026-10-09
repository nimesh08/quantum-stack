#include "spinor/passes/CommutativeCancellation.h"
#include "spinor/passes/Cleanup.h"
#include "GateMatrices.h"
#include "spinor/passes/CompilationReport.h"
#include <optional>

namespace spinor::passes {
using namespace dialect;
namespace {
bool diagonal(OpKind k){return k==OpKind::Rz||k==OpKind::Z||k==OpKind::S||k==OpKind::Sdg||k==OpKind::T||k==OpKind::Tdg||k==OpKind::Cz||k==OpKind::Rzz;}
bool commute(const WireOp& a,const WireOp& b){
  if(isNumericalRegionBoundary(a)||isNumericalRegionBoundary(b))return false;
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
  std::size_t region=0;
  for(auto op:c.instructions){
    bool erased=false;
    std::size_t visited=0;
    if(isUnitaryInstruction(op)&&!op.qubits.empty())
      for(std::size_t j=ops.size();j-->0&&visited++<256;){
        if(!ops[j])continue;auto& prev=*ops[j];
        if(prev.qubits==op.qubits&&prev.kind==op.kind){
          try{
            double phase=op.qubits.size()==1?phaseDifference(la::mul2(matrix1(op),matrix1(prev)),la::identity2(),kRecognitionTolerance):phaseDifference(la::mul4(matrix2(op),matrix2(prev)),la::identity4(),kRecognitionTolerance);
            observeRewrite("commutative-cancellation","commuting-inverse",{prev,op},{},0,phase,kRecognitionTolerance,numericalRegion(region));
            c.globalPhase+=phase;ops[j].reset();erased=true;
          }catch(const std::runtime_error&){}
          if(erased)break;
        }
        if(!commute(op,prev))break;
      }
    if(!erased)ops.push_back(std::move(op));
    if(!erased&&isNumericalRegionBoundary(*ops.back()))++region;
  }
  c.instructions.clear();for(auto& op:ops)if(op)c.instructions.push_back(std::move(*op));
  return Cleanup{}.run(rebuild(c));
}
} // namespace spinor::passes

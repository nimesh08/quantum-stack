#include "spinor/emit/Emitters.h"
#include "spinor/dialect/Circuit.h"
#include "../../passes/lib/GateMatrices.h"
#include <iomanip>
#include <sstream>
#include <set>
#include <stdexcept>
namespace spinor::emit {
using namespace dialect;
std::string emitQuil(const Module& m) {
  auto c=flatten(m);std::ostringstream os;os<<std::setprecision(17);
  for(const auto& op:c.instructions)if(op.kind==OpKind::Move)
    throw std::runtime_error("MOVE requires IQM native JSON and has no full-unitary Quil definition");
  if(c.numClbits)os<<"DECLARE ro BIT["<<c.numClbits<<"]\n";
  // Matrix definitions retain exact phase for native gates absent from Quil's
  // standard library. Provider capability validation decides their admission.
  std::size_t serial=0,branchId=0;std::vector<std::pair<std::size_t,bool>> branches;
  for(const auto& op:c.instructions){
    if(op.kind==OpKind::If){
      auto id=branchId++;branches.emplace_back(id,false);
      os<<(parameter(op,"condition_value")==1?"JUMP-UNLESS":"JUMP-WHEN")<<" @SPINOR_ELSE_"<<id<<" ro["<<op.clbit<<"]\n";continue;
    }
    if(op.kind==OpKind::Else){auto& branch=branches.back();branch.second=true;os<<"JUMP @SPINOR_END_"<<branch.first<<"\nLABEL @SPINOR_ELSE_"<<branch.first<<'\n';continue;}
    if(op.kind==OpKind::EndIf){auto branch=branches.back();branches.pop_back();if(!branch.second)os<<"LABEL @SPINOR_ELSE_"<<branch.first<<'\n';os<<"LABEL @SPINOR_END_"<<branch.first<<'\n';continue;}
    if(op.kind==OpKind::GlobalPhase){
      os<<"# Scalar phase (radians): "<<parameter(op)<<"; retained in physical JSON.\n";continue;
    }
    if(op.kind==OpKind::Barrier){os<<"FENCE";for(int q:op.qubits)os<<' '<<q;os<<'\n';continue;}
    if(op.kind==OpKind::Measure){os<<"MEASURE "<<op.qubits.at(0)<<" ro["<<op.clbit<<"]\n";continue;}
    if(op.kind==OpKind::Reset){os<<"RESET "<<op.qubits.at(0)<<'\n';continue;}
    std::string name;
    switch(op.kind){
      case OpKind::H:name="H";break;case OpKind::X:name="X";break;case OpKind::Y:name="Y";break;case OpKind::Z:name="Z";break;
      case OpKind::S:name="S";break;case OpKind::T:name="T";break;
      case OpKind::Sdg:name="DAGGER S";break;case OpKind::Tdg:name="DAGGER T";break;
      case OpKind::Rx:name="RX";break;case OpKind::Ry:name="RY";break;case OpKind::Rz:name="RZ";break;
      case OpKind::Cx:name="CNOT";break;case OpKind::Cz:name="CZ";break;case OpKind::Swap:name="SWAP";break;case OpKind::ISwap:name="ISWAP";break;
      default:break;
    }
    if(name.empty()){
      name="SPINOR_GATE_"+std::to_string(serial++);os<<"DEFGATE "<<name<<":\n";
      auto element=[&](auto matrix,int n){for(int r=0;r<n;++r){os<<"    ";for(int k=0;k<n;++k){auto z=matrix(r,k);if(k)os<<", ";os<<'('<<z.real()<<(z.imag()<0?"":"+")<<z.imag()<<"i)";}os<<'\n';}};
      if(op.qubits.size()==1)element(passes::matrix1(op),2);
      else if(op.qubits.size()==2)element(passes::matrix2(op),4);
      else throw std::runtime_error("unsupported Quil operation");
    }
    os<<name;
    if(op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rz)os<<'('<<parameter(op)<<')';
    for(int q:op.qubits)os<<' '<<q;os<<'\n';
  }
  if(c.globalPhase!=0 && c.numQubits){
    os<<"# Scalar global phase (radians): "<<c.globalPhase<<"; retained in physical JSON.\n";
  }
  return os.str();
}
}

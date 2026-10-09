#include "spinor/passes/Decomposition.h"
#include "NativeSynthesis.h"
#include <functional>

namespace spinor::passes {
using namespace dialect;
Module Decomposition::run(const Module& input,const registry::ChipInfo& chip,Diagnostics& diag) const {
  auto in=flatten(input),out=in;out.instructions.clear();out.target=chip.id;
  auto has=[&](const std::string& s){return std::find(chip.nativeGates.begin(),chip.nativeGates.end(),s)!=chip.nativeGates.end();};
  auto one=[&](const la::Mat2& u,int q){auto seq=synthesizeOne(u,q,chip,out.globalPhase);out.instructions.insert(out.instructions.end(),seq.begin(),seq.end());};
  auto native=[&](OpKind k,int a,int b,double angle=0){
    WireOp op{k,{a,b},{},{}};if(k==OpKind::Rzz||k==OpKind::Rxx)op.attributes={angleAttr(angle)};
    out.instructions.push_back(std::move(op));
  };
  auto allowed=[&](int a,int b){
    if(!chip.directedConnectivity||chip.allToAll)return true;
    return std::find(chip.coupling.begin(),chip.coupling.end(),std::pair<int,int>{a,b})!=chip.coupling.end();
  };
  std::function<void(int,int)> cx;
  cx=[&](int a,int b){
    const auto& e=chip.decompose.twoQubitEntangler;
    if(e=="cx"){
      if(allowed(a,b))native(OpKind::Cx,a,b);
      else if(allowed(b,a)){
        one(la::H(),a);one(la::H(),b);native(OpKind::Cx,b,a);one(la::H(),a);one(la::H(),b);
      }else throw std::runtime_error("no directed CX edge");
      return;
    }
    if(e=="cz"){one(la::H(),b);native(OpKind::Cz,a,b);one(la::H(),b);return;}
    if(e=="iswap"){
      // ISWAP = SWAP * CZ * (S tensor S). This identity gives CX
      // with two native ISWAPs and local Cliffords, with no scalar phase.
      one(la::Sdg(),a);native(OpKind::ISwap,a,b);one(la::H(),a);
      native(OpKind::ISwap,a,b);one(la::Sdg(),b);one(la::H(),b);one(la::Sdg(),b);return;
    }
    if(e=="rzz"){
      one(la::H(),b);native(OpKind::Rzz,a,b,M_PI/2);
      one(la::Rz(-M_PI/2),a);one(la::Rz(-M_PI/2),b);one(la::H(),b);return;
    }
    if(e=="sqrt_iswap"||e=="sqrt_iswap_inv"){
      // X echoes cancel YY: G X G X = RXX(-pi/2) for G=sqrtISWAP.
      one(la::H(),b);one(la::H(),a);one(la::H(),b);
      one(la::X(),a);native(e=="sqrt_iswap"?OpKind::SqrtISwap:OpKind::SqrtISwapInv,a,b);
      one(la::X(),a);native(e=="sqrt_iswap"?OpKind::SqrtISwap:OpKind::SqrtISwapInv,a,b);
      one(la::H(),a);one(la::H(),b);
      double angle=e=="sqrt_iswap"?M_PI/2:-M_PI/2;
      one(la::Rz(angle),a);one(la::Rz(angle),b);one(la::H(),b);return;
    }
    if(e=="syc"){
      // SYC=FSim(pi/2,pi/6). Six powers give diag(1,-1,-1,-1);
      // Z on both wires converts this exactly to CZ.
      one(la::H(),b);for(int i=0;i<6;++i)native(OpKind::Syc,a,b);
      one(la::Z(),a);one(la::Z(),b);one(la::H(),b);return;
    }
    if(e=="ms"||e=="rxx"){
      one(la::H(),b);one(la::H(),a);one(la::H(),b);
      native(e=="ms"?OpKind::Ms:OpKind::Rxx,a,b,M_PI/2);
      one(la::H(),a);one(la::H(),b);
      one(la::Rz(-M_PI/2),a);one(la::Rz(-M_PI/2),b);one(la::H(),b);return;
    }
    if(e=="ecr"){
      bool reverse=!allowed(a,b);
      if(reverse){if(!allowed(b,a))throw std::runtime_error("no directed ECR edge");one(la::H(),a);one(la::H(),b);std::swap(a,b);}
      one(la::X(),a);native(OpKind::Ecr,a,b);
      one(la::S(),a);one(la::H(),b);one(la::S(),b);one(la::H(),b);
      if(reverse){one(la::H(),a);one(la::H(),b);}return;
    }
    throw std::runtime_error("no native CX synthesis for entangler '"+e+"'");
  };
  auto sequenceMatrix=[&](std::size_t start,int a){
    auto matrix=la::identity4();
    for(std::size_t i=start;i<out.instructions.size();++i){
      const auto& op=out.instructions[i];la::Mat4 gate;
      if(op.qubits.size()==1)gate=op.qubits[0]==a?la::kron(matrix1(op),la::identity2()):la::kron(la::identity2(),matrix1(op));
      else{gate=matrix2(op);if(op.qubits[0]!=a)gate=la::mul4(la::SWAP(),la::mul4(gate,la::SWAP()));}
      matrix=la::mul4(gate,matrix);
    }
    return matrix;
  };
  int depth=0;
  for(const auto& op:in.instructions)try{
    if(isControl(op.kind)||op.kind==OpKind::GlobalPhase){
      out.instructions.push_back(op);if(op.kind==OpKind::If)++depth;if(op.kind==OpKind::EndIf)--depth;continue;
    }
    double phaseBefore=out.globalPhase;
    auto emitOperation=[&]{
    if(op.kind==OpKind::Measure||op.kind==OpKind::Reset||op.kind==OpKind::Barrier){out.instructions.push_back(op);return;}
    std::string name(opMnemonic(op.kind));name=name.substr(7);
    bool halfXOnly=op.kind==OpKind::Rx&&chip.decompose.oneQubitPi2Gate=="rx";
    bool needsFixedX=halfXOnly&&std::abs(parameter(op)/(M_PI/2)-std::round(parameter(op)/(M_PI/2)))>1e-10;
    if(has(name)&&!needsFixedX&& !((op.kind==OpKind::Cx||op.kind==OpKind::Ecr)&&!allowed(op.qubits[0],op.qubits[1]))){out.instructions.push_back(op);return;}
    if(op.qubits.size()==1){one(matrix1(op),op.qubits[0]);return;}
    if(op.qubits.size()!=2)throw std::runtime_error("unsupported gate arity");
    int a=op.qubits[0],b=op.qubits[1];auto start=out.instructions.size();double phase=out.globalPhase;
    switch(op.kind){
      case OpKind::Cx:cx(a,b);break;
      case OpKind::Ecr:
        one(la::X(),a);cx(a,b);
        one(la::Sdg(),a);one(la::H(),b);one(la::Sdg(),b);one(la::H(),b);break;
      case OpKind::Cz:one(la::H(),b);cx(a,b);one(la::H(),b);break;
      case OpKind::Swap:cx(a,b);cx(b,a);cx(a,b);break;
      case OpKind::ISwap:
        one(la::S(),a);one(la::S(),b);one(la::H(),b);cx(a,b);one(la::H(),b);
        cx(a,b);cx(b,a);cx(a,b);break;
      case OpKind::Rzz:cx(a,b);one(la::Rz(parameter(op)),b);cx(a,b);break;
      case OpKind::Rxx:case OpKind::Ms:
        one(la::H(),a);one(la::H(),b);cx(a,b);one(la::Rz(op.kind==OpKind::Ms?M_PI/2:parameter(op)),b);cx(a,b);one(la::H(),a);one(la::H(),b);break;
      default:throw std::runtime_error("no synthesis for "+name);
    }
    out.globalPhase=phase+phaseDifference(matrix2(op),sequenceMatrix(start,a));
    };
    emitOperation();
    if(depth){
      double localPhase=out.globalPhase-phaseBefore;out.globalPhase=phaseBefore;
      if(std::abs(localPhase)>1e-13)out.instructions.push_back({OpKind::GlobalPhase,{}, {angleAttr(localPhase)},op.loc});
    }
  }catch(const std::exception& e){diag.error("decompose: "+std::string(e.what())+" on "+chip.id,op.loc);return input;}
  return rebuild(out);
}
} // namespace spinor::passes

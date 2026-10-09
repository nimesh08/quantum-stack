#pragma once
#include "Complex2x2.h"
#include "spinor/dialect/Circuit.h"
#include <stdexcept>

namespace spinor::passes {
inline la::Mat2 rxy(double theta, double phi) {
  auto u=la::Rx(theta);
  u(0,1)*=std::polar(1.0,-phi); u(1,0)*=std::polar(1.0,phi); return u;
}
inline la::Mat2 matrix1(const dialect::WireOp& op) {
  using namespace dialect; using namespace la;
  switch(op.kind) {
    case OpKind::H:return H(); case OpKind::X:return X();
    case OpKind::Y:return Y(); case OpKind::Z:return Z();
    case OpKind::S:return S(); case OpKind::Sdg:return Sdg();
    case OpKind::T:return T(); case OpKind::Tdg:return Tdg();
    case OpKind::Sx:return SX(); case OpKind::Sxdg:return SXdg();
    case OpKind::Rx:return Rx(parameter(op));
    case OpKind::Ry:return Ry(parameter(op));
    case OpKind::Rz:return Rz(parameter(op));
    case OpKind::PhasedXZ:{
      double x=parameter(op,"x"),z=parameter(op,"z"),axis=parameter(op,"axis_phase");
      auto u=mul2(Rz(z+axis),mul2(Rx(x),Rz(-axis)));
      for(auto& e:u.e)e*=std::polar(1.0,(x+z)/2);return u;
    }
    case OpKind::U1q:return rxy(parameter(op,"theta"),parameter(op,"phi"));
    case OpKind::Gpi:{ Mat2 u; u(0,1)=std::polar(1.0,-parameter(op)); u(1,0)=std::polar(1.0,parameter(op)); return u; }
    case OpKind::Gpi2:return rxy(M_PI/2,parameter(op));
    default:throw std::runtime_error("no one-qubit matrix for "+std::string(opMnemonic(op.kind)));
  }
}
inline la::Mat4 matrix2(const dialect::WireOp& op) {
  using namespace dialect; using namespace la;
  switch(op.kind) {
    case OpKind::Cx:return CX(); case OpKind::Cz:return CZ();
    case OpKind::Swap:return SWAP(); case OpKind::Ecr:return ECR();
    case OpKind::Ms:return MS(); case OpKind::Rxx:return MS(parameter(op));
    case OpKind::Rzz:return RZZ(parameter(op));
    case OpKind::ISwap:{Mat4 u;u(0,0)=u(3,3)=1;u(1,2)=u(2,1)=cdbl(0,1);return u;}
    case OpKind::SqrtISwap:case OpKind::SqrtISwapInv:{
      Mat4 u=identity4();double c=std::sqrt(0.5);u(1,1)=u(2,2)=c;
      u(1,2)=u(2,1)=cdbl(0,op.kind==OpKind::SqrtISwap?c:-c);return u;
    }
    case OpKind::Syc:{Mat4 u;u(0,0)=1;u(1,2)=u(2,1)=cdbl(0,-1);u(3,3)=std::polar(1.0,-M_PI/6);return u;}
    default:throw std::runtime_error("no two-qubit matrix for "+std::string(opMnemonic(op.kind)));
  }
}
template<class Matrix> inline double phaseDifference(const Matrix& desired,const Matrix& actual) {
  la::cdbl overlap=0;
  for(std::size_t i=0;i<desired.e.size();++i) overlap+=desired.e[i]*std::conj(actual.e[i]);
  double phase=std::arg(overlap); auto z=std::polar(1.0,phase);
  for(std::size_t i=0;i<desired.e.size();++i)
    if(std::abs(desired.e[i]-z*actual.e[i])>1e-9) throw std::runtime_error("native synthesis failed its unitary check");
  return phase;
}
} // namespace spinor::passes

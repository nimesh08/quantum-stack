#pragma once

#include "GateMatrices.h"
#include "spinor/passes/TwoQubitDecomposer.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace spinor::passes::twoq {
using namespace la;

inline Mat4 unpack(const U4& u) {
  Mat4 out;
  for (int r=0;r<4;++r) for (int c=0;c<4;++c)
    out(r,c)={u[r][c].first,u[r][c].second};
  return out;
}
inline U4 pack(const Mat4& u) {
  U4 out;
  for (int r=0;r<4;++r) for (int c=0;c<4;++c)
    out[r][c]={u(r,c).real(),u(r,c).imag()};
  return out;
}
inline Mat4 transpose(const Mat4& u) {
  Mat4 out;
  for (int r=0;r<4;++r) for (int c=0;c<4;++c) out(r,c)=u(c,r);
  return out;
}
inline Mat4 adjoint(const Mat4& u) {
  auto out=transpose(u); for(auto& x:out.e)x=std::conj(x); return out;
}
inline Mat4 scale(Mat4 u,cdbl f) { for(auto& x:u.e)x*=f;return u; }
inline double distance(const Mat4& a,const Mat4& b) {
  double d=0;for(int i=0;i<16;++i)d=std::max(d,std::abs(a.e[i]-b.e[i]));return d;
}
inline void requireUnitary(const Mat4& u) {
  for(auto x:u.e)if(!std::isfinite(x.real())||!std::isfinite(x.imag()))
    throw std::invalid_argument("two-qubit unitary contains a non-finite entry");
  if(distance(mul4(adjoint(u),u),identity4())>2e-9)
    throw std::invalid_argument("two-qubit input is not unitary");
}
inline cdbl determinant(Mat4 u) {
  cdbl d=1;
  for(int c=0;c<4;++c){
    int pivot=c;
    for(int r=c+1;r<4;++r)if(std::abs(u(r,c))>std::abs(u(pivot,c)))pivot=r;
    if(std::abs(u(pivot,c))<1e-15)return 0;
    if(pivot!=c){for(int k=0;k<4;++k)std::swap(u(c,k),u(pivot,k));d=-d;}
    const auto value=u(c,c);d*=value;
    for(int r=c+1;r<4;++r){auto f=u(r,c)/value;for(int k=c+1;k<4;++k)u(r,k)-=f*u(c,k);}
  }
  return d;
}
inline Mat4 operationMatrix(const dialect::WireOp& op,int qa=0,int qb=1) {
  if(op.qubits.size()==1){
    if(op.qubits[0]==qa)return kron(matrix1(op),identity2());
    if(op.qubits[0]==qb)return kron(identity2(),matrix1(op));
  }else if(op.qubits.size()==2){
    auto g=matrix2(op);
    if(op.qubits[0]==qa&&op.qubits[1]==qb)return g;
    if(op.qubits[0]==qb&&op.qubits[1]==qa)return mul4(SWAP(),mul4(g,SWAP()));
  }
  throw std::invalid_argument("operation lies outside its two-qubit block");
}
inline Mat4 compose(const std::vector<dialect::WireOp>& ops,int qa=0,int qb=1) {
  auto u=identity4();for(const auto& op:ops)u=mul4(operationMatrix(op,qa,qb),u);return u;
}

// Factor a tensor product up to phase. Selecting the largest entry avoids
// unstable division for zero/near-zero entries, including Pauli gates.
inline std::pair<Mat2,Mat2> tensorFactors(const Mat4& u,
                                        double tolerance=kRecognitionTolerance) {
  int pivot=0;for(int i=1;i<16;++i)if(std::abs(u.e[i])>std::abs(u.e[pivot]))pivot=i;
  int r=pivot/4,c=pivot%4;Mat2 a,b;
  for(int i=0;i<2;++i)for(int j=0;j<2;++j){
    a(i,j)=u(2*i+r%2,2*j+c%2);
    b(i,j)=u(2*(r/2)+i,2*(c/2)+j);
  }
  for(auto* factor:{&a,&b}){
    auto d=std::sqrt((*factor)(0,0)*(*factor)(1,1)-(*factor)(0,1)*(*factor)(1,0));
    if(std::abs(d)<1e-13)throw std::runtime_error("two-qubit matrix is not a local tensor product");
    for(auto& x:factor->e)x/=d;
  }
  phaseDifference(u,kron(a,b),tolerance);
  return {a,b};
}

inline std::vector<std::size_t> instructionIndices(const dialect::Module& m) {
  std::vector<std::size_t> indices(m.numOps(),std::numeric_limits<std::size_t>::max());
  std::size_t next=0;
  for(std::uint32_t i=0;i<m.numOps();++i){auto k=m.op(dialect::OpId{i}).kind;
    if(k!=dialect::OpKind::AllocQubit&&k!=dialect::OpKind::AllocBit)indices[i]=next++;
  }
  return indices;
}
} // namespace spinor::passes::twoq

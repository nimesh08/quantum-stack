// Exact two-qubit Cartan synthesis. The magic-basis construction is the
// SU(2) x SU(2) / SO(4) correspondence; see Vatan and Williams,
// https://arxiv.org/abs/quant-ph/0308006. No provider compiler is used.
#include "spinor/passes/TwoQubitDecomposer.h"
#include "spinor/passes/Decomposition.h"
#include "NativeSynthesis.h"
#include "TwoQubitMath.h"
#include <array>
#include <bit>
#include <numbers>
#include <tuple>

namespace spinor::passes {
namespace {
using namespace dialect;
using namespace la;
using namespace twoq;
constexpr double pi=std::numbers::pi;
constexpr double zeroTolerance=2e-11;

Mat4 magicBasis() {
  Mat4 m;const double s=1/std::sqrt(2.0);const cdbl is(0,s);
  m(0,0)=s;m(0,3)=is;m(1,1)=is;m(1,2)=s;
  m(2,1)=is;m(2,2)=-s;m(3,0)=s;m(3,3)=-is;
  return m;
}

// Real symmetric Jacobi diagonalization, with a fixed iteration bound.
// Columns of v are eigenvectors. Largest-pivot selection is deterministic.
Mat4 jacobi(Mat4 a) {
  auto v=identity4();
  for(int iteration=0;iteration<160;++iteration){
    int p=0,q=1;double largest=0;
    for(int r=0;r<4;++r)for(int c=r+1;c<4;++c)
      if(std::abs(a(r,c).real())>largest){largest=std::abs(a(r,c).real());p=r;q=c;}
    if(largest<2e-15)break;
    const double app=a(p,p).real(),aqq=a(q,q).real(),apq=a(p,q).real();
    const double tau=(aqq-app)/(2*apq);
    const double t=std::copysign(1.0,tau)/(std::abs(tau)+std::hypot(1.0,tau));
    const double c=1/std::hypot(1.0,t),s=t*c;
    a(p,p)=app-t*apq;a(q,q)=aqq+t*apq;a(p,q)=a(q,p)=0;
    for(int k=0;k<4;++k)if(k!=p&&k!=q){
      const double kp=a(k,p).real(),kq=a(k,q).real();
      a(k,p)=a(p,k)=c*kp-s*kq;a(k,q)=a(q,k)=s*kp+c*kq;
    }
    for(int k=0;k<4;++k){auto kp=v(k,p),kq=v(k,q);v(k,p)=c*kp-s*kq;v(k,q)=s*kp+c*kq;}
  }
  return v;
}

struct Cartan {
  Mat2 before0,before1,after0,after1;
  double x=0,y=0,z=0;
};
int centralEntanglers(double x,double y,double z,bool parametric) {
  auto nonzero=[](double a){return std::abs(a)>zeroTolerance;};
  if(parametric)return int(nonzero(x))+int(nonzero(y))+int(nonzero(z));
  if(!nonzero(x)&&!nonzero(y)&&!nonzero(z))return 0;
  if(!nonzero(y)&&!nonzero(z)&&std::abs(std::abs(x)-pi/4)<zeroTolerance)return 1;
  return !nonzero(z)?2:3;
}

Cartan cartan(const Mat4& input,bool parametric) {
  const auto magic=magicBasis();
  const auto normalized=scale(input,std::polar(1.0,-std::arg(determinant(input))/4));
  const auto b=mul4(adjoint(magic),mul4(normalized,magic));
  const auto symmetric=mul4(transpose(b),b);
  Mat4 vectors;double bestResidual=1e100;
  // Real and imaginary parts commute. A real linear combination separates
  // their joint eigenspaces. There are only six distinct pair collisions:
  // eight fixed, different weights suffice, with residual verification.
  for(double weight:{0.0,1.0,-1.0,std::sqrt(2.0),std::sqrt(3.0),
                      std::sqrt(5.0),std::sqrt(7.0),pi}){
    Mat4 real;
    for(int i=0;i<16;++i)real.e[i]=symmetric.e[i].real()+weight*symmetric.e[i].imag();
    auto v=jacobi(real);
    const auto diagonal=mul4(transpose(v),mul4(symmetric,v));
    double residual=0;
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(r!=c)residual=std::max(residual,std::abs(diagonal(r,c)));
    if(residual<bestResidual){bestResidual=residual;vectors=v;}
    if(residual<2e-13)break;
  }
  if(bestResidual>2e-10)throw std::runtime_error("Cartan joint eigensystem did not converge");
  const auto eig=mul4(transpose(vectors),mul4(symmetric,vectors));
  std::array<int,4> order{0,1,2,3};
  bool found=false;Cartan best;
  std::tuple<int,double,double,double> bestScore{100,1e100,1e100,1e100};
  do {
    Mat4 v;
    for(int r=0;r<4;++r)for(int c=0;c<4;++c)v(r,c)=vectors(r,order[c]);
    if(determinant(v).real()<0)for(int r=0;r<4;++r)v(r,0)=-v(r,0);
    for(unsigned signs=0;signs<16;++signs){
      std::array<cdbl,4> roots;
      std::array<double,4> d;
      cdbl product=1;
      for(int i=0;i<4;++i){
        roots[i]=std::polar(1.0,std::arg(eig(order[i],order[i]))/2);
        if(signs&(1u<<i))roots[i]=-roots[i];
        product*=roots[i];d[i]=std::arg(roots[i]);
      }
      if(std::abs(product-cdbl(1))>1e-8)continue;
      double x=(d[0]+d[1]-d[2]-d[3])/4;
      double y=(-d[0]+d[1]-d[2]+d[3])/4;
      double z=(d[0]-d[1]-d[2]+d[3])/4;
      // Select a Weyl representative with |z| <= |y| <= |x| <= pi/4.
      if(std::abs(x)>pi/4+zeroTolerance||std::abs(y)>std::abs(x)+zeroTolerance||
         std::abs(z)>std::abs(y)+zeroTolerance)continue;
      for(double* a:{&x,&y,&z})if(std::abs(*a)<zeroTolerance)*a=0;
      auto score=std::tuple{centralEntanglers(x,y,z,parametric),std::abs(z),std::abs(y),std::abs(x)};
      if(found&&score>=bestScore)continue;
      auto left=mul4(b,v);
      for(int r=0;r<4;++r)for(int c=0;c<4;++c)left(r,c)/=roots[c];
      double imaginary=0;for(auto value:left.e)imaginary=std::max(imaginary,std::abs(value.imag()));
      if(imaginary>2e-9)continue;
      auto after=tensorFactors(mul4(magic,mul4(left,adjoint(magic))));
      auto before=tensorFactors(mul4(magic,mul4(transpose(v),adjoint(magic))));
      best={before.first,before.second,after.first,after.second,x,y,z};
      bestScore=score;found=true;
    }
  } while(std::next_permutation(order.begin(),order.end()));
  if(!found)throw std::runtime_error("Cartan factorization has no validated Weyl representative");
  return best;
}

void add(std::vector<WireOp>& ops,OpKind kind,int q,double angle=0) {
  WireOp op{kind,{q},{},{}};
  if(kind==OpKind::Rx||kind==OpKind::Ry||kind==OpKind::Rz)op.attributes={angleAttr(angle)};
  ops.push_back(std::move(op));
}
void entangle(std::vector<WireOp>& ops,OpKind kind,int a,int b,double angle=0) {
  WireOp op{kind,{a,b},{},{}};
  if(kind==OpKind::Rxx||kind==OpKind::Rzz)op.attributes={angleAttr(angle)};
  ops.push_back(std::move(op));
}
void central(std::vector<WireOp>& ops,const Cartan& k,const std::string& basis) {
  const auto x=k.x,y=k.y,z=k.z;
  if(basis=="rzz"||basis=="rxx"){
    auto interaction=[&](double angle,int axis){
      if(std::abs(angle)<zeroTolerance)return;
      if(basis=="rzz"){
        if(axis==0){add(ops,OpKind::H,0);add(ops,OpKind::H,1);}
        if(axis==1){add(ops,OpKind::Rx,0,-pi/2);add(ops,OpKind::Rx,1,-pi/2);}
        entangle(ops,OpKind::Rzz,0,1,-2*angle);
        if(axis==0){add(ops,OpKind::H,0);add(ops,OpKind::H,1);}
        if(axis==1){add(ops,OpKind::Rx,0,pi/2);add(ops,OpKind::Rx,1,pi/2);}
      }else{
        if(axis==1){add(ops,OpKind::Sdg,0);add(ops,OpKind::Sdg,1);}
        if(axis==2){add(ops,OpKind::H,0);add(ops,OpKind::H,1);}
        entangle(ops,OpKind::Rxx,0,1,-2*angle);
        if(axis==1){add(ops,OpKind::S,0);add(ops,OpKind::S,1);}
        if(axis==2){add(ops,OpKind::H,0);add(ops,OpKind::H,1);}
      }
    };
    interaction(x,0);interaction(y,1);interaction(z,2);return;
  }
  const auto count=centralEntanglers(x,y,z,false);
  if(count==0)return;
  if(count==1){
    // exp(i*x*XX), |x|=pi/4: locally equivalent to a single CX.
    add(ops,OpKind::H,0);add(ops,OpKind::H,1);
    add(ops,OpKind::Rz,0,-2*x);add(ops,OpKind::Rz,1,-2*x);
    add(ops,OpKind::H,1);entangle(ops,OpKind::Cx,0,1);
    add(ops,OpKind::H,1);add(ops,OpKind::H,0);add(ops,OpKind::H,1);
  }else if(count==2){
    add(ops,OpKind::Rx,0,pi/2);entangle(ops,OpKind::Cx,0,1);
    add(ops,OpKind::Rx,0,-2*x);add(ops,OpKind::Ry,1,-2*y);
    entangle(ops,OpKind::Cx,0,1);add(ops,OpKind::Rx,0,-pi/2);
  }else{
    // Three-CX Cartan identity, valid for all real x,y,z; its constant
    // global phase is recovered by the final matrix comparison.
    add(ops,OpKind::Rx,0,pi/2);entangle(ops,OpKind::Cx,0,1);
    add(ops,OpKind::Rx,0,pi/2-2*x);add(ops,OpKind::Ry,1,pi/2-2*y);
    entangle(ops,OpKind::Cx,1,0);add(ops,OpKind::Rx,1,-pi/2);
    add(ops,OpKind::Rz,1,pi/2-2*z);entangle(ops,OpKind::Cx,0,1);
  }
}

std::vector<WireOp> mergeLocalLayers(const std::vector<WireOp>& ops,const registry::ChipInfo& chip) {
  std::array<Mat2,2> pending{identity2(),identity2()};std::vector<WireOp> out;
  auto flush=[&]{
    for(int q=0;q<2;++q){double unusedPhase=0;auto seq=synthesizeOne(pending[q],q,chip,unusedPhase);
      out.insert(out.end(),seq.begin(),seq.end());pending[q]=identity2();}
  };
  for(const auto& op:ops){
    if(op.qubits.size()==1)pending[op.qubits[0]]=mul2(matrix1(op),pending[op.qubits[0]]);
    else{flush();out.push_back(op);}
  }
  flush();return out;
}
} // namespace

KakResult TwoQubitDecomposer::decompose(const U4& matrix,const SynthesisTraits& traits,
                                       const registry::ChipInfo* twoWireChip) const {
  auto u=twoq::unpack(matrix);twoq::requireUnitary(u);
  registry::ChipInfo chip;
  if(twoWireChip)chip=*twoWireChip;
  else {chip.id="kak";chip.qubits=2;chip.allToAll=true;chip.nativeGates=traits.nativeGates;
    chip.decompose.twoQubitEntangler=traits.entanglerName;
    chip.decompose.oneQubitRotationGate=traits.rotationGate;
    chip.decompose.oneQubitPi2Gate=traits.pi2Gate;}
  if(chip.nativeGates.empty())throw std::invalid_argument("KAK requires an explicit native gate set");
  std::vector<WireOp> operations;
  auto locals=[&](const Mat2& a,const Mat2& b){
    for(int q=0;q<2;++q){double unusedPhase=0;auto seq=synthesizeOne(q==0?a:b,q,chip,unusedPhase);
      operations.insert(operations.end(),seq.begin(),seq.end());}
  };
  bool local=false;
  try {auto factors=twoq::tensorFactors(u);locals(factors.first,factors.second);local=true;}
  catch(const std::runtime_error&){operations.clear();}
  if(!local){
    const auto k=cartan(u,traits.entanglerName=="rxx"||traits.entanglerName=="rzz");
    locals(k.before0,k.before1);central(operations,k,traits.entanglerName);locals(k.after0,k.after1);
  }
  // Validate the logical factorization before any native lowering. This
  // catches ordering, magic-basis, and phase-root mistakes independently.
  phaseDifference(u,twoq::compose(operations));
  WireCircuit circuit{"kak",chip.id,2,0,0,operations};
  Diagnostics diagnostics;
  auto native=flatten(Decomposition{}.run(rebuild(circuit),chip,diagnostics));
  if(diagnostics.hasErrors())throw std::runtime_error("KAK native basis synthesis failed");
  // AQT's native Rxx interval is [0, pi/2]. A negative Cartan angle
  // needs one entangler, not a wrapped 2*pi-angle split into four gates:
  // Z(a) Rxx(theta) Z(a) = Rxx(-theta). Include both local corrections
  // before measuring replacement cost, then merge them with adjacent layers.
  std::vector<WireOp> bounded;
  for(auto op:native.instructions){
    if(op.kind!=OpKind::Rxx){bounded.push_back(std::move(op));continue;}
    double theta=parameter(op);
    const bool negative=theta<0;
    theta=std::abs(theta);
    if(std::abs(theta-pi/2)<4*zeroTolerance)theta=pi/2;
    if(negative)add(bounded,OpKind::Z,op.qubits[0]);
    op.attributes={angleAttr(theta)};
    bounded.push_back(op);
    if(negative)add(bounded,OpKind::Z,op.qubits[0]);
  }
  auto output=mergeLocalLayers(bounded,chip);
  KakResult result;
  result.globalPhase=phaseDifference(u,twoq::compose(output));
  for(const auto& op:output)if(op.qubits.size()==2)++result.entanglerUses;
  if(result.entanglerUses>traits.entanglerCountMax)
    throw std::runtime_error("exact KAK result exceeds the configured entangler budget");
  result.operations=std::move(output);
  return result;
}
} // namespace spinor::passes

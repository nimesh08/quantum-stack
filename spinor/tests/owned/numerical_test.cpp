// Independent Pauli/projector definitions; deliberately no compiler matrix,
// equivalence, simulation, or synthesis helpers form the expected operators.
#include "spinor/passes/PassManager.h"
#include "spinor/dialect/Circuit.h"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <iomanip>
#include <numbers>
#include <stdexcept>
using namespace spinor;
using namespace spinor::dialect;
using C=std::complex<double>;
using M2=std::array<C,4>;
using M4=std::array<C,16>;
constexpr double pi=std::numbers::pi;
M2 one(const WireOp& op) {
  const double a=parameter(op),c=std::cos(a/2),s=std::sin(a/2);
  switch(op.kind) {
    case OpKind::Rx:return {c,C(0,-s),C(0,-s),c};
    case OpKind::Ry:return {c,-s,s,c};
    case OpKind::Rz:return {std::exp(C(0,-a/2)),0,0,std::exp(C(0,a/2))};
    case OpKind::Gpi:return {0,std::exp(C(0,-a)),std::exp(C(0,a)),0};
    case OpKind::Gpi2:return {1/std::sqrt(2.),C(0,-1)*std::exp(C(0,-a))/std::sqrt(2.),C(0,-1)*std::exp(C(0,a))/std::sqrt(2.),1/std::sqrt(2.)};
    case OpKind::U1q:{
      const double theta=parameter(op,"theta"),phi=parameter(op,"phi");
      return {std::cos(theta/2),C(0,-std::sin(theta/2))*std::exp(C(0,-phi)),C(0,-std::sin(theta/2))*std::exp(C(0,phi)),std::cos(theta/2)};
    }
    case OpKind::PhasedXZ:{
      const C ex=std::exp(C(0,parameter(op,"x"))),ez=std::exp(C(0,parameter(op,"z"))),ea=std::exp(C(0,parameter(op,"axis_phase")));
      // Projectors P+ + exp(ix) P-, with equatorial axis n=(cos(a),sin(a),0),
      // followed by diag(1,exp(iz)). This fixes the complete scalar phase.
      return {(C(1)+ex)/2.,(C(1)-ex)*std::conj(ea)/2.,ez*(C(1)-ex)*ea/2.,ez*(C(1)+ex)/2.};
    }
    default:throw std::runtime_error("unexpected one-qubit audit operation");
  }
}
M4 identity(){M4 u{};for(int i=0;i<4;++i)u[5*i]=1;return u;}
M4 multiply(const M4& a,const M4& b){M4 out{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)out[4*r+c]+=a[4*r+k]*b[4*k+c];return out;}
M4 complete(const WireCircuit& circuit){
  M4 u=identity();
  for(const auto& op:circuit.instructions){
    if(op.kind==OpKind::GlobalPhase){for(auto& entry:u)entry*=std::exp(C(0,parameter(op)));continue;}
    M4 g{};
    if(op.qubits.size()==1){
      const auto m=one(op);const int wire=op.qubits[0];
      for(int col=0;col<4;++col)for(int r=0;r<2;++r){const int bit=(col>>(1-wire))&1,row=(col&~(1<<(1-wire)))|(r<<(1-wire));g[4*row+col]=m[2*r+bit];}
    }else if(op.kind==OpKind::Rxx){
      for(int col=0;col<4;++col){g[5*col]=std::cos(parameter(op)/2);g[4*(col^3)+col]=C(0,-std::sin(parameter(op)/2));}
    }else if(op.kind==OpKind::Rzz){
      for(int col=0;col<4;++col)g[5*col]=std::exp(C(0,((col==0||col==3)?-1:1)*parameter(op)/2));
    }else if(op.kind==OpKind::Cz){
      g=identity();g[15]=-1;
    }else if(op.kind==OpKind::Cx){
      for(int col=0;col<4;++col){int row=col;if((col>>(1-op.qubits[0]))&1)row^=1<<(1-op.qubits[1]);g[4*row+col]=1;}
    }else throw std::runtime_error("unexpected two-qubit audit operation");
    u=multiply(g,u);
  }
  for(auto& entry:u)entry*=std::exp(C(0,circuit.globalPhase));
  return u;
}
int main()try{
  registry::ChipInfo chip;chip.id="independent_numerical";chip.qubits=2;chip.allToAll=true;
  chip.nativeGates={"rx","ry","rz","gpi","gpi2","u1q","phased_xz","rxx","rzz","cx","cz"};
  chip.decompose.twoQubitEntangler="cx";
  std::size_t checks=0;double worst=0;
  auto check=[&](const WireCircuit& source,double tolerance=1e-9){
    const auto expected=complete(source);
    auto declared=source;declared.target="generic";
    for(auto level:{passes::OptimizationLevel::O0,passes::OptimizationLevel::O1,passes::OptimizationLevel::O2,passes::OptimizationLevel::O3}){
      Diagnostics diagnostics;const auto result=passes::PassManager{}.compile(rebuild(declared),chip,level,diagnostics);
      if(diagnostics.hasErrors()){
        for(const auto& error:diagnostics.items())std::cerr<<error.message<<'\n';
        std::cerr<<"gate "<<opMnemonic(source.instructions[0].kind)<<" angle "<<parameter(source.instructions[0])<<" level "<<int(level)<<'\n';
        throw std::runtime_error("finite-angle compilation rejected");
      }
      const auto actual=complete(flatten(result));
      for(int i=0;i<16;++i){
        const double error=std::abs(actual[i]-expected[i]);worst=std::max(worst,error);
        if(error>tolerance){
          std::cerr<<std::setprecision(17)<<"matrix entry error "<<error<<" at O"<<int(level)<<" from";
          for(const auto& op:source.instructions)std::cerr<<' '<<opMnemonic(op.kind)<<'('<<parameter(op)<<')';
          std::cerr<<'\n';throw std::runtime_error("finite-angle complete matrix changed");
        }
      }
      ++checks;
    }
  };
  for(double magnitude:{1e5,1e8,1e12,1e16,1e20,1e100,1e300})for(double sign:{-1.,1.}){
    const double angle=magnitude*sign;
    for(auto kind:{OpKind::Rx,OpKind::Ry,OpKind::Rz,OpKind::Gpi,OpKind::Gpi2,OpKind::Rxx,OpKind::Rzz}){
      WireCircuit circuit;circuit.numQubits=2;
      circuit.instructions={{kind,kind==OpKind::Rxx||kind==OpKind::Rzz?std::vector<int>{0,1}:std::vector<int>{0},{angleAttr(angle)}, {}}};
      check(circuit);
      if(kind==OpKind::Rx||kind==OpKind::Ry||kind==OpKind::Rz){circuit.instructions.push_back({kind,{0},{angleAttr(1)}, {}});check(circuit);}
    }
    WireCircuit u1q;u1q.numQubits=2;u1q.instructions={{OpKind::U1q,{0},{namedDouble("theta",angle),namedDouble("phi",angle)}, {}}};check(u1q);
    WireCircuit phased;phased.numQubits=2;phased.instructions={{OpKind::PhasedXZ,{0},{namedDouble("x",angle),namedDouble("z",.3),namedDouble("axis_phase",-.7)}, {}}};check(phased);
    phased.instructions[0].attributes={namedDouble("x",.3),namedDouble("z",angle),namedDouble("axis_phase",angle)};check(phased);
    phased.globalPhase=angle;check(phased);
    WireCircuit scalar;scalar.numQubits=2;scalar.instructions={{OpKind::GlobalPhase,{}, {angleAttr(angle)},{}},{OpKind::GlobalPhase,{}, {angleAttr(1)}, {}}};check(scalar);
  }
  // Spinor's SU(2) period is 4*pi: reducing 2*pi must retain the minus sign.
  for(double angle:{-4*pi,-2*pi,-pi,0.,pi,2*pi,4*pi}){
    WireCircuit circuit;circuit.numQubits=2;circuit.instructions={{OpKind::Rx,{0},{angleAttr(angle)}, {}}};check(circuit);
  }
  // A reconstruction guard is not a license to erase a small interaction or
  // nearly inverse pair. These 1e-12..1e-8 effects exceed matrix roundoff.
  for(double delta:{1e-8,1e-10,1e-12}){
    WireCircuit c;c.numQubits=2;
    c.instructions={{OpKind::Gpi,{0},{angleAttr(0)},{}},{OpKind::Gpi,{0},{angleAttr(delta)}, {}}};check(c,2e-13);
    c.instructions={{OpKind::Rx,{0},{angleAttr(delta)}, {}}};check(c,2e-13);
    c.instructions={{OpKind::Rxx,{0,1},{angleAttr(delta)}, {}}};check(c,2e-13);
    c.instructions={{OpKind::Rxx,{0,1},{angleAttr(pi-delta)},{}},{OpKind::Rzz,{0,1},{angleAttr(pi-delta)}, {}}};check(c,2e-13);
    c.instructions={{OpKind::Rz,{0},{angleAttr(pi+delta)},{}},{OpKind::Cz,{0,1},{},{}},{OpKind::Rz,{0},{angleAttr(pi)}, {}}};check(c,2e-13);
  }
  std::cout<<checks<<" independent full-operator checks; max entry error "<<worst<<'\n';return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

#pragma once
#include "GateMatrices.h"
#include "spinor/registry/Registry.h"
#include <algorithm>

namespace spinor::passes {
// Own, exact SU(2) Euler synthesis. All angles are radians.
inline std::vector<dialect::WireOp> synthesizeOne(const la::Mat2& desired,int q,
                                               const registry::ChipInfo& chip,double& phase) {
  using namespace dialect; using namespace la;
  std::vector<WireOp> result;
  auto has=[&](const std::string& s){return std::find(chip.nativeGates.begin(),chip.nativeGates.end(),s)!=chip.nativeGates.end();};
  auto add=[&](OpKind k,std::vector<Attribute> a={}){result.push_back({k,{q},std::move(a),{}});};
  auto determinant=desired(0,0)*desired(1,1)-desired(0,1)*desired(1,0);
  auto removePhase=std::polar(1.0,-std::arg(determinant)/2);
  Mat2 u=desired; for(auto& x:u.e)x*=removePhase;
  double beta=2*std::atan2(std::abs(u(1,0)),std::abs(u(0,0))),alpha=0,gamma=0;
  if(std::abs(u(1,0))<kRecognitionTolerance)gamma=-2*std::arg(u(0,0));
  else if(std::abs(u(0,0))<kRecognitionTolerance)gamma=2*std::arg(u(1,0));
  else {gamma=std::arg(u(1,0))-std::arg(u(0,0));alpha=-std::arg(u(1,0))-std::arg(u(0,0));}
  if(has("phased_xz")){
    add(OpKind::PhasedXZ,{namedDouble("x",beta),namedDouble("z",gamma+alpha),namedDouble("axis_phase",M_PI/2-alpha)});
    phase+=phaseDifference(desired,matrix1(result.front()));return result;
  }
  auto z=[&](double a){
    if(std::abs(a)<kRecognitionTolerance)return;
    if(has("rz"))add(OpKind::Rz,{angleAttr(a)});
    else if(has("gpi")){add(OpKind::Gpi,{angleAttr(0)});add(OpKind::Gpi,{angleAttr(a/2)});}
    else if(has("u1q")){add(OpKind::U1q,{namedDouble("theta",M_PI),namedDouble("phi",0)});add(OpKind::U1q,{namedDouble("theta",M_PI),namedDouble("phi",a/2)});}
    else throw std::runtime_error("target has no supported Z rotation synthesis basis");
  };
  // A single calibrated half-X suffices when the middle Euler angle is pi/2.
  // This includes Hadamard and avoids an unnecessary second half-X pulse.
  if(std::abs(beta-M_PI/2)<kRecognitionTolerance && !has("u1q") && !has("ry") &&
     (has("sx")||has("rx")||has("gpi2"))){
    z(alpha-M_PI/2);
    if(has("sx"))add(OpKind::Sx);
    else if(has("rx"))add(OpKind::Rx,{angleAttr(M_PI/2)});
    else add(OpKind::Gpi2,{angleAttr(0)});
    z(gamma+M_PI/2);
    auto actual=identity2();for(const auto& op:result)actual=mul2(matrix1(op),actual);
    phase+=phaseDifference(desired,actual);return result;
  }
  z(alpha);
  if(std::abs(beta)>kRecognitionTolerance){
    if(has("u1q"))add(OpKind::U1q,{namedDouble("theta",beta),namedDouble("phi",M_PI/2)});
    else if(has("ry"))add(OpKind::Ry,{angleAttr(beta)});
    else {
      auto halfX=[&]{
        if(has("sx"))add(OpKind::Sx);
        else if(has("rx"))add(OpKind::Rx,{angleAttr(M_PI/2)});
        else if(has("gpi2"))add(OpKind::Gpi2,{angleAttr(0)});
        else throw std::runtime_error("target has no supported universal one-qubit basis");
      };
      halfX();z(beta+M_PI);halfX();gamma+=M_PI;
    }
  }
  z(gamma);
  auto actual=identity2();for(const auto& op:result)actual=mul2(matrix1(op),actual);
  phase+=phaseDifference(desired,actual);
  return result;
}
} // namespace spinor::passes

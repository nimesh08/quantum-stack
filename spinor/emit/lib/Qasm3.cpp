#include "spinor/emit/Emitters.h"
#include "spinor/dialect/Circuit.h"
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace spinor::emit {
using namespace dialect;
std::string emitQasm3(const Module& m,const registry::ChipInfo* chip,EmitOptions opts) {
  auto c=flatten(m);std::ostringstream os;os<<std::setprecision(17);
  os<<"OPENQASM 3.0;\n";
  if(!opts.braketVerbatim){
    os<<"include \"stdgates.inc\";\n";
    std::set<OpKind> kinds;for(const auto& op:c.instructions)kinds.insert(op.kind);
    // Stable alphabetical formal names also interoperate with importers that
    // bind custom-gate parameters by their internal sorted parameter list.
    if(kinds.count(OpKind::PhasedXZ))os<<"gate phased_xz(a_x,b_z,c_axis) q { gphase((a_x+b_z)/2); rz(-c_axis) q; rx(a_x) q; rz(c_axis+b_z) q; }\n";
    if(kinds.count(OpKind::U1q)||kinds.count(OpKind::Gpi)||kinds.count(OpKind::Gpi2))
      os<<"gate u1q(a_theta, b_phi) a { rz(-b_phi) a; rx(a_theta) a; rz(b_phi) a; }\n";
    if(kinds.count(OpKind::Gpi))os<<"gate gpi(phi) a { gphase(pi/2); u1q(pi, phi) a; }\n";
    if(kinds.count(OpKind::Gpi2))os<<"gate gpi2(phi) a { u1q(pi/2, phi) a; }\n";
    if(kinds.count(OpKind::Rzz))os<<"gate rzz(theta) a, b { cx a,b; rz(theta) b; cx a,b; }\n";
    if(kinds.count(OpKind::Rxx)||kinds.count(OpKind::Ms)||kinds.count(OpKind::SqrtISwap)||kinds.count(OpKind::SqrtISwapInv)||kinds.count(OpKind::Syc))os<<"gate rxx(theta) a, b { h a; h b; cx a,b; rz(theta) b; cx a,b; h a; h b; }\n";
    if(kinds.count(OpKind::SqrtISwap))os<<"gate sqrt_iswap a,b { rxx(-pi/4) a,b; s a; s b; rxx(-pi/4) a,b; sdg a; sdg b; }\n";
    if(kinds.count(OpKind::SqrtISwapInv)||kinds.count(OpKind::Syc))os<<"gate sqrt_iswap_inv a,b { rxx(pi/4) a,b; s a; s b; rxx(pi/4) a,b; sdg a; sdg b; }\n";
    if(kinds.count(OpKind::Syc))os<<"gate syc a,b { sqrt_iswap_inv a,b; sqrt_iswap_inv a,b; cp(-pi/6) a,b; }\n";
    if(kinds.count(OpKind::ISwap))os<<"gate iswap a,b { s a; s b; cz a,b; swap a,b; }\n";
    if(kinds.count(OpKind::Ms))os<<"gate ms a,b { rxx(pi/2) a,b; }\n";
    if(kinds.count(OpKind::Ecr))os<<"gate ecr a,b { gphase(pi/4); x a; cx a,b; sdg a; h b; sdg b; h b; }\n";
    if(c.numQubits)os<<"qubit["<<c.numQubits<<"] q;\n";
  }
  if(c.numClbits)os<<"bit["<<c.numClbits<<"] c;\n";
  auto qref=[&](int q){return opts.braketVerbatim?"$"+std::to_string(q):"q["+std::to_string(q)+"]";};
  if(opts.braketVerbatim)os<<"#pragma braket verbatim\nbox {\n";
  if(c.globalPhase!=0){
    if(opts.braketVerbatim)os<<"// Scalar global phase (radians): "<<c.globalPhase<<"; retained in physical JSON.\n";
    else os<<"gphase("<<c.globalPhase<<");\n";
  }
  bool measured=false;
  if(opts.braketVerbatim)for(const auto& op:c.instructions){
    if(isControl(op.kind))throw std::runtime_error("Braket verbatim output does not support dynamic control flow");
    if(op.kind==OpKind::Measure)measured=true;
    else if(measured&&op.kind!=OpKind::Barrier&&op.kind!=OpKind::GlobalPhase)throw std::runtime_error("Braket verbatim output requires terminal measurements");
  }
  for(const auto& op:c.instructions){
    if(opts.braketVerbatim&&op.kind==OpKind::Measure)continue;
    if(op.kind==OpKind::If){os<<"if (c["<<op.clbit<<"] == "<<parameter(op,"condition_value")<<") {\n";continue;}
    if(op.kind==OpKind::Else){os<<"} else {\n";continue;}
    if(op.kind==OpKind::EndIf){os<<"}\n";continue;}
    if(op.kind==OpKind::GlobalPhase){
      if(opts.braketVerbatim)os<<"// Scalar global phase (radians): "<<parameter(op)<<"\n";
      else os<<"gphase("<<parameter(op)<<");\n";continue;
    }
    if(op.kind==OpKind::Measure){os<<"c["<<op.clbit<<"] = measure "<<qref(op.qubits.at(0))<<";\n";continue;}
    if(op.kind==OpKind::Barrier){os<<"barrier";for(std::size_t i=0;i<op.qubits.size();++i)os<<(i?", ":" ")<<qref(op.qubits[i]);os<<";\n";continue;}
    auto name=std::string(opMnemonic(op.kind)).substr(7);
    if(op.kind!=OpKind::Reset && qubitArity(op.kind)<=0)throw std::runtime_error("OpenQASM cannot emit "+name);
    if(opts.braketVerbatim&&op.kind==OpKind::Rzz)name="zz";
    if(opts.braketVerbatim&&op.kind==OpKind::Rxx)name="xx";
    if(opts.braketVerbatim&&op.kind==OpKind::U1q&&chip&&(chip->vendor=="iqm"||chip->provider=="iqm"||chip->vendor=="aqt"||chip->provider=="aqt"))name="prx";
    os<<name;
    if(opts.braketVerbatim&&op.kind==OpKind::Ms)os<<"(0, 0, "<<std::acos(-1.0)/2<<")";
    else if(op.kind==OpKind::PhasedXZ)os<<'('<<parameter(op,"x")<<", "<<parameter(op,"z")<<", "<<parameter(op,"axis_phase")<<')';
    else if(op.kind==OpKind::U1q)os<<'('<<parameter(op,"theta")<<", "<<parameter(op,"phi")<<')';
    else if(op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rz||op.kind==OpKind::Rxx||op.kind==OpKind::Rzz||op.kind==OpKind::Gpi||op.kind==OpKind::Gpi2)os<<'('<<parameter(op)<<')';
    for(std::size_t i=0;i<op.qubits.size();++i)os<<(i?", ":" ")<<qref(op.qubits[i]);
    os<<";\n";
  }
  if(opts.braketVerbatim){
    os<<"}\n";
    for(const auto& op:c.instructions)if(op.kind==OpKind::Measure)os<<"c["<<op.clbit<<"] = measure "<<qref(op.qubits.at(0))<<";\n";
  }
  return os.str();
}
}

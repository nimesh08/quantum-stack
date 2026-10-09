// spinor/sim/lib/Simulator.cpp
//
// Statevector simulator + equivalence + resource estimator.

#include "spinor/sim/Simulator.h"
#include "spinor/dialect/Resonators.h"
#include "spinor/dialect/Classical.h"
#include <unordered_set>

#include "../../passes/lib/GateMatrices.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace spinor::sim {

namespace {

using namespace spinor::dialect;
using spinor::passes::la::Mat2;
using spinor::passes::la::Mat4;
using spinor::passes::la::cdbl;
using spinor::passes::la::Rx;
using spinor::passes::la::Ry;
using spinor::passes::la::Rz;
using spinor::passes::la::H;
using spinor::passes::la::X;
using spinor::passes::la::Y;
using spinor::passes::la::Z;
using spinor::passes::la::S;
using spinor::passes::la::Sdg;
using spinor::passes::la::T;
using spinor::passes::la::Tdg;
using spinor::passes::la::SX;
using spinor::passes::la::SXdg;
using spinor::passes::la::CX;
using spinor::passes::la::CZ;
using spinor::passes::la::SWAP;
using spinor::passes::la::ECR;
using spinor::passes::la::MS;
using spinor::passes::la::RZZ;

// Apply a 2×2 unitary to qubit `q` (0-indexed) in-place.
void apply1q(StateVector& sv, int q, const Mat2& u) {
  std::size_t n = sv.qubits;
  std::size_t mask = std::size_t(1) << q;
  for (std::size_t i = 0; i < (std::size_t(1) << n); ++i) {
    if (i & mask) continue;  // visit pairs (i, i|mask)
    std::size_t j = i | mask;
    cdbl a = sv.amps[i];
    cdbl b = sv.amps[j];
    sv.amps[i] = u(0, 0) * a + u(0, 1) * b;
    sv.amps[j] = u(1, 0) * a + u(1, 1) * b;
  }
}

// Apply a 4×4 unitary to qubits (qa, qb). Convention: bit qa is
// the higher-order qubit (operand 0 in the matrix's row index).
void apply2q(StateVector& sv, int qa, int qb, const Mat4& u) {
  std::size_t maskA = std::size_t(1) << qa;
  std::size_t maskB = std::size_t(1) << qb;
  std::size_t n = sv.qubits;
  std::size_t total = std::size_t(1) << n;
  // For each base index i with bits qa=0, qb=0, gather the four
  // amplitudes (00, 01, 10, 11) and apply u.
  for (std::size_t i = 0; i < total; ++i) {
    if (i & maskA) continue;
    if (i & maskB) continue;
    std::size_t i00 = i;
    std::size_t i01 = i | maskB;
    std::size_t i10 = i | maskA;
    std::size_t i11 = i | maskA | maskB;
    cdbl a00 = sv.amps[i00];
    cdbl a01 = sv.amps[i01];
    cdbl a10 = sv.amps[i10];
    cdbl a11 = sv.amps[i11];
    sv.amps[i00] = u(0, 0) * a00 + u(0, 1) * a01 +
                   u(0, 2) * a10 + u(0, 3) * a11;
    sv.amps[i01] = u(1, 0) * a00 + u(1, 1) * a01 +
                   u(1, 2) * a10 + u(1, 3) * a11;
    sv.amps[i10] = u(2, 0) * a00 + u(2, 1) * a01 +
                   u(2, 2) * a10 + u(2, 3) * a11;
    sv.amps[i11] = u(3, 0) * a00 + u(3, 1) * a01 +
                   u(3, 2) * a10 + u(3, 3) * a11;
  }
}

double extractAngle(const Op& op) {
  for (const auto& a : op.attributes) {
    if (a.name == "angle" && std::holds_alternative<double>(a.value)) {
      return std::get<double>(a.value);
    }
  }
  return 0.0;
}

}  // namespace

namespace {
StateVector initial(std::size_t n) {
  if (n > 24) throw std::runtime_error("local simulator supports at most 24 active qubits");
  StateVector sv{n, std::vector<cdbl>(std::size_t(1) << n, cdbl{})};
  sv.amps[0] = 1.0;
  return sv;
}
void gate(StateVector& sv, const WireOp& op) {
  if(op.kind==OpKind::Move) {
    // IQM only specifies MOVE on |00>, |01>, |10>. Choose a=i as a
    // simulation gauge; balanced sandwiches cancel it exactly. Never assign
    // SWAP semantics to the undefined |11> subspace.
    const auto q=std::size_t(1)<<op.qubits.at(0),r=std::size_t(1)<<op.qubits.at(1);
    for(std::size_t i=0;i<sv.amps.size();++i)if(!(i&q)&&!(i&r)) {
      if(std::abs(sv.amps[i|q|r])>1e-10)throw std::runtime_error("MOVE encountered population in its undefined |11> subspace");
      auto resonator=sv.amps[i|r],qubit=sv.amps[i|q];
      sv.amps[i|q]=cdbl(0,1)*resonator;
      sv.amps[i|r]=cdbl(0,-1)*qubit;
    }
  }
  else if (op.qubits.size() == 1) apply1q(sv, op.qubits[0], passes::matrix1(op));
  else if (op.qubits.size() == 2) apply2q(sv, op.qubits[0], op.qubits[1], passes::matrix2(op));
  else throw std::runtime_error("unsupported simulator operation");
}
bool measure(StateVector& sv, int q, std::mt19937_64& rng) {
  const auto mask = std::size_t(1) << q;
  double p1 = 0;
  for (std::size_t i=0; i<sv.amps.size(); ++i) if(i&mask) p1 += std::norm(sv.amps[i]);
  p1 = std::clamp(p1, 0.0, 1.0);
  bool outcome = std::generate_canonical<double,53>(rng) < p1;
  double p = outcome ? p1 : 1-p1;
  if(p<=0) throw std::runtime_error("invalid zero-probability measurement");
  for(std::size_t i=0;i<sv.amps.size();++i)
    sv.amps[i] = bool(i&mask)==outcome ? sv.amps[i]/std::sqrt(p) : cdbl{};
  return outcome;
}
}
StateVector simulate(const Module& m) {
  if(hasControlFlow(m)||hasClassicalOperations(m))throw std::runtime_error("dynamic circuits require shot simulation");
  auto circuit=flatten(m);
  validateResonatorCircuit(circuit);
  auto sv=initial(circuit.numQubits);
  bool measured=false;
  for(const auto& op:circuit.instructions) {
    if(op.kind==OpKind::GlobalPhase){for(auto& a:sv.amps)a*=std::polar(1.0,parameter(op));continue;}
    if(op.kind==OpKind::Barrier) continue;
    if(op.kind==OpKind::Measure){measured=true;continue;}
    if(op.kind==OpKind::Reset || measured)
      throw std::runtime_error("statevector equivalence requires a unitary circuit with terminal measurements; use sample for reset or mid-circuit measurement");
    gate(sv,op);
  }
  for(auto& a:sv.amps)a*=std::polar(1.0,circuit.globalPhase);
  return sv;
}
std::map<std::string,std::size_t> sample(const Module& m,std::size_t shots,std::mt19937_64& rng) {
  if(shots==0) throw std::runtime_error("shots must be positive");
  auto circuit=flatten(m);
  validateResonatorCircuit(circuit);
  std::map<int,int> active;
  for(const auto& op:circuit.instructions) if(op.kind!=OpKind::Barrier)
    for(int q:op.qubits) if(!active.count(q)) active[q]=static_cast<int>(active.size());
  for(auto& op:circuit.instructions) if(op.kind!=OpKind::Barrier)
    for(auto& q:op.qubits) q=active.at(q);
  // Validate the memory bound before allocating one trajectory per shot.
  auto zero=initial(active.size());
  std::map<std::string,std::size_t> counts;
  for(std::size_t shot=0;shot<shots;++shot){
    auto sv=zero;std::string bits(circuit.numClbits,'0');
    std::unordered_map<std::string,const ClassicalValue*> definitions;
    std::unordered_set<std::string> defined;
    for(const auto& value:circuit.classicalValues){definitions[value.id]=&value;if(value.initialized)defined.insert(value.id);}
    auto read=[&](const std::string& id){
      if(!defined.count(id))throw std::runtime_error("classical value is unavailable on this execution path: "+id);
      std::uint64_t value=0;const auto& storage=definitions.at(id)->storage;
      for(std::size_t bit=0;bit<storage.size();++bit)if(bits.at(circuit.numClbits-1-storage[bit])=='1')value|=std::uint64_t(1)<<bit;
      return value;
    };
    auto write=[&](const std::string& id,std::uint64_t value){
      const auto& storage=definitions.at(id)->storage;
      for(std::size_t bit=0;bit<storage.size();++bit)bits.at(circuit.numClbits-1-storage[bit])=(value&(std::uint64_t(1)<<bit))?'1':'0';
      defined.insert(id);
    };
    for(const auto& value:circuit.classicalValues)if(value.initialized)write(value.id,parseExactInteger<std::uint64_t>(value.initialValue));
    struct Branch{bool parent,condition;};std::vector<Branch> branches;bool active=true;
    for(const auto& op:circuit.instructions){
      if(op.kind==OpKind::If){
        const auto captured=stringAttribute(op,"condition");
        bool condition=active&&((captured.empty()?(bits.at(circuit.numClbits-1-static_cast<std::size_t>(parameter(op,"condition_clbit")))-'0'):read(captured))==parameter(op,"condition_value"));
        branches.push_back({active,condition});active=active&&condition;continue;
      }
      if(op.kind==OpKind::Else){if(branches.empty())throw std::runtime_error("unmatched else");active=branches.back().parent&&!branches.back().condition;continue;}
      if(op.kind==OpKind::EndIf){if(branches.empty())throw std::runtime_error("unmatched endif");active=branches.back().parent;branches.pop_back();continue;}
      if(!active)continue;
      if(isClassical(op.kind)){
        const auto result=stringAttribute(op,"result");const auto inputs=classicalInputs(op);const auto width=definitions.at(result)->width;
        std::uint64_t value;
        if(op.kind==OpKind::CConst)value=parseExactInteger<std::uint64_t>(stringAttribute(op,"value"));
        else if(op.kind==OpKind::CSelect)value=read(inputs.at(read(inputs.at(0))?1:2));
        else{std::vector<std::uint64_t> arguments;for(const auto& input:inputs)arguments.push_back(read(input));value=evaluateClassical(op.kind,arguments,width);}
        write(result,value);continue;
      }
      if(op.kind==OpKind::GlobalPhase){for(auto& a:sv.amps)a*=std::polar(1.0,parameter(op));continue;}
      if(op.kind==OpKind::Barrier)continue;
      if(op.kind==OpKind::Measure){
        bits.at(circuit.numClbits-1-static_cast<std::size_t>(op.clbit))=measure(sv,op.qubits[0],rng)?'1':'0';
        const auto result=stringAttribute(op,"result");if(!result.empty())defined.insert(result);
      }else if(op.kind==OpKind::Reset){
        if(measure(sv,op.qubits[0],rng))apply1q(sv,op.qubits[0],X());
      }else gate(sv,op);
    }
    ++counts[bits];
  }
  return counts;
}

EquivResult equivalent(const Module& a, const Module& b, double tol) {
  struct Prepared {
    WireCircuit circuit;
    std::vector<int> input, output;
    std::vector<std::pair<int,int>> readout;
    std::size_t active=0;
  };
  auto prepare=[](const Module& module) {
    Prepared p;p.circuit=flatten(module);validateResonatorCircuit(p.circuit);
    auto& c=p.circuit;
    if(hasControlFlow(module))throw std::runtime_error("exhaustive equivalence does not support dynamic circuits; use trajectory tests");
    p.input=c.initialLayout;p.output=c.finalLayout;
    if(p.input.empty()&&p.output.empty()) {
      for(std::size_t q=0;q<c.numQubits;++q)
        if(std::find(c.resonatorQubits.begin(),c.resonatorQubits.end(),int(q))==c.resonatorQubits.end()){
          p.input.push_back(int(q));p.output.push_back(int(q));
        }
    }
    if(p.input.size()!=p.output.size())throw std::runtime_error("equivalence requires initial and final layouts of equal width");
    if(p.input.size()>8)throw std::runtime_error("exhaustive equivalence is limited to 8 logical qubits");
    std::map<int,int> compact;
    auto add=[&](int q){if(q<0||std::size_t(q)>=c.numQubits)throw std::runtime_error("invalid equivalence layout");compact.emplace(q,0);};
    for(int q:p.input)add(q);for(int q:p.output)add(q);
    bool measured=false;
    for(const auto& op:c.instructions){
      if(op.kind==OpKind::Measure){
        measured=true;auto it=std::find(p.output.begin(),p.output.end(),op.qubits.at(0));
        if(it==p.output.end())throw std::runtime_error("readout measures a routing ancilla");
        p.readout.emplace_back(int(it-p.output.begin()),op.clbit);
      } else if(op.kind!=OpKind::Barrier&&op.kind!=OpKind::GlobalPhase && (measured||op.kind==OpKind::Reset))
        throw std::runtime_error("exhaustive equivalence requires a unitary circuit with terminal measurements");
      for(int q:op.qubits)add(q);
    }
    if(compact.size()>12)throw std::runtime_error("exhaustive equivalence is limited to 12 active physical qubits");
    for(auto& [q,index]:compact)index=int(p.active++);
    for(auto& q:p.input)q=compact.at(q);for(auto& q:p.output)q=compact.at(q);
    for(auto& op:c.instructions)for(auto& q:op.qubits)q=compact.at(q);
    return p;
  };
  auto pa=prepare(a),pb=prepare(b);EquivResult r;
  if(pa.input.size()!=pb.input.size()||pa.readout!=pb.readout||pa.circuit.numClbits!=pb.circuit.numClbits){
    r.maxAbsDiff=std::numeric_limits<double>::infinity();return r;
  }
  auto column=[&](const Prepared& p,std::size_t basis) {
    auto state=initial(p.active);state.amps[0]=0;
    std::size_t input=0;for(std::size_t q=0;q<p.input.size();++q)if(basis&(std::size_t(1)<<q))input|=std::size_t(1)<<p.input[q];
    state.amps[input]=std::polar(1.0,p.circuit.globalPhase);
    for(const auto& op:p.circuit.instructions){
      if(op.kind==OpKind::Barrier||op.kind==OpKind::Measure)continue;
      if(op.kind==OpKind::GlobalPhase){for(auto& z:state.amps)z*=std::polar(1.0,parameter(op));continue;}
      gate(state,op);
    }
    std::size_t mask=0;for(int q:p.output)mask|=std::size_t(1)<<q;
    std::vector<cdbl> result(std::size_t(1)<<p.output.size());
    for(std::size_t wire=0;wire<state.amps.size();++wire){
      if(wire&~mask){r.maxAbsDiff=std::max(r.maxAbsDiff,std::abs(state.amps[wire]));continue;}
      std::size_t logical=0;for(std::size_t q=0;q<p.output.size();++q)if(wire&(std::size_t(1)<<p.output[q]))logical|=std::size_t(1)<<q;
      result[logical]=state.amps[wire];
    }
    return result;
  };
  for(std::size_t basis=0;basis<(std::size_t(1)<<pa.input.size());++basis){
    auto ca=column(pa,basis),cb=column(pb,basis);
    if(!r.phase)for(std::size_t row=0;row<cb.size();++row)if(std::abs(cb[row])>1e-10){
      cdbl ratio=ca[row]/cb[row];
      if(std::abs(std::abs(ratio)-1)>tol){r.maxAbsDiff=std::max(r.maxAbsDiff,std::abs(std::abs(ratio)-1));return r;}
      r.phase=ratio/std::abs(ratio);break;
    }
    for(std::size_t row=0;row<ca.size();++row)r.maxAbsDiff=std::max(r.maxAbsDiff,std::abs(ca[row]-r.phase.value_or(cdbl{1,0})*cb[row]));
  }
  r.equivalent=r.maxAbsDiff<=tol;return r;
}

ResourceEstimate estimate(const Module& m, const registry::ChipInfo* chip,
                          std::size_t shots) {
  ResourceEstimate r;
  // Per-physical-qubit "depth" tracker.
  std::map<int, std::size_t> depthAt;
  std::size_t allocCount = 0;
  std::map<std::uint32_t, int> lineage;

  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      lineage[op.results.front().v] = static_cast<int>(allocCount++);
      continue;
    }
    if (op.kind == OpKind::AllocBit) continue;
    if (op.kind == OpKind::Measure) {
      ++r.measurements;
      continue;
    }
    if (op.kind == OpKind::Reset || op.kind == OpKind::Barrier) {
      // Don't count, but propagate lineage for Reset.
      int nq = qubitArity(op.kind);
      for (int k = 0; k < nq && k < (int)op.results.size(); ++k) {
        lineage[op.results[k].v] = lineage[op.operands[k].v];
      }
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    ++r.totalGates;
    if (nq == 2) ++r.twoQubitGates;

    // Depth: 1 + max(prev depth on each touched qubit).
    std::size_t prev = 0;
    for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
      int q = lineage[op.operands[k].v];
      auto it = depthAt.find(q);
      if (it != depthAt.end()) prev = std::max(prev, it->second);
    }
    std::size_t newDepth = prev + 1;
    for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
      int q = lineage[op.operands[k].v];
      depthAt[q] = newDepth;
      // Propagate lineage to results.
      if (k < (int)op.results.size()) {
        lineage[op.results[k].v] = q;
      }
    }
    if (newDepth > r.depth) r.depth = newDepth;
  }
  r.qubits = allocCount;

  if (chip) {
    // Independent-error approximation, only when every operation has a
    // supplied calibration. Missing data must not become invented fidelity.
    double pNoErr = 1.0;
    bool known=!hasControlFlow(m);
    auto use=[&](const auto& errors,const auto& key){
      auto it=errors.find(key);
      if(it==errors.end()||!std::isfinite(it->second)||it->second<0||it->second>=1)known=false;
      else pNoErr*=1-it->second;
    };
    for(const auto& op:flatten(m).instructions){
      if(isControl(op.kind)||op.kind==OpKind::Barrier||op.kind==OpKind::GlobalPhase)continue;
      if(op.kind==OpKind::Reset){known=false;continue;}
      if(op.kind==OpKind::Measure)use(chip->calibrationReadoutError,op.qubits.at(0));
      else if(op.qubits.size()==1)use(chip->calibrationOneQubitError,op.qubits[0]);
      else if(op.qubits.size()==2){
        auto edge=std::pair<int,int>{op.qubits[0],op.qubits[1]};
        if(!chip->directedConnectivity&&!chip->calibrationTwoQubitError.count(edge))std::swap(edge.first,edge.second);
        use(chip->calibrationTwoQubitError,edge);
      }
    }
    if(known)r.totalErrorEstimate=1-pNoErr;
    if(chip->pricePerShotUsd>0)r.shotCostUsd = chip->pricePerShotUsd * static_cast<double>(shots);
  }
  return r;
}

}  // namespace spinor::sim

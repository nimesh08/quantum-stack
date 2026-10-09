// spinor/passes/lib/PassManager.cpp

#include "spinor/passes/PassManager.h"
#include "NativeSynthesis.h"
#include <set>
#include <numeric>
#include <queue>
#include <tuple>

#include "spinor/passes/Cleanup.h"
#include "spinor/passes/Collect2qBlocks.h"
#include "spinor/passes/CommutativeCancellation.h"
#include "spinor/passes/ConsolidateBlocks.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/KakResynthesis.h"
#include "spinor/passes/OneQubitEulerResynthesis.h"
#include "spinor/passes/OptimizationLoop.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/passes/SynthesisTraits.h"
#include "spinor/passes/VF2PostLayout.h"

namespace spinor::passes {
namespace {
dialect::Module canonicalParameters(const dialect::Module& module) {
  using namespace dialect;
  auto in=flatten(module),out=in;out.instructions.clear();
  if(hasControlFlow(module)){
    int depth=0;
    for(const auto& op:in.instructions){
      if(isControl(op.kind)||op.kind==OpKind::GlobalPhase){out.instructions.push_back(op);if(op.kind==OpKind::If)++depth;if(op.kind==OpKind::EndIf)--depth;continue;}
      auto single=in;single.instructions={op};single.globalPhase=0;
      auto normalized=flatten(canonicalParameters(rebuild(single)));
      out.instructions.insert(out.instructions.end(),normalized.instructions.begin(),normalized.instructions.end());
      if(depth){if(std::abs(normalized.globalPhase)>1e-13)out.instructions.push_back({OpKind::GlobalPhase,{}, {angleAttr(normalized.globalPhase)},op.loc});}
      else out.globalPhase+=normalized.globalPhase;
    }
    return rebuild(out);
  }
  auto wrap=[](double x){double r=std::fmod(x,2*M_PI);return r<0?r+2*M_PI:r;};
  for(auto op:in.instructions){
    if(op.kind==OpKind::U1q){
      auto desired=matrix1(op);double theta=wrap(parameter(op,"theta")),phi=parameter(op,"phi");
      if(theta>M_PI){theta=2*M_PI-theta;phi+=M_PI;}
      op.attributes={namedDouble("theta",theta),namedDouble("phi",wrap(phi))};
      out.globalPhase+=phaseDifference(desired,matrix1(op));
    }else if(op.kind==OpKind::Gpi||op.kind==OpKind::Gpi2){op.attributes={angleAttr(wrap(parameter(op)))};}
    else if(op.kind==OpKind::Rx){
      double angle=parameter(op),reduced=std::remainder(angle,2*M_PI);
      out.globalPhase+=std::round((angle-reduced)/(2*M_PI))*M_PI;
      if(std::abs(reduced)<1e-13)continue;
      op.attributes={angleAttr(reduced)};
    }
    else if(op.kind==OpKind::Rxx){
      double angle=parameter(op),reduced=wrap(angle);
      out.globalPhase+=std::round((angle-reduced)/(2*M_PI))*M_PI;
      int pieces=std::max(1,static_cast<int>(std::ceil(reduced/(M_PI/2))));
      op.attributes={angleAttr(reduced/pieces)};
      for(int i=0;i<pieces;++i)out.instructions.push_back(op);
      continue;
    }
    out.instructions.push_back(op);
  }
  return rebuild(out);
}
dialect::Module canonicalNativeLoci(const dialect::Module& module,const registry::ChipInfo& chip) {
  using namespace dialect;
  if(!chip.directedConnectivity||chip.allToAll)return module;
  auto circuit=flatten(module);
  const std::set<std::pair<int,int>> loci(chip.coupling.begin(),chip.coupling.end());
  for(auto& op:circuit.instructions) {
    if(op.qubits.size()!=2||op.kind==OpKind::Barrier||loci.contains({op.qubits[0],op.qubits[1]}))continue;
    bool symmetric=false;
    switch(op.kind) {
      case OpKind::Cz:case OpKind::Swap:case OpKind::Rxx:case OpKind::Rzz:
      case OpKind::ISwap:case OpKind::SqrtISwap:case OpKind::SqrtISwapInv:case OpKind::Syc:
        symmetric=true;break;
      // Spinor MS is the fixed zero-phase RXX(pi/2), so exchanging its
      // operands changes neither parameters nor the unitary.
      case OpKind::Ms:symmetric=op.attributes.empty();break;
      default:break;
    }
    if(symmetric&&loci.contains({op.qubits[1],op.qubits[0]}))std::swap(op.qubits[0],op.qubits[1]);
  }
  return rebuild(circuit);
}
void checkCapabilities(const dialect::WireCircuit& c,const registry::ChipInfo& chip){
  bool measured=false;std::vector<bool> branches;
  if(c.numQubits>chip.qubits)throw std::runtime_error("circuit exceeds target qubit capacity");
  for(const auto& op:c.instructions){
    if(op.kind==dialect::OpKind::If){
      if(chip.supports.feedforward==registry::CapabilityFlags::Feedforward::None)throw std::runtime_error("target does not support classical feed-forward");
      auto bit=dialect::parameter(op,"condition_clbit"),value=dialect::parameter(op,"condition_value");
      if(bit<0||bit>=c.numClbits||bit!=std::floor(bit)||(value!=0&&value!=1))throw std::runtime_error("invalid classical condition");
      branches.push_back(false);continue;
    }
    if(op.kind==dialect::OpKind::Else){if(branches.empty()||branches.back())throw std::runtime_error("unmatched or duplicate else");branches.back()=true;continue;}
    if(op.kind==dialect::OpKind::EndIf){if(branches.empty())throw std::runtime_error("unmatched endif");branches.pop_back();continue;}
    if(op.kind==dialect::OpKind::GlobalPhase)continue;
    if(op.kind==dialect::OpKind::Measure){measured=true;continue;}
    if(op.kind==dialect::OpKind::Barrier)continue;
    if(measured&&!chip.supports.midCircuitMeasure)throw std::runtime_error("target does not support mid-circuit measurement");
    if(op.kind==dialect::OpKind::Reset&&!chip.supports.reset)throw std::runtime_error("target does not support reset");
    if(op.qubits.size()==2 && op.qubits[0]==op.qubits[1])throw std::runtime_error("two-qubit gate operands must be distinct");
  }
  if(!branches.empty())throw std::runtime_error("unclosed conditional block");
}
}
bool validateCompiled(const dialect::Module& module,const registry::ChipInfo& chip,dialect::Diagnostics& diag){
  try{
    dialect::verify(module,diag);if(diag.hasErrors())return false;
    auto c=dialect::flatten(module);checkCapabilities(c,chip);
    if(!std::isfinite(c.globalPhase))throw std::runtime_error("nonfinite global phase");
    std::set<int> layoutWires;
    for(int p:c.finalLayout)if(p<0||std::size_t(p)>=c.numQubits||!layoutWires.insert(p).second)
      throw std::runtime_error("invalid final logical-to-physical layout");
    layoutWires.clear();
    for(int p:c.initialLayout)if(p<0||std::size_t(p)>=c.numQubits||!layoutWires.insert(p).second)
      throw std::runtime_error("invalid initial logical-to-physical layout");
    if(c.initialLayout.size()!=c.finalLayout.size())throw std::runtime_error("inconsistent initial/final layout widths");
    CouplingGraph graph(chip.qubits,chip.coupling,chip.allToAll);
    for(const auto& op:c.instructions){
      if(dialect::isControl(op.kind)||op.kind==dialect::OpKind::GlobalPhase)continue;
      if(op.kind==dialect::OpKind::Measure||op.kind==dialect::OpKind::Reset||op.kind==dialect::OpKind::Barrier)continue;
      auto name=std::string(dialect::opMnemonic(op.kind)).substr(7);
      if(std::find(chip.nativeGates.begin(),chip.nativeGates.end(),name)==chip.nativeGates.end())throw std::runtime_error("non-native gate in compiled circuit: "+name);
      if(op.kind==dialect::OpKind::Rx&&chip.decompose.oneQubitPi2Gate=="rx"){
        double angle=dialect::parameter(op),steps=angle/(M_PI/2);
        if(std::abs(angle)<1e-13||std::abs(angle)>M_PI+1e-10||std::abs(steps-std::round(steps))>1e-10)
          throw std::runtime_error("native RX requires a calibrated angle of +/-pi/2 or +/-pi");
      }
      if(op.qubits.size()==2&&!graph.connected(op.qubits[0],op.qubits[1]))throw std::runtime_error("non-adjacent native two-qubit operands");
      if(chip.directedConnectivity&&!chip.allToAll&&op.qubits.size()==2 &&
         std::find(chip.coupling.begin(),chip.coupling.end(),std::pair<int,int>{op.qubits[0],op.qubits[1]})==chip.coupling.end())
        throw std::runtime_error("native gate operand order is not an allowed directed locus");
      if(op.qubits.size()==1)(void)matrix1(op);else(void)matrix2(op);
    }
  }catch(const std::exception& e){diag.error(std::string("compiled circuit: ")+e.what());}
  return !diag.hasErrors();
}


dialect::Module PassManager::compile(const dialect::Module& module,
                                     const registry::ChipInfo& chip,
                                     OptimizationLevel level,
                                     dialect::Diagnostics& diag) const try {
  dialect::verify(module,diag);
  if(diag.hasErrors())return module;
  checkCapabilities(dialect::flatten(module),chip);
  // Stage 1: Placement (always run; chip-agnostic — only reads
  // the coupling map).
  CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  Placement pl;
  auto layout = pl.run(module, g);

  auto compileLayout = [&](const Layout& candidateLayout,
                           dialect::Diagnostics& candidateDiagnostics) -> dialect::Module {
  // Stage 2: Routing (always run; chip-agnostic — only reads the
  // coupling map and uses deterministic shortest paths).
  Routing routing;
  auto routed = routing.run(module, chip, g, candidateLayout);

  // Stage 3: Decomposition (always run; vendor-modular via the
  // YAML registry strings — entangler / rotation_gate / pi_2_gate).
  Decomposition dec;
  auto decomposed = dec.run(routed.module, chip, candidateDiagnostics);

  // Stage 4: Cleanup. O0 returns raw post-decomposition IR for
  // hardware characterization use. O1/O2/O3 run the local
  // peephole (vendor-aware overload).
  if (level == OptimizationLevel::O0) {
    auto result=canonicalNativeLoci(canonicalParameters(decomposed),chip);
    validateCompiled(result,chip,candidateDiagnostics);
    return result;
  }

  Cleanup cl;
  auto cleaned = cl.run(decomposed, chip);

  // Stage 5: optimization pipeline. Vendor-modular passes,
  // selected by level. All passes take `const SynthesisTraits&`
  // (or nothing) — zero `if (vendor == ibm)` anywhere.
  //
  SynthesisTraits traits = computeTraits(chip);

  // O1 loop body: peephole + 1Q Euler resynthesis +
  // CommutativeCancellation (CommutativeCancellation enabled at
  // O2+; here it runs but is a no-op at O1 since its rule table
  // is sparse). Wrapped in a fixed-point loop.
  if (level == OptimizationLevel::O1 ||
      level == OptimizationLevel::O2 ||
      level == OptimizationLevel::O3) {
    OptimizationLoop<FixedPointCriterion> loop;
    auto body = [&](const dialect::Module& m) -> dialect::Module {
      OneQubitEulerResynthesis euler;
      auto a = euler.run(m, traits);
      if (level == OptimizationLevel::O2 ||
          level == OptimizationLevel::O3) {
        CommutativeCancellation cc;
        a = cc.run(a, traits);
      }
      Cleanup peephole;
      return peephole.run(a, chip);
    };
    cleaned = loop.run(cleaned, body, FixedPointCriterion{}, /*maxIters=*/8);
  }

  // O2 performs one exact block rewrite. Dynamic regions retain their
  // explicit fences and phase semantics; they are not flattened into a block.
  if (level == OptimizationLevel::O2 && !dialect::hasControlFlow(cleaned)) {
    const auto blocks = Collect2qBlocks{}.run(cleaned);
    const auto consolidated = ConsolidateBlocks{}.run(cleaned, blocks);
    cleaned = KakResynthesis{}.run(cleaned, consolidated, traits, &chip);
    cleaned = OneQubitEulerResynthesis{}.run(cleaned, traits);
    cleaned = Cleanup{}.run(cleaned, chip);
  }

  // O3: 2Q block KAK resynthesis + VF2 post-layout rescue,
  // wrapped in a minimum-point loop.
  if (level == OptimizationLevel::O3) {
    OptimizationLoop<MinimumPointCriterion> loop3;
    auto body3 = [&](const dialect::Module& m) -> dialect::Module {
      if(dialect::hasControlFlow(m))return m;
      Collect2qBlocks collect;
      auto blocks = collect.run(m);
      ConsolidateBlocks consolidate;
      auto consolidated = consolidate.run(m, blocks);
      KakResynthesis kak;
      auto resynth = kak.run(m, consolidated, traits, &chip);
      OneQubitEulerResynthesis euler;
      resynth = euler.run(resynth, traits);
      CommutativeCancellation cc;
      resynth = cc.run(resynth, traits);
      Cleanup peephole;
      return peephole.run(resynth, chip);
    };
    cleaned = loop3.run(cleaned, body3, MinimumPointCriterion{}, /*maxIters=*/6);
    VF2PostLayout vf2;
    CalibrationCosts calibration{chip.calibrationOneQubitError,
                                 chip.calibrationReadoutError,
                                 chip.calibrationTwoQubitError};
    cleaned = vf2.run(cleaned, chip, calibration);
  }

  auto result=canonicalNativeLoci(canonicalParameters(cleaned),chip);
  validateCompiled(result,chip,candidateDiagnostics);
  return result;
  };
  auto best = compileLayout(layout, diag);
  if (diag.hasErrors() || level != OptimizationLevel::O3 || g.allToAll() || layout.v2p.size() < 2)
    return best;

  // At most seven alternative initial embeddings. Each is fully routed,
  // synthesized, optimized and validated before selection. Invalid trials
  // cannot replace the baseline, and neither native gate count may grow.
  auto cost = [](const dialect::Module& value) {
    auto circuit = dialect::flatten(value);
    std::size_t two = 0;
    for (const auto& op : circuit.instructions)
      if (op.qubits.size() == 2 && op.kind != dialect::OpKind::Barrier) ++two;
    auto metric = computeMetric(value);
    return std::tuple{two, metric.size, metric.depth};
  };
  auto bestCost = cost(best);
  std::vector<std::vector<int>> candidates;
  auto reversed = layout.v2p;
  std::reverse(reversed.begin(), reversed.end());
  candidates.push_back(reversed);
  auto rotated = layout.v2p;
  std::rotate(rotated.begin(), rotated.begin()+1, rotated.end());
  candidates.push_back(rotated);
  std::vector<int> seeds(chip.qubits);
  std::iota(seeds.begin(), seeds.end(), 0);
  std::stable_sort(seeds.begin(), seeds.end(), [&](int a, int b) {
    return g.neighbours(a).size() > g.neighbours(b).size();
  });
  for (std::size_t seedIndex = 0; seedIndex < std::min<std::size_t>(5, seeds.size()); ++seedIndex) {
    std::vector<int> physical;
    std::vector<bool> seen(chip.qubits);
    std::queue<int> frontier;
    auto visit = [&](int start) {
      if (seen[start]) return;
      seen[start] = true; frontier.push(start);
      while (!frontier.empty() && physical.size() < layout.v2p.size()) {
        int q = frontier.front(); frontier.pop(); physical.push_back(q);
        for (int neighbor : g.neighbours(q)) if (!seen[neighbor]) {
          seen[neighbor] = true; frontier.push(neighbor);
        }
      }
    };
    visit(seeds[seedIndex]);
    for (int seed : seeds) if (physical.size() < layout.v2p.size()) visit(seed);
    candidates.push_back(std::move(physical));
  }
  std::set<std::vector<int>> tried{layout.v2p};
  for (const auto& physical : candidates) {
    if (!tried.insert(physical).second) continue;
    Layout candidate; candidate.v2p = physical; candidate.p2v.assign(chip.qubits, -1);
    for (std::size_t q = 0; q < physical.size(); ++q) candidate.p2v[physical[q]] = int(q);
    try {
      dialect::Diagnostics trialDiagnostics;
      auto trial = compileLayout(candidate, trialDiagnostics);
      if (trialDiagnostics.hasErrors()) continue;
      const auto trialCost = cost(trial);
      if (std::get<0>(trialCost) <= std::get<0>(bestCost) &&
          std::get<1>(trialCost) <= std::get<1>(bestCost) && trialCost < bestCost) {
        best = std::move(trial); bestCost = trialCost;
      }
    } catch (const std::exception&) {
      // Disconnected or otherwise infeasible candidates retain the already
      // validated baseline; a bounded layout search is optional optimization.
    }
  }
  return best;
} catch(const std::exception& e) {
  diag.error(std::string("compile: ")+e.what());
  return module;
}

}  // namespace spinor::passes

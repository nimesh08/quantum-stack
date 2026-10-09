// spinor/passes/lib/PassManager.cpp

#include "spinor/passes/PassManager.h"
#include "NativeSynthesis.h"
#include <set>
#include <numeric>
#include <queue>
#include <tuple>

#include "spinor/passes/Cleanup.h"
#include "spinor/passes/ClassicalStorageReuse.h"
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
#include "spinor/passes/ResonatorRouting.h"
#include "spinor/passes/AvailableRouting.h"
#include "spinor/dialect/Resonators.h"
#include "spinor/registry/ComponentTopology.h"

namespace spinor::passes {
namespace {
// libm's trigonometric reduction retains the represented double's angle even
// when a quotient by rounded 2*pi would lose many radians. Rotations have a
// 4*pi period in SU(2); gate-axis and scalar phases have a 2*pi period.
double boundedPhase(double angle) {
  return std::atan2(std::sin(angle),std::cos(angle));
}
double boundedRotation(double angle) {
  return 2*std::atan2(std::sin(angle/2),std::cos(angle/2));
}
dialect::Module boundedInputParameters(const dialect::Module& module) {
  using namespace dialect;
  auto circuit=flatten(module);
  const auto originalPhase=circuit.globalPhase;
  circuit.globalPhase=boundedPhase(circuit.globalPhase);
  observeRewrite("parameter-normalization","scalar-phase",{},{},originalPhase,circuit.globalPhase);
  std::size_t region=0;
  for(auto& op:circuit.instructions) {
    const auto original=op;
    switch(op.kind) {
      case OpKind::Rx:case OpKind::Ry:case OpKind::Rz:
      case OpKind::Rxx:case OpKind::Rzz:
        op.attributes={angleAttr(boundedRotation(parameter(op)))};break;
      case OpKind::Gpi:case OpKind::Gpi2:case OpKind::GlobalPhase:
        op.attributes={angleAttr(boundedPhase(parameter(op)))};break;
      case OpKind::U1q:
        op.attributes={namedDouble("theta",boundedRotation(parameter(op,"theta"))),
                       namedDouble("phi",boundedPhase(parameter(op,"phi")))};break;
      case OpKind::PhasedXZ:
        op.attributes={namedDouble("x",boundedPhase(parameter(op,"x"))),
                       namedDouble("z",boundedPhase(parameter(op,"z"))),
                       namedDouble("axis_phase",boundedPhase(parameter(op,"axis_phase")))};break;
      default:break;
    }
    observeRewrite("parameter-normalization","individual-input",{original},{op},0,0,std::nullopt,numericalRegion(region));
    if(isNumericalRegionBoundary(op))++region;
  }
  return rebuild(circuit);
}
dialect::Module canonicalParameters(const dialect::Module& module,std::size_t region=0) {
  using namespace dialect;
  auto in=flatten(module),out=in;out.instructions.clear();
  if(hasControlFlow(module)){
    int depth=0;
    for(const auto& op:in.instructions){
      if(isControl(op.kind)||op.kind==OpKind::GlobalPhase){
        out.instructions.push_back(op);if(op.kind==OpKind::If)++depth;if(op.kind==OpKind::EndIf)--depth;
        if(isNumericalRegionBoundary(op))++region;
        continue;
      }
      auto single=in;single.instructions={op};single.globalPhase=0;
      auto normalized=flatten(canonicalParameters(rebuild(single),region));
      out.instructions.insert(out.instructions.end(),normalized.instructions.begin(),normalized.instructions.end());
      if(depth){if(std::abs(normalized.globalPhase)>kRecognitionTolerance)out.instructions.push_back({OpKind::GlobalPhase,{}, {angleAttr(normalized.globalPhase)},op.loc});}
      else out.globalPhase+=normalized.globalPhase;
      if(isNumericalRegionBoundary(op))++region;
    }
    return rebuild(out);
  }
  auto wrap=[](double x){double r=boundedPhase(x);return r<0?r+2*M_PI:r;};
  for(auto op:in.instructions){
    if(isNumericalRegionBoundary(op)){out.instructions.push_back(op);++region;continue;}
    const auto original=op;
    const auto start=out.instructions.size();const auto phaseBefore=out.globalPhase;
    auto record=[&]{observeRewrite("parameter-canonicalization","native-range",{original},
      std::vector<WireOp>(out.instructions.begin()+start,out.instructions.end()),0,out.globalPhase-phaseBefore,std::nullopt,numericalRegion(region));};
    if(op.kind==OpKind::U1q){
      auto desired=matrix1(op);double theta=boundedRotation(parameter(op,"theta")),phi=boundedPhase(parameter(op,"phi"));
      if(theta<0)theta+=2*M_PI;
      if(theta>M_PI){theta=2*M_PI-theta;phi+=M_PI;}
      op.attributes={namedDouble("theta",theta),namedDouble("phi",wrap(phi))};
      out.globalPhase+=phaseDifference(desired,matrix1(op));
    }else if(op.kind==OpKind::Gpi||op.kind==OpKind::Gpi2){op.attributes={angleAttr(wrap(parameter(op)))};}
    else if(op.kind==OpKind::Rx){
      const auto desired=matrix1(op);
      double reduced=boundedRotation(parameter(op));
      if(reduced>M_PI)reduced-=2*M_PI;
      if(reduced<-M_PI)reduced+=2*M_PI;
      op.attributes={angleAttr(reduced)};
      out.globalPhase+=phaseDifference(desired,matrix1(op));
      if(std::abs(reduced)<kRecognitionTolerance){record();continue;}
    }
    else if(op.kind==OpKind::Rxx){
      const auto desired=matrix2(op);
      double reduced=boundedRotation(parameter(op));
      if(reduced<0)reduced+=2*M_PI;
      op.attributes={angleAttr(reduced)};
      out.globalPhase+=phaseDifference(desired,matrix2(op));
      int pieces=std::max(1,static_cast<int>(std::ceil(reduced/(M_PI/2))));
      op.attributes={angleAttr(reduced/pieces)};
      for(int i=0;i<pieces;++i)out.instructions.push_back(op);
      record();continue;
    }
    out.instructions.push_back(op);
    record();
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
bool preservesClassicalExports(const dialect::WireCircuit& before,
                              const dialect::WireCircuit& after) {
  if(before.exportedClbits!=after.exportedClbits||
     before.classicalOutputs.size()!=after.classicalOutputs.size())return false;
  for(std::size_t i=0;i<before.classicalOutputs.size();++i){
    const auto& a=before.classicalOutputs[i];const auto& b=after.classicalOutputs[i];
    if(std::tie(a.name,a.value,a.type,a.width,a.role)!=std::tie(b.name,b.value,b.type,b.width,b.role))return false;
    const auto oldValue=std::find_if(before.classicalValues.begin(),before.classicalValues.end(),[&](const auto& v){return v.id==a.value;});
    const auto newValue=std::find_if(after.classicalValues.begin(),after.classicalValues.end(),[&](const auto& v){return v.id==a.value;});
    if(oldValue==before.classicalValues.end()||newValue==after.classicalValues.end()||oldValue->storage!=newValue->storage)return false;
  }
  for(const auto& value:before.classicalValues)if(value.visibility!="private"||value.initialized){
    const auto found=std::find_if(after.classicalValues.begin(),after.classicalValues.end(),[&](const auto& v){return v.id==value.id;});
    if(found==after.classicalValues.end()||found->storage!=value.storage)return false;
  }
  return true;
}
void checkCapabilities(const dialect::WireCircuit& c,const registry::ChipInfo& chip){
  bool measured=false;std::vector<bool> branches;
  if(c.numQubits>chip.qubits)throw std::runtime_error("circuit exceeds target qubit capacity");
  auto requireFeature=[&](const std::string& name){
    const auto found=chip.classicalFeatures.find(name);
    if(found==chip.classicalFeatures.end()||found->second!="supported")
      throw std::runtime_error("target capability "+name+" is not verified supported; refresh capabilities or select a compatible target");
  };
  auto supportsFeature=[&](const std::string& name,bool legacy){
    const auto found=chip.classicalFeatures.find(name);
    // Only absent legacy metadata may inherit the historical flag. An explicit
    // unknown snapshot is not evidence that this operation is supported.
    return found==chip.classicalFeatures.end()?legacy:found->second=="supported";
  };
  if(!c.classicalOutputs.empty())requireFeature("output.classical");
  for(const auto& output:c.classicalOutputs)if(output.role=="loop_exhausted")requireFeature("output.loop_exhausted");
  for(const auto& value:c.classicalValues)if(value.type=="uint"&&
      std::find(chip.classicalIntegerWidths.begin(),chip.classicalIntegerWidths.end(),value.width)==chip.classicalIntegerWidths.end())
    throw std::runtime_error("target has no verified support for runtime uint width "+std::to_string(value.width)+"; refresh capabilities or use an explicitly supporting target");
  for(const auto& op:c.instructions){
    if(dialect::isClassical(op.kind)){
      auto name=std::string(dialect::opMnemonic(op.kind));
      const auto marker=name.find("c_");
      if(marker==std::string::npos)throw std::runtime_error("unknown classical instruction capability");
      const auto feature="classical."+name.substr(marker+2);
      requireFeature(feature);
      continue;
    }
    if(op.kind==dialect::OpKind::If){
      if(!supportsFeature("branching.bit",chip.supports.feedforward!=registry::CapabilityFlags::Feedforward::None))
        throw std::runtime_error("target capability branching.bit does not support classical feed-forward");
      auto bit=dialect::parameter(op,"condition_clbit"),value=dialect::parameter(op,"condition_value");
      if(bit<0||bit>=c.numClbits||bit!=std::floor(bit)||(value!=0&&value!=1))throw std::runtime_error("invalid classical condition");
      branches.push_back(false);continue;
    }
    if(op.kind==dialect::OpKind::Else){if(branches.empty()||branches.back())throw std::runtime_error("unmatched or duplicate else");branches.back()=true;continue;}
    if(op.kind==dialect::OpKind::EndIf){if(branches.empty())throw std::runtime_error("unmatched endif");branches.pop_back();continue;}
    if(op.kind==dialect::OpKind::GlobalPhase||dialect::isClassical(op.kind))continue;
    if(op.kind==dialect::OpKind::Measure){
      if(!supportsFeature("measure",true))throw std::runtime_error("target capability measure does not support measurement");
      measured=true;continue;
    }
    if(op.kind==dialect::OpKind::Barrier)continue;
    if(measured&&!chip.supports.midCircuitMeasure)throw std::runtime_error("target does not support mid-circuit measurement");
    if(op.kind==dialect::OpKind::Reset&&!supportsFeature("reset",chip.supports.reset))throw std::runtime_error("target capability reset does not support reset");
    if(op.qubits.size()==2 && op.qubits[0]==op.qubits[1])throw std::runtime_error("two-qubit gate operands must be distinct");
  }
  if(!branches.empty())throw std::runtime_error("unclosed conditional block");
}
}
dialect::Module canonicalizeNative(const dialect::Module& module,const registry::ChipInfo& chip){
  return canonicalNativeLoci(canonicalParameters(module),chip);
}
bool validateCompiled(const dialect::Module& module,const registry::ChipInfo& chip,dialect::Diagnostics& diag){
  try{
    dialect::verify(module,diag);if(diag.hasErrors())return false;
    auto c=dialect::flatten(module);checkCapabilities(c,chip);
    validateAvailableCircuit(c,chip);
    auto computers=registry::computationalComponents(chip);
    auto actualResonators=c.resonatorQubits,expectedResonators=chip.resonatorQubits;
    std::sort(actualResonators.begin(),actualResonators.end());std::sort(expectedResonators.begin(),expectedResonators.end());
    if(actualResonators!=expectedResonators)throw std::runtime_error("physical IR resonator metadata does not match target");
    dialect::validateResonatorCircuit(c);
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
      if(dialect::isControl(op.kind)||dialect::isClassical(op.kind)||op.kind==dialect::OpKind::GlobalPhase)continue;
      if(op.kind==dialect::OpKind::Measure||op.kind==dialect::OpKind::Reset||op.kind==dialect::OpKind::Barrier)continue;
      auto name=std::string(dialect::opMnemonic(op.kind)).substr(7);
      if(std::find(chip.nativeGates.begin(),chip.nativeGates.end(),name)==chip.nativeGates.end())throw std::runtime_error("non-native gate in compiled circuit: "+name);
      if(op.kind==dialect::OpKind::Move) {
        if(std::find(chip.moveLoci.begin(),chip.moveLoci.end(),std::pair{op.qubits[0],op.qubits[1]})==chip.moveLoci.end())
          throw std::runtime_error("MOVE is not calibrated on the ordered (qubit, resonator) locus");
        continue;
      }
      if(op.kind==dialect::OpKind::Cz&&!chip.resonatorQubits.empty()) {
        if(std::find(chip.czLoci.begin(),chip.czLoci.end(),std::pair{op.qubits[0],op.qubits[1]})==chip.czLoci.end())
          throw std::runtime_error("CZ operand order is not a calibrated physical locus");
        continue;
      }
      if(op.kind==dialect::OpKind::Rx&&chip.decompose.oneQubitPi2Gate=="rx"){
        double angle=dialect::parameter(op),steps=angle/(M_PI/2);
        if(std::abs(angle)<kRecognitionTolerance||std::abs(angle)>M_PI+kRecognitionTolerance||std::abs(steps-std::round(steps))>kRecognitionTolerance)
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
                                     dialect::Diagnostics& diag,
                                     CompilationReport* report) const try {
  CompilationReportScope reportScope(report);
  dialect::verify(module,diag);
  if(diag.hasErrors())return module;
  // Bound each input independently before additions in rotation merging or
  // synthesis. Adding a small angle to an unbounded double can otherwise
  // discard the complete operation (for example RZ(1e16); RZ(1)).
  if(report){
    report->stage="logical";
    report->addGap("Wire placement, routing permutations and serialization rounding are not included in local residual observations");
    for(const auto& op:dialect::flatten(module).instructions)
      if(isNumericalRegionBoundary(op)&&op.kind!=dialect::OpKind::Barrier)report->hasNonunitaryOperations=true;
  }
  auto bounded=boundedInputParameters(module);
  checkCapabilities(dialect::flatten(bounded),chip);
  if(level!=OptimizationLevel::O0){
    bounded=Cleanup{}.run(bounded);
    if(level==OptimizationLevel::O2||level==OptimizationLevel::O3)
      bounded=CommutativeCancellation{}.run(bounded,computeTraits(chip));
  }
  if(report)report->stage="native";
  auto result=compilePrepared(bounded,chip,level,diag,report);
  // Storage coloring is independent of recursive physical-index compaction.
  // Run it once, only after the complete selected native candidate is known.
  if(level!=OptimizationLevel::O0&&!diag.hasErrors()&&!result.classicalValues.empty()){
    auto candidateReport=report?*report:CompilationReport{};
    try{
      CompilationReportScope candidateScope(report?&candidateReport:nullptr);
      auto candidate=ClassicalStorageReuse{}.run(result);
      const auto before=dialect::flatten(result),after=dialect::flatten(candidate);
      dialect::Diagnostics candidateDiagnostics;
      if(before.instructions.size()!=after.instructions.size()||
         !preservesClassicalExports(before,after)||!validateCompiled(candidate,chip,candidateDiagnostics))
        throw std::runtime_error("private storage candidate failed export or native validation");
      result=std::move(candidate);
      if(report){*report=std::move(candidateReport);++report->counters["classical_storage_candidates_accepted"];}
    }catch(const std::exception&){
      if(report)++report->counters["classical_storage_candidates_rejected"];
      // Optional reuse never replaces the validated physical program on failure.
    }
  }
  return result;
} catch(const std::exception& e) {
  diag.error(std::string("compile: ")+e.what());return module;
}

dialect::Module PassManager::compilePrepared(const dialect::Module& bounded,
                                     const registry::ChipInfo& chip,
                                     OptimizationLevel level,
                                     dialect::Diagnostics& diag,
                                     CompilationReport* report) const try {
  CompilationReportScope reportScope(report);
  dialect::verify(bounded,diag);
  if(diag.hasErrors())return bounded;
  checkCapabilities(dialect::flatten(bounded),chip);
  if(auto available=compileAvailableCircuit(bounded,chip,level,diag,report))return *available;
  if(!chip.resonatorQubits.empty())return compileResonatorCircuit(bounded,chip,level,diag,report);
  // Stage 1: Placement (always run; chip-agnostic — only reads
  // the coupling map).
  CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  Placement pl;
  auto layout = pl.run(bounded, g);

  auto compileLayout = [&](const Layout& candidateLayout,
                           dialect::Diagnostics& candidateDiagnostics) -> dialect::Module {
  // Stage 2: Routing (always run; chip-agnostic — only reads the
  // coupling map and uses deterministic shortest paths).
  Routing routing;
  auto routed = routing.run(bounded, chip, g, candidateLayout);

  // Stage 3: Decomposition (always run; vendor-modular via the
  // YAML registry strings — entangler / rotation_gate / pi_2_gate).
  Decomposition dec;
  auto decomposed = dec.run(routed.module, chip, candidateDiagnostics);

  // Stage 4: Cleanup. O0 returns raw post-decomposition IR for
  // hardware characterization use. O1/O2/O3 run the local
  // peephole (vendor-aware overload).
  if (level == OptimizationLevel::O0) {
    auto result=canonicalizeNative(decomposed,chip);
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

  // O1 uses peephole cleanup and 1Q Euler resynthesis in a bounded
  // fixed-point loop. O2/O3 additionally run commutation cancellation.
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

  auto result=canonicalizeNative(cleaned,chip);
  validateCompiled(result,chip,candidateDiagnostics);
  return result;
  };
  const auto reportPrefix=report?*report:CompilationReport{};
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
      auto trialReport=reportPrefix;
      CompilationReportScope trialScope(report?&trialReport:nullptr);
      auto trial = compileLayout(candidate, trialDiagnostics);
      if(report)++report->counters["layout_trials"];
      if (trialDiagnostics.hasErrors()) {if(report)++report->counters["layout_trials_rejected"];continue;}
      const auto trialCost = cost(trial);
      if (std::get<0>(trialCost) <= std::get<0>(bestCost) &&
          std::get<1>(trialCost) <= std::get<1>(bestCost) && trialCost < bestCost) {
        best = std::move(trial); bestCost = trialCost;
        if(report){
          const auto counters=report->counters;
          *report=std::move(trialReport);
          for(const auto& [key,value]:counters)report->counters[key]=std::max(report->counters[key],value);
          ++report->counters["layout_trials_accepted"];
        }
      }else if(report)++report->counters["layout_trials_rejected"];
    } catch (const std::exception&) {
      if(report)++report->counters["layout_trial_exceptions"];
      // Disconnected or otherwise infeasible candidates retain the already
      // validated baseline; a bounded layout search is optional optimization.
    }
  }
  return best;
} catch(const std::exception& e) {
  diag.error(std::string("compile: ")+e.what());
  return bounded;
}

}  // namespace spinor::passes

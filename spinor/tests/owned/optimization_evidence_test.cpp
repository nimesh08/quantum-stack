#include "spinor/passes/PassManager.h"
#include "spinor/passes/Cleanup.h"
#include "spinor/passes/OneQubitEulerResynthesis.h"
#include "spinor/passes/OptimizationLoop.h"
#include "spinor/passes/SynthesisTraits.h"
#include "spinor/dialect/Numerics.h"
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace spinor;
using namespace spinor::dialect;
using Complex=std::complex<double>;
using Matrix=std::array<Complex,16>;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Matrix identity(){Matrix out{};for(int i=0;i<4;++i)out[5*i]=1;return out;}
Matrix product(const Matrix& a,const Matrix& b){Matrix out{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)for(int k=0;k<4;++k)out[4*r+c]+=a[4*r+k]*b[4*k+c];return out;}
// Independent complete-phase definitions, not compiler matrix helpers.
Matrix complete(const WireCircuit& circuit){
  auto out=identity();
  for(const auto& op:circuit.instructions){
    if(op.kind==OpKind::Barrier)continue;
    if(op.kind==OpKind::GlobalPhase){for(auto& entry:out)entry*=std::exp(Complex(0,parameter(op)));continue;}
    Matrix gate{};
    if(op.qubits.size()==1){
      std::array<Complex,4> one{};double angle=parameter(op);
      if(op.kind==OpKind::Rx)one={std::cos(angle/2),Complex(0,-std::sin(angle/2)),Complex(0,-std::sin(angle/2)),std::cos(angle/2)};
      else if(op.kind==OpKind::Ry)one={std::cos(angle/2),-std::sin(angle/2),std::sin(angle/2),std::cos(angle/2)};
      else if(op.kind==OpKind::Rz)one={std::exp(Complex(0,-angle/2)),0,0,std::exp(Complex(0,angle/2))};
      else throw std::runtime_error("unexpected audit one-qubit gate");
      int mask=1<<(1-op.qubits[0]);
      for(int col=0;col<4;++col)for(int bit=0;bit<2;++bit){int row=(col&~mask)|(bit?mask:0);gate[4*row+col]=one[2*bit+((col&mask)?1:0)];}
    }else if(op.kind==OpKind::Cx){
      for(int col=0;col<4;++col){int row=col;if(col&(1<<(1-op.qubits[0])))row^=1<<(1-op.qubits[1]);gate[4*row+col]=1;}
    }else throw std::runtime_error("unexpected audit two-qubit gate");
    out=product(gate,out);
  }
  for(auto& entry:out)entry*=std::exp(Complex(0,circuit.globalPhase));return out;
}
double error(const Matrix& a,const Matrix& b){double worst=0;for(int i=0;i<16;++i)worst=std::max(worst,std::abs(a[i]-b[i]));return worst;}
int main()try{
  registry::ChipInfo chip;chip.id="evidence";chip.qubits=2;chip.allToAll=true;
  chip.nativeGates={"rx","ry","rz","cx"};chip.decompose.twoQubitEntangler="cx";
  WireCircuit circuit;circuit.numQubits=2;circuit.target="generic";
  for(int i=0;i<12;++i)for(int q=0;q<2;++q)
    circuit.instructions.push_back({i%3==0?OpKind::Rx:i%3==1?OpKind::Ry:OpKind::Rz,{q},{angleAttr((i+1)*.073*(q?-.7:1.))},{}});
  static_assert(passes::NumericalReport::schemaVersion==1);
  passes::NumericalReport report;
  Module result;
  {passes::CompilationReportScope scope(&report);result=passes::OneQubitEulerResynthesis{}.run(rebuild(circuit),passes::computeTraits(chip));}
  require(flatten(result).instructions.size()<circuit.instructions.size(),"interleaved Euler blocks did not shrink");
  require(error(complete(circuit),complete(flatten(result)))<2e-13,"interleaved Euler changed complete operator");
  require(report.rewrites.size()==2,"expected one accepted observation per wire");
  for(const auto& event:report.rewrites)require(event.residual&&*event.residual<2e-13,"incorrect reconstruction observation");
  // The observer must expose a discrepancy rather than aligning it away.
  passes::CompilationReport mutation;
  {passes::CompilationReportScope scope(&mutation);
    passes::observeRewrite("audit","injected-angle",{{OpKind::Rx,{0},{angleAttr(0)}, {}}},{{OpKind::Rx,{0},{angleAttr(1e-10)}, {}}});
    passes::observeRewrite("audit","injected-phase",{},{},0,1e-10);
  }
  require(mutation.rewrites.size()==2,"missing mutation evidence");
  require(*mutation.rewrites[0].residual>7e-11,"small angle discrepancy hidden");
  require(*mutation.rewrites[1].residual>9e-11,"scalar phase discrepancy hidden");
  require(mutation.json().find("\"certified\":false")!=std::string::npos,"uncertified evidence mislabeled");
  mutation.hasNonunitaryOperations=true;
  require(mutation.json().find("\"sum_observed_local_residuals\":null")==std::string::npos,"dynamic program lost its measured unitary-region residual sum");
  require(mutation.json().find("\"aggregation_available\":true")!=std::string::npos,"dynamic region observations cannot be aggregated");
  require(mutation.json().find("\"whole_program_error\":null")!=std::string::npos,"local region observations became a whole-program estimate");
  // No observations is unknown, whereas an actually measured zero is zero.
  passes::NumericalReport unmeasured;
  {passes::CompilationReportScope scope(&unmeasured);
    passes::observeRewrite("audit","out-of-domain",{{OpKind::X,{0},{},{}},{OpKind::X,{1},{},{}},{OpKind::X,{2},{},{}}},{});
  }
  require(unmeasured.rewrites.size()==1&&!unmeasured.rewrites[0].residual,"out-of-domain rewrite received a fabricated measurement");
  require(unmeasured.json().find("\"sum_observed_local_residuals\":null")!=std::string::npos,"empty observed sum was reported as zero");
  require(unmeasured.json().find("\"max_observed_local_residual\":null")!=std::string::npos,"empty observed maximum was reported as zero");
  require(unmeasured.json().find("\"aggregation_available\":false")!=std::string::npos,"unmeasured region advertises an aggregate");
  passes::NumericalReport exactZero;
  {passes::CompilationReportScope scope(&exactZero);
    passes::observeRewrite("audit","exact-inverse",{{OpKind::X,{0},{},{}},{OpKind::X,{0},{},{}}},{});
  }
  require(exactZero.rewrites.size()==1&&exactZero.rewrites[0].residual==0,"exact inverse was not observed as zero");
  require(exactZero.json().find("\"sum_observed_local_residuals\":0")!=std::string::npos,"measured zero lost its numerical value");
  require(exactZero.json().find("\"aggregation_available\":true")!=std::string::npos,"measured unitary zero lacks an aggregate");
  // Cancellation must happen in the logical stage, before required routing.
  WireCircuit inverse;inverse.numQubits=2;inverse.target="generic";
  inverse.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::Cx,{0,1},{},{}}};
  Diagnostics diagnostics;passes::CompilationReport logical;
  result=passes::PassManager{}.compile(rebuild(inverse),chip,passes::OptimizationLevel::O1,diagnostics,&logical);
  require(!diagnostics.hasErrors()&&flatten(result).instructions.empty(),"logical cancellation failed");
  require(!logical.rewrites.empty()&&logical.rewrites.front().pass=="logical.cleanup","cancellation occurred after routing");
  // Hard barriers forbid collection, even when operations otherwise cancel.
  inverse.instructions={{OpKind::Rx,{0},{angleAttr(.3)},{}},{OpKind::Barrier,{},{},{}},{OpKind::Rx,{0},{angleAttr(-.3)}, {}}};
  result=passes::OneQubitEulerResynthesis{}.run(rebuild(inverse),passes::computeTraits(chip));
  require(flatten(result).instructions.size()==3,"Euler crossed a barrier");
  require(error(complete(inverse),complete(flatten(result)))<2e-13,"barrier circuit operator changed");
  // Direct cleanup calls have the same individually bounded angle semantics
  // as the compilation entrypoint. Keep SU(2) scalar phase at +/-2*pi.
  for(const auto& angles:std::array<std::array<double,2>,6>{{
      {1e16,1.},{-1e300,.7},{2*M_PI,0.},{M_PI,M_PI},{.4,-.4+1e-10},{.4,-.4}}}){
    WireCircuit pair;pair.numQubits=2;
    pair.instructions={{OpKind::Rx,{0},{angleAttr(angles[0])},{}},
                       {OpKind::Rx,{0},{angleAttr(angles[1])},{}}};
    passes::CompilationReport merged;
    {passes::CompilationReportScope scope(&merged);result=passes::Cleanup{}.run(rebuild(pair));}
    require(error(complete(pair),complete(flatten(result)))<2e-13,"direct rotation merge changed full operator");
    require(merged.rewrites.size()==1&&merged.rewrites[0].residual&&*merged.rewrites[0].residual<2e-13,"rotation merge lacks checked evidence");
    require(merged.rewrites[0].threshold==(flatten(result).instructions.empty()?dialect::kMatrixRecognitionTolerance:1e-9),"rotation merge reports an unapplied guard");
  }
  // Deliberately inject later bad trials into the minimum-point scheduler.
  // Their observed discrepancies must not enter the selected artifact trace.
  WireCircuit initial;initial.numQubits=2;
  initial.instructions={{OpKind::Rx,{0},{angleAttr(.2)},{}},{OpKind::Rx,{0},{angleAttr(.1)},{}}};
  passes::CompilationReport selected;selected.hasNonunitaryOperations=true;int trial=0;
  {passes::CompilationReportScope scope(&selected);
    result=passes::OptimizationLoop<passes::MinimumPointCriterion>{}.run(rebuild(initial),[&](const Module& input){
      const auto before=flatten(input);auto next=before;++trial;
      if(trial==1)next.instructions={{OpKind::Rx,{0},{angleAttr(.3)},{}}};
      else next.instructions.push_back({OpKind::Ry,{0},{angleAttr(.7)},{}});
      passes::observeRewrite("audit",trial==1?"kept":"discarded",before.instructions,next.instructions,
                            0,0,std::nullopt,trial==1?"unitary-2":"unitary-4");
      ++passes::currentCompilationReport()->counters["audit_trials"];
      return rebuild(next);
    },passes::MinimumPointCriterion{},4);
  }
  require(flatten(result).instructions.size()==1,"minimum-point scheduler lost its incumbent");
  require(selected.rewrites.size()==1&&selected.rewrites[0].rule=="kept","discarded trials contaminated accepted residual totals");
  require(selected.counters.at("audit_trials")==3,"discarded trial count was lost");
  require(selected.json().find("\"id\":\"unitary-2\"")!=std::string::npos,"accepted dynamic region was lost");
  require(selected.json().find("\"id\":\"unitary-4\"")==std::string::npos,"discarded candidate introduced a region aggregate");
  require(selected.json().find("\"aggregation_available\":true")!=std::string::npos,"accepted minimum-point observations lost their sum");
  require(error(complete(initial),complete(flatten(result)))<2e-13,"retained minimum-point candidate has wrong full operator");
  // Every pass must use the same fence numbering. In particular, recursive
  // native range normalization must retain the enclosing branch region, and
  // decomposition must count If/Else/EndIf as separate region boundaries.
  registry::ChipInfo dynamicTarget=chip;
  dynamicTarget.supports.midCircuitMeasure=true;dynamicTarget.supports.reset=true;
  dynamicTarget.supports.feedforward=registry::CapabilityFlags::Feedforward::Full;
  dynamicTarget.classicalFeatures["classical.const"]="supported";
  WireCircuit dynamic;dynamic.target="generic";dynamic.numQubits=2;dynamic.numClbits=2;
  dynamic.classicalStorage={{"scratch",1,{1},"private",true,"0"}};
  dynamic.classicalValues={{"scratch_value","bool",1,{1},"private",false,""}};
  auto block=[&]{
    dynamic.instructions.push_back({OpKind::H,{0},{},{}});
    dynamic.instructions.push_back({OpKind::Rx,{0},{angleAttr(4.1)},{}});
  };
  block();dynamic.instructions.push_back({OpKind::Barrier,{},{},{}});
  block();dynamic.instructions.push_back({OpKind::Measure,{1},{},{},0});
  block();dynamic.instructions.push_back({OpKind::If,{},{{"condition_clbit",0.0},{"condition_value",1.0}},{},0});
  block();dynamic.instructions.push_back({OpKind::Else,{},{},{}});
  block();dynamic.instructions.push_back({OpKind::EndIf,{},{},{}});
  block();dynamic.instructions.push_back({OpKind::Reset,{0},{},{}});
  block();dynamic.instructions.push_back({OpKind::CConst,{},{{"result",std::string("scratch_value")},{"value",std::string("1")}},{}});
  block();
  std::set<std::string> expectedRegions;
  for(std::size_t i=0;i<8;++i)expectedRegions.insert(passes::numericalRegion(i));
  for(const auto level:{passes::OptimizationLevel::O0,passes::OptimizationLevel::O1,
                       passes::OptimizationLevel::O2,passes::OptimizationLevel::O3}){
    Diagnostics dynamicDiagnostics;passes::NumericalReport dynamicReport;
    passes::PassManager{}.compile(rebuild(dynamic),dynamicTarget,level,dynamicDiagnostics,&dynamicReport);
    require(!dynamicDiagnostics.hasErrors(),"dynamic numerical-region fixture failed to compile");
    require(dynamicReport.hasNonunitaryOperations,"dynamic report lost its nonunitary coverage flag");
    std::set<std::string> decompositionRegions,canonicalRegions;
    for(const auto& event:dynamicReport.rewrites){
      if(event.pass=="native.decomposition")decompositionRegions.insert(event.region);
      if(event.pass=="native.parameter-canonicalization")canonicalRegions.insert(event.region);
      require(event.residual&&*event.residual<2e-13,"dynamic local reconstruction lacks a measured residual");
    }
    require(decompositionRegions==expectedRegions,"decomposition merged regions across a dynamic fence");
    require(canonicalRegions==expectedRegions,"native parameter normalization lost the enclosing region");
    const auto json=dynamicReport.json();
    require(json.find("\"sum_observed_local_residuals\":null")==std::string::npos,"measured dynamic region sum was suppressed");
    require(json.find("\"whole_program_error\":null")!=std::string::npos,"dynamic region sum mislabeled as global error");
    require(json.find("\"certified\":false")!=std::string::npos,"dynamic local evidence was certified");
  }
  // Explicit controller/output contracts take precedence over legacy flags.
  registry::ChipInfo controller=chip;controller.supports.midCircuitMeasure=true;
  controller.supports.feedforward=registry::CapabilityFlags::Feedforward::Full;
  WireCircuit exported;exported.target="generic";exported.numQubits=1;exported.numClbits=1;exported.exportedClbits={0};
  exported.classicalStorage={{"s0",1,{0},"exported",true,"0"}};
  exported.classicalValues={{"flag","bool",1,{0},"exported",false,""}};
  exported.classicalOutputs={{"flag","flag","bool",1,"value"}};
  exported.instructions={{OpKind::Measure,{0},{{"result",std::string("flag")}},{},0}};
  auto admission=[&](const WireCircuit& source,const registry::ChipInfo& target,const std::string& expectedError){
    const auto module=rebuild(source);Diagnostics structural;spinor::dialect::verify(module,structural);
    require(!structural.hasErrors(),"capability fixture failed structural validation before admission");
    Diagnostics checks;passes::PassManager{}.compile(module,target,passes::OptimizationLevel::O0,checks);
    std::string actual;for(const auto& entry:checks.items())actual+=" ["+entry.message+"]";
    if(expectedError.empty()){
      if(checks.hasErrors())throw std::runtime_error("advertised explicit capability was rejected:"+actual);
    }
    else{bool found=false;for(const auto& entry:checks.items())found|=entry.message.find(expectedError)!=std::string::npos;
      if(!checks.hasErrors()||!found)throw std::runtime_error("expected capability diagnostic '"+expectedError+"', received:"+actual);}
  };
  admission(exported,controller,"output.classical");
  controller.classicalFeatures["output.classical"]="supported";
  admission(exported,controller,"");
  exported.classicalOutputs[0].role="loop_exhausted";
  admission(exported,controller,"output.loop_exhausted");
  controller.classicalFeatures["output.loop_exhausted"]="supported";
  admission(exported,controller,"");
  exported.instructions.insert(exported.instructions.end(),{
    {OpKind::If,{},{{"condition_clbit",0.0},{"condition_value",1.0}},{},0},
    {OpKind::X,{0},{},{}},{OpKind::EndIf,{},{},{}}});
  controller.classicalFeatures["branching.bit"]="unsupported";
  admission(exported,controller,"branching.bit");
  controller.classicalFeatures["branching.bit"]="unknown";
  admission(exported,controller,"branching.bit");
  controller.classicalFeatures.erase("branching.bit");
  admission(exported,controller,"");
  controller.classicalFeatures["branching.bit"]="supported";
  controller.supports.feedforward=registry::CapabilityFlags::Feedforward::None;
  admission(exported,controller,"");
  controller.classicalFeatures["measure"]="unsupported";
  admission(exported,controller,"measure");
  controller.classicalFeatures["measure"]="unknown";
  admission(exported,controller,"measure");
  controller.classicalFeatures.erase("measure");
  admission(exported,controller,"");
  WireCircuit resetting;resetting.target="generic";resetting.numQubits=1;resetting.instructions={{OpKind::Reset,{0},{},{}}};
  controller.supports.reset=true;controller.classicalFeatures["reset"]="unsupported";
  admission(resetting,controller,"reset");
  controller.supports.reset=false;controller.classicalFeatures["reset"]="supported";
  admission(resetting,controller,"");
  controller.classicalFeatures["reset"]="unknown";
  admission(resetting,controller,"reset");
  controller.supports.reset=true;
  admission(resetting,controller,"reset");
  controller.classicalFeatures.erase("reset");
  admission(resetting,controller,"");
  std::cout<<"optimization evidence checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

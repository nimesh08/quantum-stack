#include "../support/test_main.h"
#include "spinor/passes/TwoQubitDecomposer.h"
#include "spinor/passes/ConsolidateBlocks.h"
#include "spinor/passes/KakResynthesis.h"
#include "spinor/passes/VF2PostLayout.h"
#include "spinor/passes/PassManager.h"
#include "../../passes/lib/TwoQubitMath.h"
#include <random>
#include <numbers>

using namespace spinor;
using namespace spinor::dialect;
using namespace spinor::passes;
using namespace spinor::passes::la;
using namespace spinor::passes::twoq;
constexpr double pi=std::numbers::pi;

static registry::ChipInfo chipFor(std::string entangler) {
  registry::ChipInfo chip;chip.id="test_"+entangler;chip.qubits=2;chip.allToAll=true;
  chip.decompose.twoQubitEntangler=entangler;
  if(entangler=="sqrt_iswap"||entangler=="sqrt_iswap_inv"||entangler=="syc")chip.nativeGates={"phased_xz",entangler};
  else if(entangler=="iswap"||entangler=="rxx")chip.nativeGates={"rx","rz",entangler};
  else if(entangler=="ms")chip.nativeGates={"gpi","gpi2","ms"};
  else if(entangler=="rzz")chip.nativeGates={"u1q","rz","rzz"};
  else if(entangler=="cz"||entangler=="ecr")chip.nativeGates={"rz","sx",entangler};
  else chip.nativeGates={"rz","ry",entangler};
  chip.decompose.oneQubitRotationGate=entangler=="ms"?"gpi":"rz";
  chip.decompose.oneQubitPi2Gate=entangler=="ms"?"gpi2":"sx";
  return chip;
}
static Mat4 haar(std::mt19937_64& random) {
  std::normal_distribution<double> normal;
  Mat4 u;
  for(int c=0;c<4;++c){
    std::array<cdbl,4> v;for(auto& x:v)x={normal(random),normal(random)};
    for(int previous=0;previous<c;++previous){
      cdbl dot=0;for(int r=0;r<4;++r)dot+=std::conj(u(r,previous))*v[r];
      for(int r=0;r<4;++r)v[r]-=dot*u(r,previous);
    }
    double length=0;for(auto x:v)length+=std::norm(x);length=std::sqrt(length);
    for(int r=0;r<4;++r)u(r,c)=v[r]/length;
  }
  return u;
}
static KakResult checkSynthesis(const Mat4& u,const registry::ChipInfo& chip) {
  auto result=TwoQubitDecomposer{}.decompose(pack(u),computeTraits(chip),&chip);
  EXPECT_TRUE(result.entanglerUses<=computeTraits(chip).entanglerCountMax);
  auto actual=scale(compose(result.operations),std::polar(1.0,result.globalPhase));
  EXPECT_TRUE(distance(u,actual)<1e-9);
  for(const auto& op:result.operations){
    auto name=std::string(opMnemonic(op.kind)).substr(7);
    EXPECT_TRUE(std::find(chip.nativeGates.begin(),chip.nativeGates.end(),name)!=chip.nativeGates.end());
    if(op.kind==OpKind::Rxx){EXPECT_TRUE(parameter(op)>=0);EXPECT_TRUE(parameter(op)<=pi/2);}
  }
  return result;
}

TEST(KAK, haar_random_all_native_bases) {
  std::mt19937_64 random(0x4b414b2026);
  for(int sample=0;sample<64;++sample){
    auto u=haar(random);
    for(const auto& basis:{"cx","cz","ecr","ms","rxx","rzz"})checkSynthesis(u,chipFor(basis));
  }
}
TEST(KAK, global_phase_and_degenerate_eigenspaces) {
  for(const auto& basis:{"cx","cz","ecr","ms","rxx","rzz"}){
    auto chip=chipFor(basis);
    for(const auto& u:{identity4(),CX(),CZ(),SWAP(),ECR(),MS(),RZZ(.37),
                       kron(Rx(.7),Rz(-2.8)),mul4(MS(.4),RZZ(.4))}){
      for(double phase:{0.0,pi/2,-pi+.000001,2.7})
        checkSynthesis(scale(u,std::polar(1.0,phase)),chip);
    }
  }
}
TEST(KAK, swap_family_native_bases) {
  std::mt19937_64 random(0x676f6f676c65);
  for(const auto& basis:{"sqrt_iswap","sqrt_iswap_inv","syc","iswap"}){
    auto chip=chipFor(basis);
    for(int sample=0;sample<24;++sample)checkSynthesis(haar(random),chip);
    for(const auto& u:{identity4(),CX(),CZ(),SWAP(),MS(.2)})checkSynthesis(u,chip);
  }
}
TEST(KAK, optimal_entangler_classes) {
  auto chip=chipFor("cx");
  EXPECT_EQ(checkSynthesis(identity4(),chip).entanglerUses,0);
  EXPECT_EQ(checkSynthesis(kron(Ry(.7),Rz(.4)),chip).entanglerUses,0);
  EXPECT_EQ(checkSynthesis(CX(),chip).entanglerUses,1);
  EXPECT_EQ(checkSynthesis(CZ(),chip).entanglerUses,1);
  EXPECT_EQ(checkSynthesis(RZZ(.37),chip).entanglerUses,2);
  EXPECT_EQ(checkSynthesis(SWAP(),chip).entanglerUses,3);
  EXPECT_EQ(checkSynthesis(RZZ(.37),chipFor("rzz")).entanglerUses,1);
  EXPECT_EQ(checkSynthesis(MS(.37),chipFor("rxx")).entanglerUses,1);
}
TEST(KAK, near_local_and_weyl_boundaries) {
  for(double angle:{1e-10,1e-7,pi/2-1e-9,pi/2,pi/2+1e-9,pi-1e-8}){
    checkSynthesis(MS(angle),chipFor("cx"));
    checkSynthesis(mul4(MS(angle),RZZ(angle)),chipFor("ecr"));
  }
}
TEST(KAK, directed_cx_and_ecr) {
  std::mt19937_64 random(42);
  for(const auto& basis:{"cx","ecr"}){
    auto chip=chipFor(basis);chip.allToAll=false;chip.directedConnectivity=true;chip.coupling={{1,0}};
    for(int i=0;i<8;++i){
      auto result=checkSynthesis(haar(random),chip);
      for(const auto& op:result.operations)if(op.qubits.size()==2){
        EXPECT_EQ(op.qubits[0],1);EXPECT_EQ(op.qubits[1],0);
      }
    }
  }
}
TEST(KAK, rejects_nonunitary_and_exhausted_budget) {
  bool rejected=false;auto bad=identity4();bad(0,0)=2;
  try{checkSynthesis(bad,chipFor("cx"));}catch(const std::invalid_argument&){rejected=true;}
  EXPECT_TRUE(rejected);
  rejected=false;auto traits=computeTraits(chipFor("cx"));traits.entanglerCountMax=1;
  try{TwoQubitDecomposer{}.decompose(pack(SWAP()),traits);}catch(const std::runtime_error&){rejected=true;}
  EXPECT_TRUE(rejected);
}
TEST(KAK, deterministic_output) {
  std::mt19937_64 random(2026);auto u=haar(random);auto chip=chipFor("cz");
  const auto a=checkSynthesis(u,chip),b=checkSynthesis(u,chip);
  EXPECT_EQ(a.operations.size(),b.operations.size());EXPECT_EQ(a.globalPhase,b.globalPhase);
  for(std::size_t i=0;i<a.operations.size();++i){
    EXPECT_EQ(int(a.operations[i].kind),int(b.operations[i].kind));
    EXPECT_TRUE(a.operations[i].qubits==b.operations[i].qubits);
    EXPECT_EQ(parameter(a.operations[i]),parameter(b.operations[i]));
  }
}

TEST(KAK, aqt_negative_angles_keep_one_native_entangler_after_pipeline) {
  auto chip=chipFor("rxx");
  for(double angle:{-.07,-pi/2,pi/2}){
    auto synthesized=checkSynthesis(MS(angle),chip);
    EXPECT_EQ(synthesized.entanglerUses,1);
    WireCircuit c{"aqt_bounded",chip.id,2,0,synthesized.globalPhase,synthesized.operations};
    Diagnostics diagnostics;
    auto compiled=flatten(PassManager{}.compile(rebuild(c),chip,OptimizationLevel::O2,diagnostics));
    EXPECT_FALSE(diagnostics.hasErrors());
    std::size_t count=0;
    for(const auto& op:compiled.instructions)if(op.qubits.size()==2)++count;
    EXPECT_EQ(count,std::size_t(1));
    EXPECT_TRUE(distance(MS(angle),scale(compose(compiled.instructions),
                       std::polar(1.0,compiled.globalPhase)))<1e-9);
  }
}

static std::size_t entanglers(const WireCircuit& c) {
  std::size_t count=0;for(const auto& op:c.instructions)if(op.qubits.size()==2)++count;return count;
}
TEST(KAK, block_composition_and_native_cost_improvement) {
  WireCircuit c{"block","test_cx",2,0,.37,{}};
  for(int i=0;i<8;++i){
    c.instructions.push_back({OpKind::Cx,{0,1},{},{}});
    c.instructions.push_back({OpKind::Ry,{1},{angleAttr(.071*(i+1))},{}});
    c.instructions.push_back({OpKind::Rz,{0},{angleAttr(.093*(i+1))},{}});
  }
  auto module=rebuild(c);auto blocks=Collect2qBlocks{}.run(module);
  auto consolidated=ConsolidateBlocks{}.run(module,blocks);
  EXPECT_EQ(consolidated.size(),1u);
  EXPECT_TRUE(distance(unpack(consolidated[0].unitary),compose(c.instructions))<1e-12);
  auto chip=chipFor("cx");
  auto result=flatten(KakResynthesis{}.run(module,consolidated,computeTraits(chip),&chip));
  EXPECT_TRUE(entanglers(result)<entanglers(c));
  EXPECT_TRUE(result.instructions.size()<=c.instructions.size());
  EXPECT_TRUE(distance(scale(compose(c.instructions),std::polar(1.0,c.globalPhase)),
                       scale(compose(result.instructions),std::polar(1.0,result.globalPhase)))<1e-9);
}
TEST(KAK, preserves_one_entangler_native_blocks) {
  auto chip=chipFor("cx");
  WireCircuit c{"block",chip.id,2,0,0,{{OpKind::Cx,{0,1},{},{}}}};
  auto m=rebuild(c);
  auto result=flatten(KakResynthesis{}.run(m,Collect2qBlocks{}.run(m),computeTraits(chip),&chip));
  EXPECT_EQ(result.instructions.size(),1u);EXPECT_EQ(entanglers(result),1u);
}
TEST(KAK, random_native_blocks_never_increase_either_gate_count) {
  std::mt19937_64 random(0x0b10c2026);std::uniform_real_distribution<double> angle(-pi,pi);
  for(const auto& basis:{"cx","cz","ecr","ms","rxx","rzz"}){
    auto chip=chipFor(basis);
    for(int sample=0;sample<12;++sample){
      WireCircuit c{"random_block",chip.id,2,0,angle(random),{}};
      for(int layer=0;layer<3;++layer){
        auto native=checkSynthesis(haar(random),chip);
        c.globalPhase+=native.globalPhase;
        c.instructions.insert(c.instructions.end(),native.operations.begin(),native.operations.end());
      }
      auto m=rebuild(c);
      auto result=flatten(KakResynthesis{}.run(m,Collect2qBlocks{}.run(m),computeTraits(chip),&chip));
      EXPECT_TRUE(entanglers(result)<=entanglers(c));
      EXPECT_TRUE(result.instructions.size()<=c.instructions.size());
      EXPECT_TRUE(distance(scale(compose(c.instructions),std::polar(1.0,c.globalPhase)),
                           scale(compose(result.instructions),std::polar(1.0,result.globalPhase)))<1e-9);
    }
  }
}
TEST(KAK, measurement_fences_and_classical_mapping) {
  auto chip=chipFor("cx");
  WireCircuit c{"readout",chip.id,2,5,.41,{}};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::Cx,{0,1},{},{}},
                  {OpKind::Measure,{1},{},{},4},{OpKind::Cx,{0,1},{},{}},
                  {OpKind::Measure,{0},{},{},2}};
  auto m=rebuild(c);auto blocks=Collect2qBlocks{}.run(m);
  EXPECT_EQ(blocks.size(),2u);
  auto result=flatten(KakResynthesis{}.run(m,blocks,computeTraits(chip),&chip));
  EXPECT_EQ(result.numClbits,5u);
  std::vector<int> bits;for(const auto& op:result.instructions)if(op.kind==OpKind::Measure)bits.push_back(op.clbit);
  EXPECT_TRUE(bits==std::vector<int>({4,2}));EXPECT_EQ(entanglers(result),1u);
  EXPECT_TRUE(std::abs(result.globalPhase-c.globalPhase)<1e-9);
}
TEST(KAK, disjoint_interleaving_is_preserved) {
  auto chip=chipFor("cx");chip.qubits=3;
  WireCircuit c{"disjoint",chip.id,3,0,0,{}};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::X,{2},{},{}},
                  {OpKind::Cx,{0,1},{},{}}};
  auto m=rebuild(c);
  auto result=flatten(KakResynthesis{}.run(m,Collect2qBlocks{}.run(m),computeTraits(chip),&chip));
  EXPECT_EQ(result.instructions.size(),1u);
  EXPECT_EQ(int(result.instructions[0].kind),int(OpKind::X));
  EXPECT_EQ(result.instructions[0].qubits[0],2);
}
TEST(KAK, rejects_blocks_that_cross_a_dependent_operation) {
  WireCircuit c{"malformed","test_cx",2,0,0,{}};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::X,{0},{},{}},
                  {OpKind::Cx,{0,1},{},{}}};
  auto m=rebuild(c);auto blocks=Collect2qBlocks{}.run(m);
  blocks[0].ops.erase(blocks[0].ops.begin()+1);
  bool rejected=false;
  try{ConsolidateBlocks{}.run(m,blocks);}catch(const std::invalid_argument&){rejected=true;}
  EXPECT_TRUE(rejected);
}
TEST(KAK, respects_global_barrier) {
  WireCircuit c{"barrier","test_cx",2,0,0,{}};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::Barrier,{},{},{}},
                  {OpKind::Cx,{0,1},{},{}}};
  auto m=rebuild(c);auto blocks=Collect2qBlocks{}.run(m);
  EXPECT_EQ(blocks.size(),2u);
}

TEST(VF2, calibrated_directed_embedding_preserves_readout) {
  auto chip=chipFor("cx");chip.qubits=4;chip.allToAll=false;chip.directedConnectivity=true;
  chip.coupling={{0,1},{1,2},{2,3}};
  WireCircuit c{"mapping",chip.id,3,5,.42,{}};
  c.initialLayout={0,1,2};c.finalLayout={0,1,2};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::Measure,{0},{},{},4},
                  {OpKind::Measure,{1},{},{},1}};
  CalibrationCosts costs;
  costs.twoQubitError={{{0,1},.3},{{1,2},.1},{{2,3},.001}};
  costs.readoutError={{0,.2},{1,.2},{2,.001},{3,.001}};
  VF2Statistics stats;
  auto result=flatten(VF2PostLayout{}.run(rebuild(c),chip,costs,10000,&stats));
  EXPECT_TRUE(stats.improved);EXPECT_TRUE(stats.selectedCost<stats.originalCost);
  EXPECT_TRUE(result.instructions[0].qubits==std::vector<int>({2,3}));
  EXPECT_EQ(result.instructions[1].qubits[0],2);EXPECT_EQ(result.instructions[1].clbit,4);
  EXPECT_EQ(result.instructions[2].qubits[0],3);EXPECT_EQ(result.instructions[2].clbit,1);
  EXPECT_EQ(result.numClbits,5u);EXPECT_EQ(result.globalPhase,c.globalPhase);
  EXPECT_TRUE(result.initialLayout==std::vector<int>({2,3,0}));
  EXPECT_TRUE(result.finalLayout==std::vector<int>({2,3,0}));
}
TEST(VF2, no_invented_calibration_or_uncalibrated_baseline) {
  auto chip=chipFor("cx");chip.qubits=4;
  WireCircuit c{"mapping",chip.id,2,0,0,{{OpKind::Cx,{0,1},{},{}}}};
  auto m=rebuild(c);VF2Statistics stats;
  EXPECT_STREQ(print(VF2PostLayout{}.run(m,chip,{},100,&stats)),print(m));
  EXPECT_FALSE(stats.improved);EXPECT_EQ(stats.visitedStates,0u);
  CalibrationCosts costs;costs.twoQubitError={{{2,3},.001}};
  EXPECT_STREQ(print(VF2PostLayout{}.run(m,chip,costs,100,&stats)),print(m));
  EXPECT_FALSE(stats.improved);
}
TEST(VF2, bounded_deterministic_search) {
  auto chip=chipFor("cx");chip.qubits=4;
  WireCircuit c{"mapping",chip.id,2,0,0,{{OpKind::Cx,{0,1},{},{}}}};
  CalibrationCosts costs;
  for(int a=0;a<4;++a)for(int b=a+1;b<4;++b)costs.twoQubitError[{a,b}]=.1/(a+b+1);
  VF2Statistics a,b;auto m=rebuild(c);
  auto first=VF2PostLayout{}.run(m,chip,costs,1,&a);
  auto second=VF2PostLayout{}.run(m,chip,costs,1,&b);
  EXPECT_TRUE(a.visitedStates<=1);EXPECT_TRUE(a.budgetExhausted);
  EXPECT_EQ(a.visitedStates,b.visitedStates);EXPECT_STREQ(print(first),print(second));
}
TEST(VF2, validates_error_probabilities) {
  auto chip=chipFor("cx");CalibrationCosts costs;costs.twoQubitError[std::pair{0,1}]=-0.1;
  bool rejected=false;
  try{VF2PostLayout{}.run(Module{},chip,costs);}catch(const std::invalid_argument&){rejected=true;}
  EXPECT_TRUE(rejected);
}
TEST(VF2, optimization_pipeline_consumes_registry_calibration) {
  auto chip=chipFor("cx");chip.qubits=4;chip.allToAll=false;chip.directedConnectivity=true;
  chip.coupling={{0,1},{1,2},{2,3}};
  chip.calibrationTwoQubitError={{{0,1},.3},{{1,2},.1},{{2,3},.001}};
  chip.calibrationReadoutError={{0,.2},{1,.2},{2,.001},{3,.001}};
  WireCircuit c{"integrated_mapping",chip.id,2,2,0,{}};
  c.instructions={{OpKind::Cx,{0,1},{},{}},{OpKind::Measure,{0},{},{},0},
                  {OpKind::Measure,{1},{},{},1}};
  Diagnostics diagnostics;
  auto result=flatten(PassManager{}.compile(rebuild(c),chip,OptimizationLevel::O3,diagnostics));
  EXPECT_FALSE(diagnostics.hasErrors());
  EXPECT_TRUE(result.instructions[0].qubits==std::vector<int>({2,3}));
}

// Reconstruct logical columns from arbitrary basis inputs, respecting both
// placement maps. This independently catches a layout that preserves only
// the all-zero input or silently swaps output wires.
static std::vector<cdbl> logicalUnitary(const WireCircuit& circuit,int logical) {
  const std::size_t width=std::size_t(1)<<logical;
  std::vector<cdbl> matrix(width*width);
  for(std::size_t column=0;column<width;++column){
    std::size_t input=0;
    for(int q=0;q<logical;++q)if(column&(std::size_t(1)<<(logical-1-q)))
      input|=std::size_t(1)<<(circuit.initialLayout.empty()?q:circuit.initialLayout[q]);
    std::vector<cdbl> state(std::size_t(1)<<circuit.numQubits);state[input]=1;
    for(const auto& op:circuit.instructions){
      const int arity=int(op.qubits.size());
      if(op.kind==OpKind::Barrier)continue;
      EXPECT_TRUE(arity==1||arity==2);
      Mat4 gate;
      if(arity==2)gate=matrix2(op);
      else {auto g=matrix1(op);for(int a=0;a<2;++a)for(int b=0;b<2;++b)gate(a,b)=g(a,b);}
      std::vector<cdbl> output(state.size());
      for(std::size_t basis=0;basis<state.size();++basis){
        int local=0;std::size_t mask=0;
        for(int j=0;j<arity;++j){local=(local<<1)|int((basis>>op.qubits[j])&1);mask|=std::size_t(1)<<op.qubits[j];}
        for(int row=0;row<(1<<arity);++row){
          auto dest=basis&~mask;
          for(int j=0;j<arity;++j)if(row&(1<<(arity-1-j)))dest|=std::size_t(1)<<op.qubits[j];
          output[dest]+=gate(row,local)*state[basis];
        }
      }
      state=std::move(output);
    }
    for(std::size_t row=0;row<width;++row){
      std::size_t output=0;
      for(int q=0;q<logical;++q)if(row&(std::size_t(1)<<(logical-1-q)))
        output|=std::size_t(1)<<(circuit.finalLayout.empty()?q:circuit.finalLayout[q]);
      matrix[row*width+column]=state[output]*std::polar(1.0,circuit.globalPhase);
    }
  }
  return matrix;
}
TEST(O3, alternative_layouts_preserve_full_logical_unitary_and_improve_star) {
  auto chip=chipFor("cx");chip.qubits=4;chip.allToAll=false;chip.coupling={{0,1},{1,2},{2,3}};
  WireCircuit source{"star",chip.id,3,0,.29,{}};
  source.instructions.push_back({OpKind::H,{0},{},{}});
  for(int repeat=0;repeat<3;++repeat){
    source.instructions.push_back({OpKind::Cx,{0,1},{},{}});
    source.instructions.push_back({OpKind::Ry,{1},{angleAttr(.23*(repeat+1))},{}});
    source.instructions.push_back({OpKind::Cx,{0,2},{},{}});
    source.instructions.push_back({OpKind::Rz,{2},{angleAttr(.17*(repeat+1))},{}});
  }
  Diagnostics d2,d3;
  auto o2=flatten(PassManager{}.compile(rebuild(source),chip,OptimizationLevel::O2,d2));
  auto o3=flatten(PassManager{}.compile(rebuild(source),chip,OptimizationLevel::O3,d3));
  EXPECT_FALSE(d2.hasErrors());EXPECT_FALSE(d3.hasErrors());
  EXPECT_TRUE(entanglers(o3)<entanglers(o2));
  EXPECT_TRUE(o3.instructions.size()<=o2.instructions.size());
  auto expected=logicalUnitary(source,3),actual=logicalUnitary(o3,3);
  for(std::size_t i=0;i<expected.size();++i)EXPECT_TRUE(std::abs(expected[i]-actual[i])<1e-9);
  Diagnostics again;
  EXPECT_STREQ(print(PassManager{}.compile(rebuild(source),chip,OptimizationLevel::O3,again)),print(rebuild(o3)));
}
SPINOR_TEST_MAIN()

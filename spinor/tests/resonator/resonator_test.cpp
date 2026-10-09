#include "spinor/dialect/Resonators.h"
#include "spinor/registry/ComponentTopology.h"
#include "spinor/passes/PassManager.h"
#include "spinor/sim/Simulator.h"
#include "spinor/emit/Emitters.h"
#include "test_main.h"
#include <complex>
#include <numbers>
#include <random>

using namespace spinor;
using namespace spinor::dialect;
using passes::OptimizationLevel;

namespace {
registry::ChipInfo twoStars() {
  Diagnostics d;auto r=registry::Registry::load(RESONATOR_REGISTRY,d);
  if(d.hasErrors()){for(const auto& error:d.items())std::cerr<<error.message<<'\n';throw std::runtime_error("fixture load failed");}
  return r.get("iqm_move_test");
}
Module compile(const Module& source,const registry::ChipInfo& chip,OptimizationLevel level) {
  Diagnostics d;auto m=passes::PassManager{}.compile(source,chip,level,d);
  if(d.hasErrors()){for(const auto& error:d.items())std::cerr<<error.message<<'\n';throw std::runtime_error("resonator compile failed");}
  EXPECT_TRUE(passes::validateCompiled(m,chip,d));
  EXPECT_FALSE(d.hasErrors());return m;
}
std::size_t count(const Module& m,OpKind kind) {
  std::size_t n=0;for(const auto& op:m.ops())if(op.kind==kind)++n;return n;
}
template<class F> void rejects(F&& f) { bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}EXPECT_TRUE(rejected); }
void exactEquivalent(const Module& a,const Module& b) {
  auto check=sim::equivalent(a,b,1e-9);EXPECT_TRUE(check.equivalent);
  EXPECT_TRUE(std::abs(check.phase.value_or(std::complex<double>{1,0})-std::complex<double>{1,0})<1e-9);
}
registry::ChipInfo singleStar() {
  auto chip=twoStars();chip.qubits=3;chip.computationalQubits={0,2};chip.resonatorQubits={1};
  chip.moveLoci={{0,1}};chip.czLoci={{2,1}};chip.coupling={{0,1},{2,1}};return chip;
}
Module czSource() { Module m;m.targetAttr="generic";Builder b(m);auto a=b.allocQubit(),z=b.allocQubit();b.cz(a,z);return m; }
}

TEST(Resonator, metadata_and_reserved_slot_placement) {
  auto chip=twoStars();EXPECT_EQ(chip.qubits,std::size_t(5));
  EXPECT_TRUE(registry::computationalComponents(chip)==std::vector<int>({0,2,3}));
  EXPECT_TRUE(chip.resonatorQubits==std::vector<int>({1,4}));
  for(auto level:{OptimizationLevel::O0,OptimizationLevel::O1,OptimizationLevel::O2,OptimizationLevel::O3}) {
    auto native=compile(czSource(),chip,level);auto wire=flatten(native);
    EXPECT_EQ(wire.numQubits,std::size_t(5));EXPECT_EQ(wire.initialLayout.size(),std::size_t(2));
    EXPECT_TRUE(count(native,OpKind::Move)>=2);
    for(int q:wire.initialLayout)EXPECT_TRUE(q!=1&&q!=4);
    for(int q:wire.finalLayout)EXPECT_TRUE(q!=1&&q!=4);
    auto text=print(native);Diagnostics d;auto parsed=parse(text,d);
    EXPECT_TRUE(parsed.has_value());EXPECT_TRUE(parsed->resonatorQubits==native.resonatorQubits);
    exactEquivalent(czSource(),*parsed);
    auto json=emit::emitPhysicalJson(*parsed);
    EXPECT_CONTAINS(json,"\"resonator_qubits\":[1,4]");
    EXPECT_CONTAINS(json,"\"computational_qubits\":[0,2,3]");
    EXPECT_CONTAINS(json,"\"op\":\"move\"");
  }
  auto bad=chip;bad.moveLoci={{1,0}};rejects([&]{registry::computationalComponents(bad);});
  bad=chip;bad.computationalQubits={0,1,2};rejects([&]{registry::computationalComponents(bad);});
  Module tooLarge;tooLarge.targetAttr="generic";Builder b(tooLarge);
  for(int i=0;i<4;++i)b.allocQubit();Diagnostics d;
  passes::PassManager{}.compile(tooLarge,chip,OptimizationLevel::O0,d);EXPECT_TRUE(d.hasErrors());
}

TEST(Resonator, arbitrary_move_phase_cancels_without_swap_assumption) {
  auto native=flatten(compile(czSource(),singleStar(),OptimizationLevel::O0));
  EXPECT_EQ(native.instructions.size(),std::size_t(3));
  for(double phi:{-2.7,-0.3,0.0,0.2,std::numbers::pi/2,2.9})for(unsigned basis=0;basis<4;++basis) {
    std::vector<std::complex<double>> state(8);unsigned physical=0;
    for(unsigned q=0;q<2;++q)if(basis&(1U<<q))physical|=1U<<native.initialLayout[q];
    state[physical]=1;
    for(const auto& op:native.instructions) {
      const unsigned q=1U<<op.qubits[0],r=1U<<op.qubits[1];
      if(op.kind==OpKind::Move) {
        const auto phase=std::polar(1.0,phi);
        for(unsigned i=0;i<8;++i)if(!(i&q)&&!(i&r)) {
          EXPECT_TRUE(std::abs(state[i|q|r])<1e-12);
          auto a=state[i|q],z=state[i|r];state[i|q]=phase*z;state[i|r]=std::conj(phase)*a;
        }
      } else {
        EXPECT_TRUE(op.kind==OpKind::Cz);
        for(unsigned i=0;i<8;++i)if((i&q)&&(i&r))state[i]=-state[i];
      }
    }
    for(unsigned i=0;i<8;++i) {
      auto expected=i==physical?std::complex<double>(basis==3?-1:1,0):std::complex<double>{};
      EXPECT_TRUE(std::abs(state[i]-expected)<1e-12);
    }
  }
}

TEST(Resonator, multi_resonator_routing_exact_for_random_circuits) {
  auto chip=twoStars();std::mt19937 rng(53);std::uniform_real_distribution<double> angle(-5,5);
  for(int trial=0;trial<12;++trial) {
    Module logical;logical.targetAttr="generic";Builder b(logical);
    auto a=b.allocQubit(),c=b.allocQubit(),d=b.allocQubit();
    a=b.ry(angle(rng),a);c=b.h(c);d=b.rx(angle(rng),d);
    auto pair=b.cx(a,c);a=pair.first;c=pair.second;
    pair=b.rzz(angle(rng),c,d);c=pair.first;d=pair.second;
    pair=b.cx(a,d);a=pair.first;d=pair.second;
    a=b.rz(angle(rng),a);d=b.ry(angle(rng),d);
    std::size_t baseline=0;
    for(auto level:{OptimizationLevel::O0,OptimizationLevel::O1,OptimizationLevel::O2,OptimizationLevel::O3}) {
      auto native=compile(logical,chip,level);exactEquivalent(logical,native);
      EXPECT_EQ(count(native,OpKind::Swap),std::size_t(0));
      if(level==OptimizationLevel::O0)baseline=flatten(native).instructions.size();
      else EXPECT_TRUE(flatten(native).instructions.size()<=baseline);
    }
  }
}

TEST(Resonator, adjacent_sandwiches_reuse_the_same_resonator) {
  auto chip=twoStars();chip.qubits=4;chip.computationalQubits={0,1,2};chip.resonatorQubits={3};
  chip.moveLoci={{0,3}};chip.czLoci={{1,3},{2,3}};chip.coupling={{0,3},{1,3},{2,3}};
  Module m;m.targetAttr="generic";Builder b(m);auto a=b.allocQubit(),c=b.allocQubit(),d=b.allocQubit();
  auto pair=b.cz(a,c);b.cz(pair.first,d);
  auto raw=compile(m,chip,OptimizationLevel::O0),optimized=compile(m,chip,OptimizationLevel::O1);
  EXPECT_EQ(count(raw,OpKind::Move),std::size_t(4));EXPECT_EQ(count(optimized,OpKind::Move),std::size_t(2));
  exactEquivalent(m,optimized);
}

TEST(Resonator, invalid_native_state_transfers_are_rejected) {
  const auto chip=singleStar();auto good=flatten(compile(czSource(),chip,OptimizationLevel::O0));
  std::vector<WireCircuit> invalid;
  auto bad=good;bad.instructions.pop_back();invalid.push_back(bad);
  bad=good;std::swap(bad.instructions.front().qubits[0],bad.instructions.front().qubits[1]);invalid.push_back(bad);
  bad=good;bad.instructions.back().qubits[0]=2;invalid.push_back(bad);
  bad=good;bad.instructions.insert(bad.instructions.begin()+1,{OpKind::X,{0},{},{}});invalid.push_back(bad);
  bad=good;bad.instructions.insert(bad.instructions.begin()+1,{OpKind::Measure,{1},{},{},0});bad.numClbits=1;invalid.push_back(bad);
  bad=good;bad.instructions.insert(bad.instructions.begin()+1,{OpKind::Barrier,{},{},{}});invalid.push_back(bad);
  bad=good;bad.initialLayout[0]=1;invalid.push_back(bad);
  for(const auto& circuit:invalid) {
    rejects([&]{validateResonatorCircuit(circuit);});
    Diagnostics d;EXPECT_FALSE(passes::validateCompiled(rebuild(circuit),chip,d));
  }
  bad=good;std::swap(bad.instructions[1].qubits[0],bad.instructions[1].qubits[1]);
  Diagnostics d;EXPECT_FALSE(passes::validateCompiled(rebuild(bad),chip,d)); // symmetric matrix, uncalibrated operand order
  auto module=rebuild(good);
  rejects([&]{emit::emitQasm3(module);});rejects([&]{emit::emitQir(module);});rejects([&]{emit::emitQuil(module);});
}

TEST(Resonator, dynamic_boundaries_and_sparse_measurements) {
  auto chip=singleStar();Module m;m.targetAttr="generic";Builder b(m);
  auto a=b.allocQubit(),c=b.allocQubit();a=b.x(a);auto bit=b.measure(a);setMeasurementTarget(m,bit,4);
  a=b.reset(a);b.beginIf(4,true);auto pair=b.cz(a,c);b.elseBranch();pair=b.cz(pair.first,pair.second);b.endIf();
  c=b.x(pair.second);bit=b.measure(c);setMeasurementTarget(m,bit,1);m.numClbits=5;
  for(auto level:{OptimizationLevel::O0,OptimizationLevel::O3}) {
    auto native=compile(m,chip,level);validateResonatorCircuit(flatten(native));
    std::mt19937_64 random(2);auto counts=sim::sample(native,128,random);
    EXPECT_EQ(counts.size(),std::size_t(1));EXPECT_EQ(counts.at("10010"),std::size_t(128));
    for(const auto& op:flatten(native).instructions)if(op.kind==OpKind::Measure)EXPECT_TRUE(op.qubits[0]!=1);
  }
}

TEST(Resonator, high_physical_resonator_indices_do_not_expand_shot_statevector) {
  auto chip=singleStar();chip.qubits=66;chip.resonatorQubits={65};chip.computationalQubits.clear();
  for(int q=0;q<65;++q)chip.computationalQubits.push_back(q);
  chip.moveLoci={{52,65}};chip.czLoci={{63,65}};chip.coupling={{52,65},{63,65}};
  Module bell;bell.targetAttr="generic";Builder b(bell);auto a=b.allocQubit(),c=b.allocQubit();
  a=b.h(a);auto pair=b.cx(a,c);auto first=b.measure(pair.first),second=b.measure(pair.second);
  setMeasurementTarget(bell,first,0);setMeasurementTarget(bell,second,2);bell.numClbits=3;
  auto native=compile(bell,chip,OptimizationLevel::O2);
  EXPECT_EQ(flatten(native).numQubits,std::size_t(66));
  auto mapping=native.finalLayout;std::sort(mapping.begin(),mapping.end());
  EXPECT_TRUE(mapping==std::vector<int>({52,63}));
  exactEquivalent(bell,native);
  std::mt19937_64 rng(71);auto counts=sim::sample(native,256,rng);
  EXPECT_EQ(counts.size(),std::size_t(2));EXPECT_EQ(counts.at("000")+counts.at("101"),std::size_t(256));
}

SPINOR_TEST_MAIN()

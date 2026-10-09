#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"
#include "phonon/lower/Lowering.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/sim/Simulator.h"
#include "test_main.h"
#include <algorithm>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <map>

namespace sd=spinor::dialect;
namespace {
sd::Module lower(const std::string& body) {
  auto parsed=photon::lang::parse("target generic\nkernel test() {\n"+body+"\n}\n");
  if(!parsed.module){for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';throw std::runtime_error("Photon parse failed");}
  auto logical=photon::lang::lowerToPhonon(*parsed.module);
  if(!logical.module){for(const auto& d:logical.diag.items())std::cerr<<d.message<<'\n';throw std::runtime_error("Photon lowering failed");}
  auto physical=phonon::lower::lower(*logical.module);
  if(!physical.module){for(const auto& d:physical.diag.items())std::cerr<<d.message<<'\n';throw std::runtime_error("Phonon lowering failed");}
  return *physical.module;
}
std::map<std::string,std::uint64_t> outputs(const sd::Module& module,const std::string& bits) {
  std::map<std::string,std::uint64_t> result;
  for(const auto& output:module.classicalOutputs){
    auto value=std::find_if(module.classicalValues.begin(),module.classicalValues.end(),[&](const auto& v){return v.id==output.value;});
    if(value==module.classicalValues.end())throw std::runtime_error("missing output metadata");
    std::uint64_t integer=0;
    for(std::size_t bit=0;bit<value->storage.size();++bit)if(bits.at(bits.size()-1-value->storage[bit])=='1')integer|=std::uint64_t{1}<<bit;
    result[output.name]=integer;
  }
  return result;
}
void expectOutputs(const sd::Module& module,const std::map<std::string,std::uint64_t>& expected) {
  std::mt19937_64 rng(42);auto counts=spinor::sim::sample(module,32,rng);
  for(const auto& [bits,n]:counts){EXPECT_TRUE(n>0);EXPECT_TRUE(outputs(module,bits)==expected);}
}
bool rejected(const std::string& body){try{lower(body);return false;}catch(const std::exception&){return true;}}
struct BudgetSetting {
  std::string previous;bool present=false;
  explicit BudgetSetting(const char* value){
    if(const auto* old=std::getenv("QSTACK_EXPANDED_OPERATION_BUDGET")){previous=old;present=true;}
    set(value);
  }
  static void set(const char* value){
#ifdef _WIN32
    _putenv_s("QSTACK_EXPANDED_OPERATION_BUDGET",value?value:"");
#else
    if(value)setenv("QSTACK_EXPANDED_OPERATION_BUDGET",value,1);else unsetenv("QSTACK_EXPANDED_OPERATION_BUDGET");
#endif
  }
  ~BudgetSetting(){set(present?previous.c_str():nullptr);}
};
}

TEST(Photon_controller,saved_measurement_is_immutable_after_register_overwrite) {
  auto module=lower("QReg q(1)\nBit measured[1]\nq.x(0)\nmeasured[0] = q.measure(0)\n"
    "Bit saved = measured[0]\nq.reset(0)\nmeasured[0] = q.measure(0)\n"
    "Bit current = measured[0]\noutput saved\noutput current\n");
  expectOutputs(module,{{"saved",1},{"current",0}});
}

TEST(Photon_controller,unsigned_widths_wrap_and_cast_explicitly) {
  auto module=lower("QReg q(1)\nUInt<8> counter = 255\ncounter = counter + 2\n"
    "UInt<64> wide = UInt<64>(18446744073709551615)\nwide = wide + 1\n"
    "UInt<4> narrowed = UInt<4>(counter)\noutput counter\noutput wide\noutput narrowed\n");
  expectOutputs(module,{{"counter",1},{"wide",0},{"narrowed",1}});
}

TEST(Photon_controller,boolean_expressions_and_branch_ssa_join) {
  for(const auto* preparation:{"","q.x(0)\n"}){
    const bool one=std::string(preparation).size()>0;
    auto module=lower(std::string("QReg q(1)\n")+preparation+"Bit observed = q.measure(0)\n"
      "Bit saved = observed\nUInt<8> counter = 7\nif (saved & !false) {\ncounter = counter + 2\n"
      "} else {\ncounter = counter - 3\n}\noutput counter\n");
    expectOutputs(module,{{"counter",one?9U:4U}});
  }
}

TEST(Photon_controller,bounded_loop_guards_every_iteration_and_exposes_exhaustion) {
  for(int limit:{2,4}){
    auto module=lower("QReg q(1)\nUInt<8> counter = 0\nwhile (counter < 3) max_iterations "+
      std::to_string(limit)+" {\ncounter = counter + 1\nq.x(0)\n}\n"
      "Bit measured = q.measure(0)\noutput counter\noutput measured\n");
    expectOutputs(module,{{"counter",limit==2?2U:3U},{"measured",limit==2?0U:1U},{"loop_exhausted_0",limit==2?1U:0U}});
  }
}

TEST(Photon_controller,bounded_measurement_loop_stops_when_bit_changes) {
  auto module=lower("QReg q(1)\nBit done = false\nUInt<8> count = 0\nwhile (!done) max_iterations 3 {\n"
    "q.x(0)\ndone = q.measure(0)\ncount = count + 1\n}\noutput done\noutput count\n");
  expectOutputs(module,{{"done",1},{"count",1},{"loop_exhausted_0",0}});
}

TEST(Photon_controller,conditional_quantum_registers_have_distinct_static_capacity_and_scope) {
  auto module=lower("QReg q(1)\nq.h(0)\nBit coin = q.measure(0)\n"
    "if(coin) {\nQReg temporary(1)\ntemporary.x(0)\n} else {\nQReg temporary(1)\ntemporary.h(0)\n}\n");
  EXPECT_EQ(sd::flatten(module).numQubits,std::size_t(3));
  EXPECT_TRUE(rejected("QReg q(1)\nBit coin = q.measure(0)\nif(coin) {\nQReg temporary(1)\n}\ntemporary.x(0)\n"));
}

TEST(Photon_controller,conditional_return_skips_the_continuation_on_its_taken_path) {
  for(const auto* preparation:{"","q.x(0)\n"}){
    auto module=lower(std::string("QReg q(1)\n")+preparation+"Bit coin = q.measure(0)\n"
      "if(coin) {\nreturn q.measure_int()\n}\nq.x(0)\nreturn q.measure_int()\n");
    std::mt19937_64 rng(42);auto counts=spinor::sim::sample(module,32,rng);
    for(const auto& [bits,n]:counts){EXPECT_EQ(bits.back(),'1');EXPECT_TRUE(n>0);}
  }
}

TEST(Photon_controller,conditional_scalar_returns_merge_with_exact_width) {
  for(const auto* preparation:{"","q.x(0)\n"}){
    auto module=lower(std::string("QReg q(1)\n")+preparation+"Bit coin = q.measure(0)\n"
      "UInt<8> value = 7\nif(coin) {\nreturn value\n}\nvalue = value + 2\nreturn value\n");
    expectOutputs(module,{{"return",std::string(preparation).empty()?9U:7U}});
  }
  EXPECT_TRUE(rejected("QReg q(1)\nBit coin = q.measure(0)\nUInt<8> narrow = 1\nUInt<4> wide = 2\nif(coin) {\nreturn narrow\n}\nreturn wide\n"));
}

TEST(Photon_controller,bounded_break_continue_and_return_guard_the_continuation) {
  auto module=lower("QReg q(1)\nUInt<8> count = 0\nUInt<8> touched = 0\n"
    "bounded while(count < 4) max_iterations 6 {\ncount = count + 1\n"
    "if(count == 2) {\ncontinue\n}\nif(count == 3) {\nbreak\n}\ntouched = touched + 1\n}\noutput count\noutput touched\n");
  expectOutputs(module,{{"count",3},{"touched",1},{"loop_exhausted_0",0}});
  auto returning=lower("QReg q(1)\nUInt<8> count = 0\nwhile(count < 4) max_iterations 6 {\n"
    "count = count + 1\nif(count == 2) {\nreturn count\n}\n}\nq.x(0)\nreturn count\n");
  expectOutputs(returning,{{"return",2},{"loop_exhausted_0",0}});
  std::mt19937_64 rng(42);auto state=spinor::sim::sample(returning,8,rng);
  for(const auto& [bits,n]:state){EXPECT_TRUE(n>0);EXPECT_EQ(bits.back(),'0');}
}

TEST(Photon_controller,discard_pool_reuse_resets_before_new_owner_and_uint_complement) {
  auto module=lower("QReg q(1)\nq.x(0)\ndiscard q\nQReg q(1)\nBit zero = q.measure(0)\n"
    "UInt<8> value = 1\nvalue = ~value\noutput zero\noutput value\n");
  EXPECT_EQ(sd::flatten(module).numQubits,std::size_t(1));
  expectOutputs(module,{{"zero",0},{"value",254}});
  EXPECT_TRUE(rejected("QReg q(2)\ndiscard q[0]\nq.x(0)\n"));
  EXPECT_TRUE(rejected("QReg q(1)\nBit c = q.measure(0)\nif(c) {\ndiscard q\n}\n"));
}

TEST(Photon_controller,invalid_runtime_programs_fail) {
  for(const auto* body:{
    "QReg q(1)\nUInt<8> x = 256\n",
    "QReg q(1)\nUInt<8> x = 1\nUInt<4> y = x\n",
    "QReg q(1)\nUInt<8> x = 1\nx = x * 2\n",
    "QReg q(1)\nUInt<8> x = 1\nx = x << 8\n",
    "QReg q(1)\nBit x = q.measure(0)\nq.x(x)\n",
    "QReg q(1)\nUInt<8> x = 1\nwhile (x < 3) {\nx = x + 1\n}\n",
    "QReg q(1)\nBit bits[1]\nBit saved = bits[0]\n",
    "QReg q(1)\nBit c = q.measure(0)\nif(c) {\nUInt<8> local = 1\n}\noutput local\n"
  })EXPECT_TRUE(rejected(body));
}

TEST(Photon_controller,operation_budget_covers_allocations_and_straight_line_programs) {
  {BudgetSetting setting("8");
    EXPECT_TRUE(rejected("QReg q(5)\n"));
    EXPECT_TRUE(rejected("Bit bits[9]\n"));
    EXPECT_TRUE(rejected("QReg q(1)\nq.x(0)\nq.x(0)\nq.x(0)\nq.x(0)\nq.x(0)\nq.x(0)\nq.x(0)\n"));
    EXPECT_FALSE(rejected("QReg q(1)\nq.x(0)\n"));
  }
  for(const auto* invalid:{"0","-1","+8","8junk"}){
    BudgetSetting setting(invalid);EXPECT_TRUE(rejected("QReg q(1)\n"));
  }
}

SPINOR_TEST_MAIN()

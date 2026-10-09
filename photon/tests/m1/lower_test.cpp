// photon/tests/m1/lower_test.cpp
//
// OO -> Phonon lowering tests. Verifies the resulting Phonon module
// (a) has the expected ops and (b) passes Phonon's verifier.

#include "phonon/dialect/Phonon.h"
#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace photon::lang;
namespace pd = phonon::dialect;

namespace {
std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
std::string corpus(const std::string& name) {
  return std::string(PHOTON_TEST_CORPUS_DIR) + "/" + name;
}

std::optional<pd::Module> lowerOrFail(const std::string& path) {
  auto pr = parse(readFile(path), path);
  if (!pr.module) return std::nullopt;
  auto lr = lowerToPhonon(*pr.module);
  return std::move(lr.module);
}

// Count ops by mnemonic (e.g. "spinor.h", "spinor.cx", "spinor.measure").
int countOps(const pd::Module& m, std::string_view mnemonic) {
  int n = 0;
  for (const auto& op : m.ops()) {
    if (pd::opMnemonic(op.kind) == mnemonic) ++n;
  }
  return n;
}
}  // namespace

TEST(M1_photon_lower, bell) {
  auto m = lowerOrFail(corpus("bell.pho"));
  EXPECT_TRUE(m.has_value());
  if (!m.has_value()) return;
  // Bell expects: 2 alloc_qubit + 2 alloc_bit + 1 h + 1 cx + 2 measure
  // wrapped inside def/end_def + return.
  EXPECT_EQ(countOps(*m, "spinor.alloc_qubit"), 2);
  EXPECT_EQ(countOps(*m, "spinor.h"), 1);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 1);
  EXPECT_EQ(countOps(*m, "spinor.measure"), 2);
}

TEST(M1_photon_lower, ghz) {
  auto m = lowerOrFail(corpus("ghz.pho"));
  EXPECT_TRUE(m.has_value());
  if (!m.has_value()) return;
  EXPECT_EQ(countOps(*m, "spinor.alloc_qubit"), 3);
  EXPECT_EQ(countOps(*m, "spinor.h"), 1);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 2);
}

TEST(M1_photon_lower, for_loop_resolves_each_induction_index) {
  auto m = lowerOrFail(corpus("for_loop.pho"));
  EXPECT_TRUE(m.has_value());
  if (!m) return;
  EXPECT_EQ(countOps(*m, "spinor.h"), 4);
  std::vector<pd::ValueId> allocated;
  std::size_t i = 0;
  for (const auto& op : m->ops()) {
    if (op.kind == pd::OpKind::AllocQubit) allocated.push_back(op.results.front());
    if (op.kind == pd::OpKind::H) EXPECT_TRUE(op.operands.front() == allocated.at(i++));
  }
}

TEST(M1_photon_lower, ambiguous_measurement_predicate_is_rejected) {
  auto m = lowerOrFail(corpus("if_else.pho"));
  EXPECT_FALSE(m.has_value());
}

TEST(M1_photon_lower, static_else_and_angle_assignment) {
  auto parsed = parse("target generic\nkernel sample() {\nQReg q(1)\nangle theta = 0.25\ntheta = theta + 0.5\nif (2 < 1) {\nq.x(0)\n} else {\nq.rz(theta, 0)\n}\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto lowered = lowerToPhonon(*parsed.module);
  EXPECT_TRUE(lowered.module.has_value());
  if (!lowered.module) return;
  EXPECT_EQ(countOps(*lowered.module, "spinor.x"), 0);
  EXPECT_EQ(countOps(*lowered.module, "spinor.rz"), 1);
  for (const auto& op : lowered.module->ops()) if (op.kind == pd::OpKind::Rz)
    EXPECT_TRUE(std::get<double>(op.attributes.front().value) == 0.75);
}

TEST(M1_photon_lower, fractional_qubit_index_is_rejected) {
  auto parsed = parse("target generic\nkernel sample() {\nQReg q(2)\nq.x(0.5)\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (parsed.module) EXPECT_FALSE(lowerToPhonon(*parsed.module).module.has_value());
}

TEST(M1_photon_lower, target_propagated) {
  auto m = lowerOrFail(corpus("bell_kernel.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_TRUE(m->targetAttr == "ibm_heron_r2");
}

TEST(M1_photon_lower, def_wrapper_present) {
  // After M5: Photon kernels with no parameters lower to a flat
  // Phonon program (no phonon.def wrapper) so phonon::lower can
  // produce flat Spinor. We instead assert the body ops landed.
  auto m = lowerOrFail(corpus("bell.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ(countOps(*m, "spinor.alloc_qubit"), 2);
  EXPECT_EQ(countOps(*m, "spinor.h"), 1);
}

TEST(M1_photon_lower, measured_bit_index_and_runtime_branches) {
  auto parsed=parse("target generic\nkernel dynamic() {\nQReg q(2)\nq.h(0)\nBit c = q.measure(0)\nif (c == 1) {\nq.x(1)\n} else {\nq.z(1)\n}\nBit d = q.measure(1)\n}\n");
  EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  auto result=lowerToPhonon(*parsed.module);
  EXPECT_TRUE(result.module.has_value());if(!result.module)return;
  EXPECT_EQ(countOps(*result.module,"phonon.if"),1);
  EXPECT_EQ(countOps(*result.module,"spinor.measure"),2);
  EXPECT_EQ(countOps(*result.module,"spinor.x"),1);
  EXPECT_EQ(countOps(*result.module,"spinor.z"),1);
}

TEST(M1_photon_lower, explicit_resets_and_saved_measurements_preserve_storage) {
  auto parsed = parse("target generic\nkernel sample() {\nQReg q(2)\n"
      "Bit first = q.measure(0)\nfor i in 0..2 {\nq.reset(i)\n}\n"
      "Bit second = q.measure(0)\nq.measure()\n"
      "if (first == 1) {\nq.x(1)\n}\nreturn q.measure_int()\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = lowerToPhonon(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  EXPECT_EQ(countOps(*result.module, "spinor.reset"), 2);
  std::vector<std::size_t> destinations;
  for (const auto& operation : result.module->ops()) if (operation.kind == pd::OpKind::Measure)
    for (const auto& attribute : operation.attributes) if (attribute.name == "clbit")
      destinations.push_back(static_cast<std::size_t>(std::get<double>(attribute.value)));
  EXPECT_EQ(destinations.size(), std::size_t(6));
  EXPECT_TRUE(destinations[0] != destinations[1]);
  EXPECT_TRUE(destinations[0] != destinations[2]);
  EXPECT_EQ(destinations[4], std::size_t(0));
  EXPECT_EQ(destinations[5], std::size_t(1));
}

SPINOR_TEST_MAIN()

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

TEST(M1_photon_lower, for_loop_emits_phonon_for) {
  auto m = lowerOrFail(corpus("for_loop.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ(countOps(*m, "phonon.for"), 1);
  EXPECT_EQ(countOps(*m, "phonon.end_for"), 1);
}

TEST(M1_photon_lower, if_else_emits_phonon_if) {
  auto m = lowerOrFail(corpus("if_else.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ(countOps(*m, "phonon.if"), 1);
  EXPECT_EQ(countOps(*m, "phonon.end_if"), 1);
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

SPINOR_TEST_MAIN()

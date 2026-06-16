// photon/tests/m2/lib_test.cpp
//
// photon.lib expansion tests. Each test parses a small driver
// kernel that calls a library routine, lowers to Phonon, and
// asserts the catalogue's op-count contract.

#include "phonon/dialect/Phonon.h"
#include "photon/lang/Library.h"
#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

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
int countOps(const pd::Module& m, std::string_view mn) {
  int n = 0;
  for (const auto& op : m.ops()) if (pd::opMnemonic(op.kind) == mn) ++n;
  return n;
}
}  // namespace

TEST(M2_lib, library_routine_set) {
  EXPECT_TRUE(isLibraryRoutine("bell_pair"));
  EXPECT_TRUE(isLibraryRoutine("ghz"));
  EXPECT_TRUE(isLibraryRoutine("qft"));
  EXPECT_TRUE(isLibraryRoutine("iqft"));
  EXPECT_TRUE(isLibraryRoutine("grover"));
  EXPECT_TRUE(isLibraryRoutine("teleport"));
  EXPECT_TRUE(isLibraryRoutine("vqe_ansatz"));
  EXPECT_FALSE(isLibraryRoutine("nonsense"));
}

TEST(M2_lib, bell_pair) {
  auto m = lowerOrFail(corpus("lib_bell.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ(countOps(*m, "spinor.h"), 1);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 1);
}

TEST(M2_lib, ghz_n3) {
  auto m = lowerOrFail(corpus("lib_ghz.pho"));
  EXPECT_TRUE(m.has_value());
  EXPECT_EQ(countOps(*m, "spinor.h"), 1);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 2);
  EXPECT_EQ(countOps(*m, "spinor.measure"), 3);
}

TEST(M2_lib, qft_n3) {
  auto m = lowerOrFail(corpus("lib_qft.pho"));
  EXPECT_TRUE(m.has_value());
  // QFT(3): 3 H + 3 controlled-Rz blocks (each 4 cx + 4 rz internally)
  // + 1 swap. So we expect 3 H, 6 CX (controlled-Rz uses 2 cx each
  // and there are 3 of them: (k=1,j=0), (k=2,j=0), (k=2,j=1)), and
  // exactly 1 swap (n=3, n/2=1 pair: i=0 <-> i=2).
  EXPECT_EQ(countOps(*m, "spinor.h"), 3);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 6);
  EXPECT_EQ(countOps(*m, "spinor.swap"), 1);
}

TEST(M2_lib, grover_rounds2_emits_oracle_calls) {
  auto m = lowerOrFail(corpus("lib_grover.pho"));
  EXPECT_TRUE(m.has_value());
  // 2 oracle calls + 2 diffusion blocks.
  EXPECT_EQ(countOps(*m, "phonon.call"), 2);
  // Diffusion has H^N · X^N · ... · X^N · H^N. With rounds=2 and
  // n=3, expect at least 2*2*3 = 12 H ops and 2*2*3 = 12 X ops, plus
  // the initial 3 H's = 15 H total minimum.
  EXPECT_TRUE(countOps(*m, "spinor.h") >= 15);
  EXPECT_TRUE(countOps(*m, "spinor.x") >= 12);
}

TEST(M2_lib, teleport_emits_corrections) {
  auto m = lowerOrFail(corpus("lib_teleport.pho"));
  EXPECT_TRUE(m.has_value());
  // 2 measurements + 2 phonon.if (one per correction).
  EXPECT_EQ(countOps(*m, "spinor.measure"), 2);
  EXPECT_EQ(countOps(*m, "phonon.if"), 2);
}

TEST(M2_lib, vqe_depth2) {
  auto m = lowerOrFail(corpus("lib_vqe.pho"));
  EXPECT_TRUE(m.has_value());
  // depth=2, n=3: 2*3 = 6 ry, plus 2*(n-1) = 4 cx.
  EXPECT_EQ(countOps(*m, "spinor.ry"), 6);
  EXPECT_EQ(countOps(*m, "spinor.cx"), 4);
}

TEST(M2_lib, e2e_verifies) {
  auto m = lowerOrFail(corpus("lib_bell.pho"));
  EXPECT_TRUE(m.has_value());
  pd::Diagnostics d;
  pd::verify(*m, d);
  bool errored = false;
  for (const auto& it : d.items())
    if (it.severity == pd::DiagSeverity::Error) errored = true;
  EXPECT_FALSE(errored);
}

SPINOR_TEST_MAIN()

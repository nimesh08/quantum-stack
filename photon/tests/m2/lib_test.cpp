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

TEST(M2_lib, grover_rejects_unbound_oracle) {
  // This fixture declares an oracle symbol without supplying its definition.
  // It must not compile to a placeholder call or an identity operation.
  auto m = lowerOrFail(corpus("lib_grover.pho"));
  EXPECT_TRUE(!m.has_value());
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

TEST(M2_lib, invalid_arity_indices_and_depth_never_compile) {
  for (const auto* call : {
      "bell_pair(1)", "bell_pair(0, 1, 2)", "bell_pair(0, 0)",
      "bell_pair(-1, 1)", "bell_pair(0, 3)", "bell_pair(0.5, 1)",
      "ghz(1)", "qft(1)", "iqft(1)",
      "teleport(1)", "teleport(0, 1)", "teleport(0, 1, 2, 3)",
      "teleport(0, 1, 1)", "teleport(0, 1, 3)", "teleport(0, -1, 2)",
      "vqe_ansatz(1, 2)", "vqe_ansatz(-1)", "vqe_ansatz(0)",
      "vqe_ansatz(0.5)", "vqe_ansatz(100001)", "grover()", "grover(1)"}) {
    const auto source = std::string("target generic\nkernel invalid() -> int {\nQReg q(3)\nq.") +
        call + "\nreturn q.measure_int()\n}\n";
    auto parsed = parse(source);
    EXPECT_TRUE(parsed.module.has_value());
    if (!parsed.module) continue;
    auto lowered = lowerToPhonon(*parsed.module);
    EXPECT_FALSE(lowered.module.has_value());
    EXPECT_TRUE(lowered.diag.hasErrors());
  }
}

TEST(M2_lib, documented_defaults_and_integer_expressions_remain_valid) {
  for (const auto* call : {"bell_pair()", "bell_pair(0, 1 + 1)", "ghz()",
                         "qft()", "iqft()", "teleport()", "teleport(2, 0, 1)",
                         "vqe_ansatz()", "vqe_ansatz(1 + 1)"}) {
    const auto source = std::string("target generic\nkernel valid() -> void {\nQReg q(3)\nq.") + call + "\n}\n";
    auto parsed = parse(source);
    EXPECT_TRUE(parsed.module.has_value());
    if (!parsed.module) continue;
    auto lowered = lowerToPhonon(*parsed.module);
    EXPECT_TRUE(lowered.module.has_value());
    EXPECT_FALSE(lowered.diag.hasErrors());
  }
}

SPINOR_TEST_MAIN()

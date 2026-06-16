// photon/tests/m5/ingest_test.cpp

#include "phonon/dialect/Phonon.h"
#include "photon/cppfront/Ingest.h"
#include "photon/lang/Lower.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

using namespace photon::cppfront;
namespace pl = photon::lang;
namespace pd = phonon::dialect;

namespace {
std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
std::string corpus(const std::string& name) {
  return std::string(PHOTON_TEST_CORPUS_DIR) + "/cpp/" + name;
}

int countOps(const pd::Module& m, std::string_view mn) {
  int n = 0;
  for (const auto& op : m.ops()) if (pd::opMnemonic(op.kind) == mn) ++n;
  return n;
}
}  // namespace

TEST(M5_ingest, bell) {
  auto src = readFile(corpus("bell.cpp"));
  auto r = ingestCpp(src, "bell_kernel");
  EXPECT_TRUE(r.module.has_value());
  if (!r.module) return;
  EXPECT_EQ(static_cast<int>(r.module->functions.size()), 1);
  EXPECT_TRUE(r.module->functions[0].name == "bell_kernel");
  // Lower and confirm the Phonon module shape.
  auto lr = pl::lowerToPhonon(*r.module);
  EXPECT_TRUE(lr.module.has_value());
  if (!lr.module) return;
  EXPECT_EQ(countOps(*lr.module, "spinor.alloc_qubit"), 2);
  EXPECT_EQ(countOps(*lr.module, "spinor.h"), 1);
  EXPECT_EQ(countOps(*lr.module, "spinor.cx"), 1);
}

TEST(M5_ingest, ghz) {
  auto src = readFile(corpus("ghz.cpp"));
  auto r = ingestCpp(src, "ghz3");
  EXPECT_TRUE(r.module.has_value());
  auto lr = pl::lowerToPhonon(*r.module);
  EXPECT_TRUE(lr.module.has_value());
  EXPECT_EQ(countOps(*lr.module, "spinor.h"), 1);
  EXPECT_EQ(countOps(*lr.module, "spinor.cx"), 2);
}

TEST(M5_ingest, lib_grover) {
  auto src = readFile(corpus("lib_grover.cpp"));
  auto r = ingestCpp(src, "grover_demo");
  EXPECT_TRUE(r.module.has_value());
  auto lr = pl::lowerToPhonon(*r.module);
  EXPECT_TRUE(lr.module.has_value());
  // grover with rounds=2: 2 oracle calls + diffusion blocks.
  EXPECT_EQ(countOps(*lr.module, "phonon.call"), 2);
}

TEST(M5_ingest, no_kernel_marker) {
  auto src = std::string("int regular_function() { return 0; }");
  auto r = ingestCpp(src, "regular_function");
  // Without the marker, the function name still matches `entry` so
  // it is promoted to a kernel; but absent body of supported
  // constructs we still produce a valid (empty) module.
  EXPECT_TRUE(r.module.has_value());
}

TEST(M5_ingest, unknown_construct) {
  auto src = std::string(
      "[[photon::kernel]]\n"
      "int bad() {\n"
      "  asm(\"nop\");\n"
      "  return 0;\n"
      "}\n");
  auto r = ingestCpp(src, "bad");
  // The asm token is unknown; the parser should diagnose it.
  bool sawErr = false;
  for (const auto& d : r.diag.items())
    if (d.severity == pl::DiagSeverity::Error) sawErr = true;
  EXPECT_TRUE(sawErr);
}

SPINOR_TEST_MAIN()

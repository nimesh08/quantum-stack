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

TEST(M5_ingest, unresolved_grover_is_rejected) {
  auto src = readFile(corpus("lib_grover.cpp"));
  auto r = ingestCpp(src, "grover_demo");
  EXPECT_TRUE(r.module.has_value());
  auto lr = pl::lowerToPhonon(*r.module);
  EXPECT_FALSE(lr.module.has_value());
  bool diagnosed = false;
  for (const auto& d : lr.diag.items())
    if (d.severity == pl::DiagSeverity::Error &&
        d.message.find("grover") != std::string::npos) diagnosed = true;
  EXPECT_TRUE(diagnosed);
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

TEST(M5_ingest, counted_loop_steps_and_comparisons_are_exact) {
  for (const auto& header : {"int i = 3; i >= 0; --i", "int i = 3; i >= 0; i -= 1",
                             "int i = 0; i <= 3; ++i", "int i = 0; i < 4; i = i + 1"}) {
    const std::string source = "[[photon::kernel]] int sample() { QReg q(4); for (" +
        std::string(header) + ") { q.rx(i / 4.0, i); } return 0; }";
    auto parsed = ingestCpp(source, "sample");
    EXPECT_TRUE(parsed.module.has_value());
    if (!parsed.module) continue;
    auto result = pl::lowerToPhonon(*parsed.module);
    EXPECT_TRUE(result.module.has_value());
    if (!result.module) continue;
    EXPECT_EQ(countOps(*result.module, "spinor.rx"), 4);
    std::vector<pd::ValueId> allocated;
    for (const auto& op : result.module->ops()) {
      if (op.kind == pd::OpKind::AllocQubit) allocated.push_back(op.results[0]);
      if (op.kind == pd::OpKind::Rx) {
        const auto index = static_cast<std::size_t>(std::get<double>(op.attributes[0].value) * 4);
        EXPECT_TRUE(op.operands[0] == allocated.at(index));
      }
    }
  }
}

TEST(M5_ingest, absent_or_wrong_loop_increments_are_rejected) {
  for (const auto& increment : {"", "i", "++j", "j++", "i = j + 1"}) {
    auto parsed = ingestCpp("[[photon::kernel]] int sample() { QReg q(4); "
        "for (int i = 0; i < 4; " + std::string(increment) + ") { q.h(i); } }", "sample");
    EXPECT_FALSE(parsed.module.has_value());
  }
}

TEST(M5_ingest, static_relational_condition_selects_one_branch) {
  auto parsed = ingestCpp("[[photon::kernel]] int sample() { QReg q(1); "
      "if (2 >= 1) { q.x(0); } else { q.z(0); } }", "sample");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lowerToPhonon(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  EXPECT_EQ(countOps(*result.module, "spinor.x"), 1);
  EXPECT_EQ(countOps(*result.module, "spinor.z"), 0);
}

TEST(M5_ingest, counted_loop_rechecks_mutated_bound) {
  auto parsed = ingestCpp("[[photon::kernel]] int sample() { QReg q(4); "
      "int n = 2; for (int i = 0; i < n; ++i) { q.x(i); n = 4; } }", "sample");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lowerToPhonon(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (result.module) EXPECT_EQ(countOps(*result.module, "spinor.x"), 4);
}

SPINOR_TEST_MAIN()

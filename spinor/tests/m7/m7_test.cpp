// spinor/tests/m7/m7_test.cpp

#include "spinor/dialect/Spinor.h"
#include "spinor/parser/Parser.h"
#include "test_main.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>

using namespace spinor::dialect;
using namespace spinor::parser;

#ifndef SPINOR_M7_CORPUS_DIR
#define SPINOR_M7_CORPUS_DIR "."
#endif
#ifndef SPINOR_M7_GOLDENS_DIR
#define SPINOR_M7_GOLDENS_DIR "."
#endif

namespace {

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p);
  std::ostringstream s; s << f.rdbuf(); return s.str();
}
std::filesystem::path corpus(const std::string& name) {
  return std::filesystem::path(SPINOR_M7_CORPUS_DIR) / (name + ".spn");
}
std::string joinErrors(const Diagnostics& d) {
  std::string out;
  for (const auto& di : d.items()) {
    out += di.message + " (line " + std::to_string(di.loc.line) +
           ":" + std::to_string(di.loc.column) + ")\n";
  }
  return out;
}
int countOps(const Module& m, OpKind k) {
  int n = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i)
    if (m.op(OpId{i}).kind == k) ++n;
  return n;
}

}  // namespace

// =============================================================
// M7_parse: positive cases
// =============================================================

TEST(M7_parse, bell) {
  auto r = parse(slurp(corpus("bell")));
  if (!r.module) {
    spinor::test::fail("bell.spn parse failed:\n" + joinErrors(r.diag));
  }
  const Module& m = *r.module;
  EXPECT_STREQ(m.targetAttr, "generic");
  EXPECT_EQ(countOps(m, OpKind::AllocQubit), 2);
  EXPECT_EQ(countOps(m, OpKind::AllocBit),   2);
  EXPECT_EQ(countOps(m, OpKind::H),          1);
  EXPECT_EQ(countOps(m, OpKind::Cx),         1);
  EXPECT_EQ(countOps(m, OpKind::Measure),    2);
}

TEST(M7_parse, ghz) {
  auto r = parse(slurp(corpus("ghz")));
  EXPECT_TRUE(r.module.has_value());
  const Module& m = *r.module;
  EXPECT_EQ(countOps(m, OpKind::AllocQubit), 3);
  EXPECT_EQ(countOps(m, OpKind::Cx),         2);
  EXPECT_EQ(countOps(m, OpKind::Measure),    3);
}

TEST(M7_parse, rotations) {
  auto r = parse(slurp(corpus("rotations")));
  EXPECT_TRUE(r.module.has_value());
  const Module& m = *r.module;
  // Walk and grab the angles in order.
  std::vector<double> angles;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::Rx || op.kind == OpKind::Ry ||
        op.kind == OpKind::Rz) {
      for (const auto& a : op.attributes)
        if (a.name == "angle")
          angles.push_back(std::get<double>(a.value));
    }
  }
  EXPECT_EQ(static_cast<int>(angles.size()), 3);
  // pi/2, -pi, 0.5*pi
  EXPECT_TRUE(std::abs(angles[0] - M_PI / 2) < 1e-12);
  EXPECT_TRUE(std::abs(angles[1] + M_PI)     < 1e-12);
  EXPECT_TRUE(std::abs(angles[2] - 0.5 * M_PI) < 1e-12);
}

TEST(M7_parse, native_ibm) {
  auto r = parse(slurp(corpus("native_ibm")));
  EXPECT_TRUE(r.module.has_value());
  const Module& m = *r.module;
  EXPECT_STREQ(m.targetAttr, "ibm_heron_r2");
  EXPECT_TRUE(countOps(m, OpKind::Ecr) > 0);
  EXPECT_TRUE(countOps(m, OpKind::Sx)  > 0);
}

TEST(M7_parse, native_ionq) {
  auto r = parse(slurp(corpus("native_ionq")));
  EXPECT_TRUE(r.module.has_value());
  const Module& m = *r.module;
  EXPECT_STREQ(m.targetAttr, "ionq_forte");
  EXPECT_TRUE(countOps(m, OpKind::Ms)   > 0);
  EXPECT_TRUE(countOps(m, OpKind::Gpi)  > 0);
  EXPECT_TRUE(countOps(m, OpKind::Gpi2) > 0);
}

TEST(M7_parse, mid_circuit) {
  auto r = parse(slurp(corpus("mid_circuit")));
  EXPECT_TRUE(r.module.has_value());
  const Module& m = *r.module;
  EXPECT_EQ(countOps(m, OpKind::Reset),   1);
  EXPECT_EQ(countOps(m, OpKind::Measure), 2);
}

// =============================================================
// M7_parse: negative cases
// =============================================================

TEST(M7_parse, malformed_no_target) {
  auto r = parse(slurp(corpus("malformed_no_target")));
  EXPECT_FALSE(r.module.has_value());
  EXPECT_TRUE(r.diag.hasErrors());
  EXPECT_CONTAINS(joinErrors(r.diag), "target");
}

TEST(M7_parse, malformed_bad_gate) {
  auto r = parse(slurp(corpus("malformed_bad_gate")));
  EXPECT_FALSE(r.module.has_value());
  EXPECT_TRUE(r.diag.hasErrors());
}

TEST(M7_parse, malformed_brackets) {
  auto r = parse(slurp(corpus("malformed_brackets")));
  EXPECT_FALSE(r.module.has_value());
  EXPECT_TRUE(r.diag.hasErrors());
  EXPECT_CONTAINS(joinErrors(r.diag), "]");
}

// =============================================================
// M7_parity: parser parses what Lark+ingest produced, structurally
// =============================================================

TEST(M7_parity, bell_structural) {
  auto r = parse(slurp(corpus("bell")));
  EXPECT_TRUE(r.module.has_value());
  // The C++ parser's per-element naming convention matches the M2
  // ingest tool, so the printed module must equal the M2 round-trip
  // golden byte-for-byte.
  std::string actual = print(*r.module);
  // Read the M2 golden.
  auto goldenPath =
      std::filesystem::path(SPINOR_M7_GOLDENS_DIR) / "bell.spnir";
  std::string expected = slurp(goldenPath);
  EXPECT_STREQ(actual, expected);
}

TEST(M7_parity, ghz_structural) {
  auto r = parse(slurp(corpus("ghz")));
  EXPECT_TRUE(r.module.has_value());
  std::string actual = print(*r.module);
  auto goldenPath =
      std::filesystem::path(SPINOR_M7_GOLDENS_DIR) / "ghz.spnir";
  std::string expected = slurp(goldenPath);
  EXPECT_STREQ(actual, expected);
}

TEST(M7_parity, rotations_structural) {
  auto r = parse(slurp(corpus("rotations")));
  EXPECT_TRUE(r.module.has_value());
  std::string actual = print(*r.module);
  auto goldenPath =
      std::filesystem::path(SPINOR_M7_GOLDENS_DIR) / "rotations.spnir";
  std::string expected = slurp(goldenPath);
  EXPECT_STREQ(actual, expected);
}

SPINOR_TEST_MAIN()

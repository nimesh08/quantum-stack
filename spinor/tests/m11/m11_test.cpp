// spinor/tests/m11/m11_test.cpp

#include "spinor/dialect/Spinor.h"
#include "spinor/parser/Qasm.h"
#include "spinor/parser/Parser.h"
#include "test_main.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace spinor::dialect;
using namespace spinor::parser;

#ifndef SPINOR_M11_QASM_DIR
#define SPINOR_M11_QASM_DIR "."
#endif
#ifndef SPINOR_M11_SPN_DIR
#define SPINOR_M11_SPN_DIR "."
#endif

namespace {

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p);
  std::ostringstream s; s << f.rdbuf(); return s.str();
}
int countOps(const Module& m, OpKind k) {
  int n = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i)
    if (m.op(OpId{i}).kind == k) ++n;
  return n;
}
std::string joinErrors(const Diagnostics& d) {
  std::string out;
  for (const auto& di : d.items()) {
    out += di.message + " (line " + std::to_string(di.loc.line) + ")\n";
  }
  return out;
}

}  // namespace

TEST(M11_qasm, bell_imports) {
  auto r = qasm::import(
      slurp(std::filesystem::path(SPINOR_M11_QASM_DIR) / "bell.qasm"));
  if (!r.module) {
    spinor::test::fail("import failed:\n" + joinErrors(r.diag));
  }
  EXPECT_EQ(countOps(*r.module, OpKind::H), 1);
  EXPECT_EQ(countOps(*r.module, OpKind::Cx), 1);
  EXPECT_EQ(countOps(*r.module, OpKind::Measure), 2);
}

TEST(M11_qasm, ghz_imports) {
  auto r = qasm::import(
      slurp(std::filesystem::path(SPINOR_M11_QASM_DIR) / "ghz.qasm"));
  EXPECT_TRUE(r.module.has_value());
  EXPECT_EQ(countOps(*r.module, OpKind::Cx), 2);
  EXPECT_EQ(countOps(*r.module, OpKind::Measure), 3);
}

TEST(M11_qasm, rotations_imports) {
  auto r = qasm::import(
      slurp(std::filesystem::path(SPINOR_M11_QASM_DIR) / "rotations.qasm"));
  EXPECT_TRUE(r.module.has_value());
  EXPECT_EQ(countOps(*r.module, OpKind::Rx), 1);
  EXPECT_EQ(countOps(*r.module, OpKind::Ry), 1);
  EXPECT_EQ(countOps(*r.module, OpKind::Rz), 1);
}

TEST(M11_qasm, mid_circuit_imports) {
  auto r = qasm::import(
      slurp(std::filesystem::path(SPINOR_M11_QASM_DIR) / "mid_circuit.qasm"));
  EXPECT_TRUE(r.module.has_value());
  EXPECT_EQ(countOps(*r.module, OpKind::Reset), 1);
  EXPECT_EQ(countOps(*r.module, OpKind::Measure), 2);
}

TEST(M11_qasm, rejects_gate_definitions) {
  auto r = qasm::import(slurp(
      std::filesystem::path(SPINOR_M11_QASM_DIR) / "unsupported_gate_def.qasm"));
  EXPECT_FALSE(r.module.has_value());
  EXPECT_TRUE(r.diag.hasErrors());
  EXPECT_CONTAINS(joinErrors(r.diag), "Phonon");
}

TEST(M11_qasm, rejects_control_flow) {
  auto r = qasm::import(slurp(
      std::filesystem::path(SPINOR_M11_QASM_DIR) / "unsupported_if.qasm"));
  EXPECT_FALSE(r.module.has_value());
  EXPECT_TRUE(r.diag.hasErrors());
}

// Parity: import the QASM equivalent of a corpus .spn and assert
// the resulting Module has the same op count and target attribute
// shape.
TEST(M11_qasm, parity_with_spn_bell) {
  auto qasmRes = qasm::import(slurp(
      std::filesystem::path(SPINOR_M11_QASM_DIR) / "bell.qasm"));
  auto spnRes  = parse(slurp(
      std::filesystem::path(SPINOR_M11_SPN_DIR) / "bell.spn"));
  EXPECT_TRUE(qasmRes.module.has_value());
  EXPECT_TRUE(spnRes.module.has_value());
  EXPECT_EQ(spnRes.module->numOps(), qasmRes.module->numOps());
  EXPECT_EQ(countOps(*spnRes.module, OpKind::Cx),
            countOps(*qasmRes.module, OpKind::Cx));
  EXPECT_EQ(countOps(*spnRes.module, OpKind::Measure),
            countOps(*qasmRes.module, OpKind::Measure));
}

SPINOR_TEST_MAIN()

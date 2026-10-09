// phonon/tests/m4/lower_test.cpp
//
// Phonon → Spinor lowering: structural and pipeline tests.

#include "phonon/lower/Lowering.h"
#include "phonon/parser/Parser.h"
#include "spinor/dialect/Spinor.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;
namespace pl = phonon::lower;
namespace sd = spinor::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

std::size_t countOpKind(const sd::Module& m, sd::OpKind k) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (m.op(sd::OpId{i}).kind == k) ++n;
  }
  return n;
}

}  // namespace

TEST(M4_lower, bell_passthrough) {
  // Parse bell.spn through the Phonon parser, then lower.
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/bell.spn");
  auto pr = pp::parse(src, "bell.spn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  if (lr.diag.hasErrors()) {
    for (const auto& it : lr.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_TRUE(lr.module.has_value());
  // Bell: 2 alloc_qubit, 2 alloc_bit, 1 h, 1 cx, 2 measure.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocQubit), static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocBit),   static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H),          static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Cx),         static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Measure),    static_cast<std::size_t>(2));
}

TEST(M4_lower, qft_loop_unrolls) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn");
  auto pr = pp::parse(src, "qft_loop.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  // Each iteration must address its own physical logical slot.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H), static_cast<std::size_t>(4));
  const auto source = pl::emitSpinorSource(*lr.module);
  for (int i = 0; i < 4; ++i)
    EXPECT_TRUE(source.find("h q[" + std::to_string(i) + "]") != std::string::npos);
}

TEST(M4_lower, bell_pair_func_inlines) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/bell_pair_func.phn");
  auto pr = pp::parse(src, "bell_pair_func.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  if (lr.diag.hasErrors()) {
    for (const auto& it : lr.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_TRUE(lr.module.has_value());
  // Inlined twice: 2 H ops, 2 CX ops; 4 alloc_qubit, 4 alloc_bit; 4 measure.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocQubit), static_cast<std::size_t>(4));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocBit),   static_cast<std::size_t>(4));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H),          static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Cx),         static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Measure),    static_cast<std::size_t>(4));
}

TEST(M4_lower, dynamic_branches_preserve_classical_conditions) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/teleportation.phn");
  auto pr = pp::parse(src, "teleportation.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  EXPECT_FALSE(lr.diag.hasErrors());
  if(!lr.module)return;
  EXPECT_EQ(countOpKind(*lr.module,sd::OpKind::If),std::size_t(2));
  EXPECT_EQ(countOpKind(*lr.module,sd::OpKind::EndIf),std::size_t(2));
  auto source=pl::emitSpinorSource(*lr.module);
  EXPECT_CONTAINS(source,"if c[1] == 1 {");
  EXPECT_CONTAINS(source,"if c[0] == 1 {");
}

TEST(M4_lower, nested_loops_angles_and_readout_permutation) {
  auto parsed = pp::parse("target generic\nqubit q[4]\nbit c[5]\nfor i in 0..2 {\nfor j in 0..2 {\nrx((i * 2 + j) / 4) q[i * 2 + j]\n}\n}\nc[4] = measure q[0]\nc[1] = measure q[3]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_TRUE(source.find("rx(0.25) q[1]") != std::string::npos);
  EXPECT_TRUE(source.find("rx(0.75) q[3]") != std::string::npos);
  EXPECT_TRUE(source.find("bit c[5]") != std::string::npos);
  EXPECT_TRUE(source.find("c[4] = measure q[0]") != std::string::npos);
  EXPECT_TRUE(source.find("c[1] = measure q[3]") != std::string::npos);
}

TEST(M4_lower, static_else_retains_following_qubit_value) {
  auto parsed = pp::parse("target generic\nqubit q[1]\nif (1 == 0) {\nx q[0]\n} else {\nz q[0]\n}\nh q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  auto source = pl::emitSpinorSource(*result.module);
  EXPECT_TRUE(source.find("x q[0]") == std::string::npos);
  EXPECT_TRUE(source.find("z q[0]\nh q[0]") != std::string::npos);
}

TEST(M4_lower, invalid_indices_sizes_and_parameters_fail) {
  for (const auto* body : {"qubit q[-1]\n", "qubit q[2]\nx q[0.5]\n",
                           "qubit q[1]\nrx(theta) q[0]\n", "qubit q[1]\nrx q[0]\n",
                           "qubit q[1]\nx q[9]\n", "}\n",
                           "qubit q[999999999999999999999999999999999999]\n"}) {
    auto parsed = pp::parse(std::string("target generic\n") + body);
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, native_rxx_parameter_survives_frontend) {
  auto parsed = pp::parse("target generic\nqubit q[2]\nrxx(0.125) q[0], q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (result.module) EXPECT_TRUE(pl::emitSpinorSource(*result.module).find(
      "rxx(0.125) q[0], q[1]") != std::string::npos);
}

SPINOR_TEST_MAIN()

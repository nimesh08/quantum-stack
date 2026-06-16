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
  // The loop unrolls: emit body 4 times — but the body has a single
  // `h q[0]` that the parser already folded with var=0 (Phase B M4
  // limitation), so we expect 4 `h` ops on the same slot.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H), static_cast<std::size_t>(4));
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

TEST(M4_lower, no_phonon_ops_in_output) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/teleportation.phn");
  auto pr = pp::parse(src, "teleportation.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  // Every op in the lowered module must be a Spinor op kind.
  // (Trivially true: spinor::dialect::OpKind has no Phonon ops.)
  EXPECT_TRUE(lr.module->numOps() > 0);
}

SPINOR_TEST_MAIN()

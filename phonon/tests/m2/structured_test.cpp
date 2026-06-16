// phonon/tests/m2/structured_test.cpp
//
// Structured-program corpus: parse Phonon-only fixtures and assert
// the produced IR contains the expected Phonon ops.

#include "phonon/parser/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;
namespace pd = phonon::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

std::size_t countOpKind(const pd::Module& m, pd::OpKind k) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (m.op(pd::OpId{i}).kind == k) ++n;
  }
  return n;
}

}  // namespace

TEST(M2_struct, bell_pair_func) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/bell_pair_func.phn");
  auto r = pp::parse(src, "bell_pair_func.phn");
  if (r.diag.hasErrors()) {
    for (const auto& it : r.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_FALSE(r.diag.hasErrors());
  EXPECT_TRUE(r.module.has_value());
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::Def), static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::Call), static_cast<std::size_t>(2));
}

TEST(M2_struct, qft_loop) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn");
  auto r = pp::parse(src, "qft_loop.phn");
  if (r.diag.hasErrors()) {
    for (const auto& it : r.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_FALSE(r.diag.hasErrors());
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::For), static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::EndFor), static_cast<std::size_t>(1));
}

TEST(M2_struct, teleportation) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/teleportation.phn");
  auto r = pp::parse(src, "teleportation.phn");
  if (r.diag.hasErrors()) {
    for (const auto& it : r.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_FALSE(r.diag.hasErrors());
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::If), static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*r.module, pd::OpKind::Cmp), static_cast<std::size_t>(2));
}

SPINOR_TEST_MAIN()

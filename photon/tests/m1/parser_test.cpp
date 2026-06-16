// photon/tests/m1/parser_test.cpp
//
// Parser tests: corpus shape, error messages.

#include "photon/lang/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

using namespace photon::lang;

namespace {
std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
std::string corpus(const std::string& name) {
  return std::string(PHOTON_TEST_CORPUS_DIR) + "/" + name;
}
}  // namespace

TEST(M1_photon_parser, bell) {
  auto pr = parse(readFile(corpus("bell.pho")), "bell.pho");
  EXPECT_TRUE(pr.module.has_value());
  if (!pr.module.has_value()) return;
  EXPECT_TRUE(pr.module->target == "generic");
  EXPECT_EQ(static_cast<int>(pr.module->functions.size()), 1);
  const auto& f = pr.module->functions[0];
  EXPECT_TRUE(f.name == "bell");
  EXPECT_TRUE(f.is_kernel);
  // Body: VarDecl QReg + h + cx + return.
  EXPECT_TRUE(f.body.size() >= 4u);
}

TEST(M1_photon_parser, ghz) {
  auto pr = parse(readFile(corpus("ghz.pho")), "ghz.pho");
  EXPECT_TRUE(pr.module.has_value());
  if (!pr.module.has_value()) return;
  EXPECT_TRUE(pr.module->functions[0].name == "ghz");
}

TEST(M1_photon_parser, target_carries) {
  auto pr = parse(readFile(corpus("bell_kernel.pho")), "bell_kernel.pho");
  EXPECT_TRUE(pr.module.has_value());
  EXPECT_TRUE(pr.module->target == "ibm_heron_r2");
}

TEST(M1_photon_parser, for_loop) {
  auto pr = parse(readFile(corpus("for_loop.pho")), "for_loop.pho");
  EXPECT_TRUE(pr.module.has_value());
}

TEST(M1_photon_parser, if_else) {
  auto pr = parse(readFile(corpus("if_else.pho")), "if_else.pho");
  EXPECT_TRUE(pr.module.has_value());
}

TEST(M1_photon_parser, error_unknown_top_level) {
  auto pr = parse("garbage_at_top_level\n", "<inline>");
  EXPECT_FALSE(pr.module.has_value());
  bool sawErr = false;
  for (const auto& d : pr.diag.items()) {
    if (d.severity == photon::lang::DiagSeverity::Error) sawErr = true;
  }
  EXPECT_TRUE(sawErr);
}

TEST(M1_photon_parser, error_missing_paren) {
  auto pr = parse("kernel f() { QReg q(2 }\n", "<inline>");
  EXPECT_FALSE(pr.module.has_value());
}

TEST(M1_photon_parser, comments_all_kinds) {
  std::string src =
      "# hash comment\n"
      "; semicolon comment\n"
      "// slash slash comment\n"
      "/* block\n   comment */\n"
      "kernel ok() {}\n";
  auto pr = parse(src, "<inline>");
  EXPECT_TRUE(pr.module.has_value());
  EXPECT_EQ(static_cast<int>(pr.module->functions.size()), 1);
}

SPINOR_TEST_MAIN()

// phonon/tests/m2/error_test.cpp
//
// Diagnostic tests: malformed Phonon programs produce errors with
// line/column information.

#include "phonon/parser/Parser.h"
#include "test_main.h"

namespace pp = phonon::parser;

TEST(M2_error, unbalanced_brace) {
  std::string src =
      "target generic\n"
      "qubit q[2]\n"
      "if (q[0] == 1) {\n"
      "  h q[0]\n"
      "; missing closing brace\n";
  auto r = pp::parse(src, "<test>");
  EXPECT_TRUE(r.diag.hasErrors());
}

TEST(M2_error, unknown_func_handled) {
  // Calls to unknown functions are emitted as phonon.call; the parser
  // does not fail. (Resolution happens in M4 lowering / type checker.)
  std::string src =
      "target generic\n"
      "qubit q[2]\n"
      "no_such_function(q[0], q[1])\n";
  auto r = pp::parse(src, "<test>");
  EXPECT_FALSE(r.diag.hasErrors());
  EXPECT_TRUE(r.module.has_value());
}

TEST(M2_error, missing_target_is_error) {
  std::string src = "qubit q[1]\n";
  auto r = pp::parse(src, "<test>");
  EXPECT_TRUE(r.diag.hasErrors());
}

SPINOR_TEST_MAIN()

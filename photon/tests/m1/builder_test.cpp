// photon/tests/m1/builder_test.cpp
//
// Builder/AST construction tests for the Photon language.

#include "photon/lang/Module.h"
#include "test_main.h"

using namespace photon::lang;

TEST(M1_photon_builder, ast_helpers) {
  auto e = mkBinOp("+", mkInt(1), mkInt(2));
  EXPECT_EQ(static_cast<int>(e->kind), static_cast<int>(ExprKind::BinOp));
  EXPECT_EQ(static_cast<int>(e->children.size()), 2);
  EXPECT_TRUE(e->text == "+");
}

TEST(M1_photon_builder, types) {
  EXPECT_TRUE(intType().kind == TypeKind::Int);
  EXPECT_TRUE(qregType(4).kind == TypeKind::QReg);
  EXPECT_EQ(static_cast<int>(qregType(4).qreg_size), 4);
}

TEST(M1_photon_builder, module_lookup) {
  Module m;
  Function f;
  f.name = "main";
  f.is_kernel = true;
  m.functions.push_back(std::move(f));
  EXPECT_TRUE(m.findFunction("main") != nullptr);
  EXPECT_TRUE(m.findFunction("missing") == nullptr);
}

SPINOR_TEST_MAIN()

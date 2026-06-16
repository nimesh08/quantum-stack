// phonon/tests/m1/builder_test.cpp
//
// Tests for the Phonon Builder API: classical types, control-flow
// markers, function definition.

#include "phonon/dialect/Phonon.h"
#include "test_main.h"

#include <vector>

using namespace phonon::dialect;

TEST(M1_builder, bell_in_phonon) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  (void)b.allocBit();
  (void)b.allocBit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);

  EXPECT_EQ(m.numOps(), static_cast<std::size_t>(8));
  EXPECT_STREQ(std::string(opMnemonic(m.op(OpId{0}).kind)), std::string("spinor.alloc_qubit"));
  EXPECT_STREQ(std::string(opMnemonic(m.op(OpId{4}).kind)), std::string("spinor.h"));
  EXPECT_STREQ(std::string(opMnemonic(m.op(OpId{5}).kind)), std::string("spinor.cx"));
  EXPECT_EQ(static_cast<int>(m.typeOf(q0).kind), static_cast<int>(TypeKind::Qubit));
}

TEST(M1_builder, const_and_binop) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto a = b.constInt(2);
  auto bb = b.constInt(3);
  auto sum = b.binOp("+", a, bb);
  EXPECT_EQ(static_cast<int>(m.typeOf(a).kind), static_cast<int>(TypeKind::Int));
  EXPECT_EQ(static_cast<int>(m.typeOf(sum).kind), static_cast<int>(TypeKind::Int));

  auto ang = b.constAngle(3.14);
  EXPECT_EQ(static_cast<int>(m.typeOf(ang).kind), static_cast<int>(TypeKind::Angle));
}

TEST(M1_builder, def_call_return) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  Builder::Param params[2] = {
    {qubitType(), "a"},
    {qubitType(), "bb"},
  };
  OpId defId = b.beginDef("bell_pair", std::span<const Builder::Param>(params, 2));
  ValueId pa = b.paramValue(defId, 0);
  ValueId pb = b.paramValue(defId, 1);
  ValueId pa1 = b.h(pa);
  auto [pa2, pb1] = b.cx(pa1, pb);
  ValueId outs[2] = {pa2, pb1};
  b.returnOp(std::span<const ValueId>(outs, 2));
  b.endDef(defId);

  // Now call it.
  auto qx = b.allocQubit();
  auto qy = b.allocQubit();
  ValueId args[2] = {qx, qy};
  Type rts[2] = {qubitType(), qubitType()};
  auto results = b.call("bell_pair",
                        std::span<const ValueId>(args, 2),
                        std::span<const Type>(rts, 2));
  EXPECT_EQ(results.size(), static_cast<std::size_t>(2));

  // Verify markers paired (structural verifier).
  Diagnostics d;
  verify(m, d);
  EXPECT_FALSE(d.hasErrors());
}

TEST(M1_builder, teleportation_skeleton) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q2 = b.allocQubit();
  // Prepare entangled pair on q1, q2
  auto q1a = b.h(q1);
  auto [q1b, q2a] = b.cx(q1a, q2);
  // Bell measurement on q0, q1
  auto [q0a, q1c] = b.cx(q0, q1b);
  auto q0b_ = b.h(q0a);
  auto m0 = b.measure(q0b_);
  auto m1 = b.measure(q1c);

  // if (m1 == 1) { x q2 }
  auto one = b.constInt(1);
  // Cmp on bits is allowed (bit treated as 0/1 int for comparison).
  auto pred1 = b.cmp("==", m1, one);
  OpId if1 = b.beginIf(pred1);
  auto q2b = b.x(q2a);
  b.endIf(if1);

  // if (m0 == 1) { z q2 }
  auto pred0 = b.cmp("==", m0, one);
  OpId if2 = b.beginIf(pred0);
  auto q2c = b.z(q2b);
  (void)q2c;
  b.endIf(if2);

  Diagnostics d;
  verify(m, d);
  EXPECT_FALSE(d.hasErrors());
}

SPINOR_TEST_MAIN()

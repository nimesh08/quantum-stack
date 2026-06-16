// spinor/tests/m1/verify_test.cpp
//
// M1-T11..T16 — structural verifier accept/reject cases.

#include "spinor/dialect/Spinor.h"
#include "test_main.h"

using namespace spinor::dialect;

TEST(M1_verify, accept_well_formed_bell) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);
  Diagnostics d;
  verify(m, d);
  EXPECT_FALSE(d.hasErrors());
}

TEST(M1_verify, accept_well_formed_rotations) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  auto a = b.rx(0.5, q);
  auto bb = b.ry(0.5, a);
  (void)b.rz(0.5, bb);
  Diagnostics d;
  verify(m, d);
  EXPECT_FALSE(d.hasErrors());
}

TEST(M1_verify, reject_undefined_operand) {
  Module m;
  m.targetAttr = "generic";
  // Hand-build an op that references an undefined value 99.
  Op op;
  op.kind = OpKind::H;
  op.operands = {ValueId{99}};
  // Allocate a fake result so the result-count check passes.
  OpId id = m.addOp(std::move(op));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "out of range");
}

TEST(M1_verify, reject_type_mismatch) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  auto bit = b.measure(q);
  // Now apply an h to a bit (should fail).
  Op op;
  op.kind = OpKind::H;
  op.operands = {bit};
  OpId id = m.addOp(std::move(op));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "expects qubit operand");
}

TEST(M1_verify, reject_missing_attribute) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  // Manually append an Rz with no angle attribute.
  Op op;
  op.kind = OpKind::Rz;
  op.operands = {q};
  OpId id = m.addOp(std::move(op));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "missing required attribute");
}

TEST(M1_verify, reject_no_target) {
  Module m;
  Builder b(m);
  auto q = b.allocQubit();
  (void)b.h(q);
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "target");
}

TEST(M1_verify, reject_duplicate_result_id) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  // Hand-corrupt: two ops with the same result id.
  Op op1; op1.kind = OpKind::H; op1.operands = {q};
  OpId id1 = m.addOp(std::move(op1));
  ValueId v = m.addValue(qubitType(), id1);
  m.opMut(id1).results.push_back(v);
  Op op2; op2.kind = OpKind::H; op2.operands = {v};
  OpId id2 = m.addOp(std::move(op2));
  m.opMut(id2).results.push_back(v);  // duplicate id!
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  std::string joined;
  for (const auto& di : d.items()) joined += di.message + "\n";
  EXPECT_CONTAINS(joined, "duplicate result id");
}

SPINOR_TEST_MAIN()

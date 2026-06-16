// phonon/tests/m5/cancel_test.cpp
//
// Cancellation tests.

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Optimizer.h"
#include "test_main.h"

namespace pd = phonon::dialect;
namespace po = phonon::optimizer;

namespace {

std::size_t countOpKind(const pd::Module& m, pd::OpKind k) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (m.op(pd::OpId{i}).kind == k) ++n;
  }
  return n;
}

}  // namespace

TEST(M5_cancel, h_h_cancels) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.h(q);
  q = b.h(q);
  (void)q;
  auto s = po::cancelInversePairs(m);
  EXPECT_EQ(s.gatesRemoved, static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(m, pd::OpKind::H), static_cast<std::size_t>(0));
}

TEST(M5_cancel, x_x_cancels) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.x(q);
  q = b.x(q);
  (void)q;
  po::cancelInversePairs(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::X), static_cast<std::size_t>(0));
}

TEST(M5_cancel, s_sdg_cancels) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.s(q);
  q = b.sdg(q);
  (void)q;
  po::cancelInversePairs(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::S), static_cast<std::size_t>(0));
  EXPECT_EQ(countOpKind(m, pd::OpKind::Sdg), static_cast<std::size_t>(0));
}

TEST(M5_cancel, cx_cx_cancels) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto [a1, b1] = b.cx(q0, q1);
  auto [a2, b2] = b.cx(a1, b1);
  (void)a2; (void)b2;
  po::cancelInversePairs(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::Cx), static_cast<std::size_t>(0));
}

TEST(M5_cancel, no_false_cancel_when_op_between) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.h(q);
  q = b.x(q);
  q = b.h(q);
  (void)q;
  po::cancelInversePairs(m);
  // Both Hs should remain.
  EXPECT_EQ(countOpKind(m, pd::OpKind::H), static_cast<std::size_t>(2));
}

SPINOR_TEST_MAIN()

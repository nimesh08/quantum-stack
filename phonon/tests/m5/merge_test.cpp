// phonon/tests/m5/merge_test.cpp
//
// Rotation merging tests.

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Optimizer.h"
#include "test_main.h"

#include <cmath>

namespace pd = phonon::dialect;
namespace po = phonon::optimizer;

namespace {
std::size_t countOpKind(const pd::Module& m, pd::OpKind k) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) if (m.op(pd::OpId{i}).kind == k) ++n;
  return n;
}
}

TEST(M5_merge, rz_rz_drops_to_zero) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.rz(0.5, q);
  q = b.rz(-0.5, q);
  (void)q;
  po::mergeRotations(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::Rz), static_cast<std::size_t>(0));
}

TEST(M5_merge, rz_rz_fuses) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.rz(0.4, q);
  q = b.rz(0.3, q);
  (void)q;
  auto s = po::mergeRotations(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::Rz), static_cast<std::size_t>(1));
  EXPECT_EQ(s.rotationsMerged, static_cast<std::size_t>(1));
}

TEST(M5_merge, rx_rx_fuses) {
  pd::Module m; m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  q = b.rx(1.0, q);
  q = b.rx(0.5, q);
  (void)q;
  po::mergeRotations(m);
  EXPECT_EQ(countOpKind(m, pd::OpKind::Rx), static_cast<std::size_t>(1));
}

SPINOR_TEST_MAIN()

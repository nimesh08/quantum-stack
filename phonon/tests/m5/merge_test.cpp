// phonon/tests/m5/merge_test.cpp
//
// Rotation merging tests.

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Optimizer.h"
#include "test_main.h"

#include <cmath>
#include <array>
#include <complex>
#include <numbers>

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

TEST(M5_merge, complete_operator_phase_and_large_angles_are_preserved) {
  using C=std::complex<double>;
  using Matrix=std::array<C,4>;
  // Independent Pauli exponential definitions, without optimizer/compiler
  // matrix helpers or an equality-up-to-global-phase comparison.
  auto rotation=[](pd::OpKind kind,double angle)->Matrix {
    const double c=std::cos(angle/2),s=std::sin(angle/2);
    if(kind==pd::OpKind::Rx)return {c,C(0,-s),C(0,-s),c};
    if(kind==pd::OpKind::Ry)return {c,-s,s,c};
    return {std::exp(C(0,-angle/2)),0,0,std::exp(C(0,angle/2))};
  };
  auto multiply=[](const Matrix& a,const Matrix& b) {
    Matrix product{};
    for(int r=0;r<2;++r)for(int c=0;c<2;++c)for(int k=0;k<2;++k)
      product[2*r+c]+=a[2*r+k]*b[2*k+c];
    return product;
  };
  for(auto kind:{pd::OpKind::Rx,pd::OpKind::Ry,pd::OpKind::Rz})
    for(auto angles:{std::pair{std::numbers::pi,std::numbers::pi},
                     std::pair{-std::numbers::pi,-std::numbers::pi},
                     std::pair{1e16,1.0},std::pair{-1e16,.37},
                     std::pair{1e300,.01},std::pair{1e16,-1e16}}) {
      pd::Module m;m.targetAttr="generic";pd::Builder builder(m);auto q=builder.allocQubit();
      for(double angle:{angles.first,angles.second}){
        if(kind==pd::OpKind::Rx)q=builder.rx(angle,q);
        else if(kind==pd::OpKind::Ry)q=builder.ry(angle,q);
        else q=builder.rz(angle,q);
      }
      const auto expected=multiply(rotation(kind,angles.second),rotation(kind,angles.first));
      po::mergeRotations(m);
      Matrix actual{1,0,0,1};
      for(const auto& op:m.ops())if(op.kind==kind){
        for(const auto& attribute:op.attributes)if(attribute.name=="angle")
          actual=multiply(rotation(kind,std::get<double>(attribute.value)),actual);
      }
      for(int i=0;i<4;++i)EXPECT_TRUE(std::abs(expected[i]-actual[i])<1e-12);
    }
}

SPINOR_TEST_MAIN()

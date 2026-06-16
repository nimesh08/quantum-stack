// phonon/tests/m6/borrowed_test.cpp
//
// Tests for the borrowed-pass adapters.

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/BorrowedPasses.h"
#include "phonon/optimizer/Optimizer.h"
#include "test_main.h"

#include <cstdlib>

namespace pd = phonon::dialect;
namespace po = phonon::optimizer;

namespace {

pd::Module makeBell() {
  pd::Module m;
  m.targetAttr = "generic";
  m.name = "main";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);
  return m;
}

std::size_t numOps(const pd::Module& m) { return m.numOps(); }

}  // namespace

TEST(M6_synth, null_passthrough) {
  pd::Module m = makeBell();
  std::size_t before = numOps(m);
  auto s = po::makeNullSynthesizer();
  s->synthesize(m);
  EXPECT_EQ(numOps(m), before);
  EXPECT_STREQ(s->name(), "null");
}

TEST(M6_synth, factory_returns_named) {
  auto a = po::makeNullSynthesizer();
  auto b_ = po::makeTweedledumSynthesizer();
  EXPECT_STREQ(a->name(), "null");
  // Without SPINOR_HAVE_TWEEDLEDUM the factory returns the null
  // impl (graceful fallback per the spec).
  bool isNull = (b_->name() == "null");
  bool isTweed = (b_->name() == "tweedledum");
  EXPECT_TRUE(isNull || isTweed);
}

TEST(M6_zx, null_passthrough) {
  pd::Module m = makeBell();
  std::size_t before = numOps(m);
  auto s = po::makeNullZxSimplifier();
  s->simplify(m);
  EXPECT_EQ(numOps(m), before);
  EXPECT_STREQ(s->name(), "null");
}

TEST(M6_zx, cassette_replay_identity) {
  pd::Module m = makeBell();
  std::size_t before = numOps(m);
  // Pin the cassette dir.
  setenv("PHONON_CASSETTE_DIR", PHONON_CASSETTE_DIR, /*overwrite=*/1);
  auto s = po::makePyZXSimplifier();
  s->simplify(m);
  // The Phase B cassettes are identity transformations.
  EXPECT_EQ(numOps(m), before);
}

TEST(M6_zx, no_cassette_silent_passthrough) {
  pd::Module m = makeBell();
  m.name = "no_such_cassette";
  std::size_t before = numOps(m);
  setenv("PHONON_CASSETTE_DIR", PHONON_CASSETTE_DIR, /*overwrite=*/1);
  unsetenv("PHONON_PYZX_LIVE");
  auto s = po::makePyZXSimplifier();
  s->simplify(m);
  EXPECT_EQ(numOps(m), before);
}

TEST(M6_swappable, same_pipeline_with_either_impl) {
  pd::Module a = makeBell();
  pd::Module b_ = makeBell();
  auto sNull = po::makeNullZxSimplifier();
  auto sPy   = po::makePyZXSimplifier();
  sNull->simplify(a);
  sPy->simplify(b_);
  // Identity cassette: outputs structurally identical.
  EXPECT_EQ(a.numOps(), b_.numOps());
}

SPINOR_TEST_MAIN()

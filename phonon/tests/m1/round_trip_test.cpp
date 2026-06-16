// phonon/tests/m1/round_trip_test.cpp
//
// Round-trip the Phonon textual format: build → print → parse → print.

#include "phonon/dialect/Phonon.h"
#include "test_main.h"

namespace pd = phonon::dialect;

namespace {

std::string buildAndPrintBell() {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);
  return pd::print(m);
}

std::string buildAndPrintFor() {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q = b.allocQubit();
  auto lo = b.constInt(0);
  auto hi = b.constInt(2);
  pd::OpId fid = b.beginFor("i", lo, hi);
  q = b.h(q);
  b.endFor(fid);
  (void)q;
  return pd::print(m);
}

}  // namespace

TEST(M1_round_trip, bell_phonon_byte_equal) {
  std::string a = buildAndPrintBell();
  pd::Diagnostics d;
  auto m2 = pd::parse(a, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(m2.has_value());
  std::string b = pd::print(*m2);
  EXPECT_STREQ(a, b);
}

TEST(M1_round_trip, for_with_const_bounds) {
  std::string a = buildAndPrintFor();
  pd::Diagnostics d;
  auto m2 = pd::parse(a, d);
  EXPECT_FALSE(d.hasErrors());
  EXPECT_TRUE(m2.has_value());
  std::string b = pd::print(*m2);
  EXPECT_STREQ(a, b);
}

SPINOR_TEST_MAIN()

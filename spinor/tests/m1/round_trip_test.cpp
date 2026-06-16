// spinor/tests/m1/round_trip_test.cpp
//
// M1-T07/08/09 round_trip.{bell,ghz,rotations}
// M1-T10 round_trip.fuzz

#include "spinor/dialect/Spinor.h"
#include "test_main.h"

#include <random>

using namespace spinor::dialect;

namespace {

Module bellModule() {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();  m.setName(q0, "q0");
  auto q1 = b.allocQubit();  m.setName(q1, "q1");
  auto c0 = b.allocBit();    m.setName(c0, "c0");
  auto c1 = b.allocBit();    m.setName(c1, "c1");
  auto q0a = b.h(q0);        m.setName(q0a, "q0_1");
  auto [q0b, q1a] = b.cx(q0a, q1);
  m.setName(q0b, "q0_2"); m.setName(q1a, "q1_1");
  auto m0 = b.measure(q0b);  m.setName(m0, "c0_1");
  auto m1 = b.measure(q1a);  m.setName(m1, "c1_1");
  (void)c0; (void)c1;
  return m;
}

void expectFixedPoint(const Module& m) {
  std::string text1 = print(m);
  Diagnostics d;
  auto parsed = parse(text1, d);
  if (!parsed) {
    std::ostringstream os;
    os << "parse failed:\n" << text1 << "\nDiagnostics:\n";
    for (const auto& di : d.items()) os << "  " << di.message << "\n";
    spinor::test::fail(os.str());
  }
  std::string text2 = print(*parsed);
  EXPECT_STREQ(text2, text1);
}

}  // namespace

TEST(M1_round_trip, bell) {
  expectFixedPoint(bellModule());
}

TEST(M1_round_trip, ghz) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q2 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  auto [q1b, q2a] = b.cx(q1a, q2);
  (void)b.measure(q0b);
  (void)b.measure(q1b);
  (void)b.measure(q2a);
  expectFixedPoint(m);
}

TEST(M1_round_trip, rotations) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  auto a = b.rx(0.25, q);
  auto bb = b.ry(-1.5, a);
  auto c = b.rz(3.14, bb);
  (void)b.t(c);
  expectFixedPoint(m);
}

TEST(M1_round_trip, fuzz) {
  // 200 random valid IR snapshots; print → parse → print is identity.
  std::mt19937 rng(42);
  for (int trial = 0; trial < 200; ++trial) {
    Module m;
    m.targetAttr = (trial % 3 == 0) ? "generic" : "ibm_heron_r2";
    Builder b(m);
    int n = 1 + (rng() % 5);
    std::vector<ValueId> live;
    for (int i = 0; i < n; ++i) live.push_back(b.allocQubit());
    int gates = 1 + (rng() % 12);
    for (int g = 0; g < gates; ++g) {
      uint32_t pick = rng() % 6;
      uint32_t i = rng() % live.size();
      switch (pick) {
        case 0: live[i] = b.h(live[i]); break;
        case 1: live[i] = b.x(live[i]); break;
        case 2: live[i] = b.rz(0.1 * (g + 1), live[i]); break;
        case 3:
          if (live.size() >= 2) {
            uint32_t j = (i + 1 + (rng() % (live.size() - 1))) % live.size();
            auto [a, c] = b.cx(live[i], live[j]);
            live[i] = a; live[j] = c;
          } else {
            live[i] = b.h(live[i]);
          }
          break;
        case 4: live[i] = b.s(live[i]); break;
        case 5: live[i] = b.t(live[i]); break;
      }
    }
    expectFixedPoint(m);
  }
}

SPINOR_TEST_MAIN()

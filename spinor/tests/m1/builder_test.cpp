// spinor/tests/m1/builder_test.cpp
//
// M1-T01 builder.bell_module
// M1-T02 builder.value_uniqueness
// M1-T03 builder.type_signatures

#include "spinor/dialect/Spinor.h"
#include "test_main.h"

#include <set>

using namespace spinor::dialect;

TEST(M1_builder, bell_module) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto c0 = b.allocBit();
  auto c1 = b.allocBit();
  (void)c0; (void)c1;
  auto q0_1 = b.h(q0);
  auto [q0_2, q1_1] = b.cx(q0_1, q1);
  auto bit0 = b.measure(q0_2);
  auto bit1 = b.measure(q1_1);
  (void)bit0; (void)bit1;

  // 2 alloc_qubit + 2 alloc_bit + 1 h + 1 cx + 2 measure = 8 ops
  EXPECT_EQ(static_cast<size_t>(m.numOps()), static_cast<size_t>(8));
  EXPECT_STREQ(m.targetAttr, "generic");
  // value count: 2 q + 2 c + 1 (h result) + 2 (cx results) + 2 (measure bits)
  EXPECT_EQ(static_cast<size_t>(m.numValues()), static_cast<size_t>(9));
}

TEST(M1_builder, value_uniqueness) {
  // Build 1000 random gate sequences; every value id must be unique.
  for (int trial = 0; trial < 1000; ++trial) {
    Module m;
    m.targetAttr = "generic";
    Builder b(m);
    std::vector<ValueId> live;
    int n = 1 + (trial % 5);
    for (int i = 0; i < n; ++i) live.push_back(b.allocQubit());
    int gates = 1 + (trial % 7);
    for (int g = 0; g < gates; ++g) {
      int idx = (trial * 31 + g) % live.size();
      live[idx] = b.h(live[idx]);
    }
    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < m.numValues(); ++i) {
      EXPECT_TRUE(seen.insert(i).second);
    }
  }
}

TEST(M1_builder, type_signatures) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  auto bit = b.measure(q);
  EXPECT_TRUE(m.typeOf(q) == qubitType());
  EXPECT_TRUE(m.typeOf(bit) == bitType());
  // Two-qubit gate produces two qubit values.
  auto a = b.allocQubit();
  auto bb = b.allocQubit();
  auto [a_, bb_] = b.cx(a, bb);
  EXPECT_TRUE(m.typeOf(a_) == qubitType());
  EXPECT_TRUE(m.typeOf(bb_) == qubitType());
}

SPINOR_TEST_MAIN()

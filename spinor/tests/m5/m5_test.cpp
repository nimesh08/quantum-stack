// spinor/tests/m5/m5_test.cpp
//
// All M5 tests in one executable.

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/registry/Registry.h"
#include "test_main.h"

#include <climits>
#include <random>

using namespace spinor::dialect;
using namespace spinor::passes;

namespace {

CouplingGraph linear(std::size_t n) {
  std::vector<std::pair<int, int>> e;
  for (std::size_t i = 0; i + 1 < n; ++i) e.push_back({(int)i, (int)(i + 1)});
  return CouplingGraph(n, e, /*allToAll=*/false);
}
CouplingGraph allToAll(std::size_t n) {
  return CouplingGraph(n, {}, /*allToAll=*/true);
}
CouplingGraph disconnected2x2() {
  return CouplingGraph(4, {{0, 1}, {2, 3}}, false);
}

spinor::registry::ChipInfo dummyChip(const std::string& id, std::size_t n,
                                     bool a2a) {
  spinor::registry::ChipInfo c;
  c.id = id;
  c.qubits = n;
  c.allToAll = a2a;
  c.nativeGates = {"h", "x", "cx", "rz", "swap"};
  return c;
}

bool everyTwoQOnConnected(const Module& m, const CouplingGraph& g,
                          int /*N*/) {
  // Compute, op-by-op, the "physical position" of every value.
  // Routing's output names physical qubits q0..q<P-1>; subsequent
  // ops (gates / swaps) consume per-physical-qubit values. Each
  // value's lineage maps to its current physical location, which we
  // recover by tracking allocation order.
  std::vector<int> physOfValue(m.numValues(), -1);
  int alloc = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      physOfValue[op.results.front().v] = alloc++;
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    if (op.kind == OpKind::Swap) {
      // The output values name the state at positions a and b
      // after the swap. The position label is unchanged on each
      // result; what changes is which `state` lives there.
      int pa = physOfValue[op.operands[0].v];
      int pb = physOfValue[op.operands[1].v];
      physOfValue[op.results[0].v] = pa;
      physOfValue[op.results[1].v] = pb;
    } else {
      int qResults = nq;
      if (op.kind == OpKind::Measure) qResults = 0;
      for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
        physOfValue[op.results[k].v] = physOfValue[op.operands[k].v];
      }
    }
    if (nq == 2 && op.kind != OpKind::Swap) {
      int a = physOfValue[op.operands[0].v];
      int b = physOfValue[op.operands[1].v];
      if (!g.connected(a, b)) return false;
    }
  }
  return true;
}

}  // namespace

// =============================================================
// M5_coupling
// =============================================================

TEST(M5_coupling, linear_distances) {
  CouplingGraph g = linear(4);
  EXPECT_EQ(g.distance(0, 0), 0);
  EXPECT_EQ(g.distance(0, 3), 3);
  EXPECT_EQ(g.distance(1, 2), 1);
  EXPECT_TRUE(g.connected(1, 2));
  EXPECT_FALSE(g.connected(0, 3));
}

TEST(M5_coupling, disconnected) {
  CouplingGraph g = disconnected2x2();
  EXPECT_EQ(g.distance(0, 1), 1);
  EXPECT_EQ(g.distance(2, 3), 1);
  EXPECT_EQ(g.distance(0, 2), INT_MAX);
}

TEST(M5_coupling, all_to_all) {
  CouplingGraph g = allToAll(8);
  for (int i = 0; i < 8; ++i)
    for (int j = 0; j < 8; ++j)
      EXPECT_EQ(g.distance(i, j), i == j ? 0 : 1);
}

// =============================================================
// M5_placement
// =============================================================

TEST(M5_placement, bell_on_linear) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);

  CouplingGraph g = linear(4);
  Placement p;
  Layout L = p.run(m, g);
  // Two virtual qubits placed on a connected pair.
  int p0 = L.v2p[0];
  int p1 = L.v2p[1];
  EXPECT_TRUE(g.connected(p0, p1));
}

TEST(M5_placement, ghz_on_linear) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q2 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  auto [q1b, q2a] = b.cx(q1a, q2);
  (void)q0b; (void)q1b; (void)q2a;
  CouplingGraph g = linear(4);
  Placement p;
  Layout L = p.run(m, g);
  // The two interaction edges (0,1) and (1,2) should each be on
  // adjacent physicals.
  EXPECT_TRUE(g.connected(L.v2p[0], L.v2p[1]));
  EXPECT_TRUE(g.connected(L.v2p[1], L.v2p[2]));
}

// =============================================================
// M5_routing
// =============================================================

TEST(M5_routing, bell_no_swap) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)q0b; (void)q1a;
  CouplingGraph g = linear(4);
  Placement p;
  Layout L = p.run(m, g);
  Routing r;
  auto chip = dummyChip("ibm_heron_r2", 4, false);
  RoutingResult R = r.run(m, chip, g, L);
  EXPECT_EQ(static_cast<int>(R.swapCount), 0);
  EXPECT_TRUE(everyTwoQOnConnected(R.module, g, 4));
}

TEST(M5_routing, long_range_inserts_swaps) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  std::vector<ValueId> qs;
  for (int i = 0; i < 4; ++i) qs.push_back(b.allocQubit());
  auto [q0a, q3a] = b.cx(qs[0], qs[3]);
  (void)q0a; (void)q3a;
  CouplingGraph g = linear(4);
  // Force an identity placement (q[i]→i) by skipping the
  // interaction-graph greedy preference: the cx q[0],q[3] is the
  // single pair, and Placement might pick q0→0,q3→1 (adjacent).
  // To exercise routing we use an adversarial layout.
  Layout L;
  L.v2p = {0, 1, 2, 3};
  L.p2v = {0, 1, 2, 3};
  Routing r;
  auto chip = dummyChip("ibm_heron_r2", 4, false);
  RoutingResult R = r.run(m, chip, g, L);
  EXPECT_TRUE(R.swapCount > 0);
  EXPECT_TRUE(everyTwoQOnConnected(R.module, g, 4));
  EXPECT_EQ(R.module.targetAttr, std::string("ibm_heron_r2"));
}

TEST(M5_routing, all_to_all_zero_swaps) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  std::vector<ValueId> qs;
  for (int i = 0; i < 4; ++i) qs.push_back(b.allocQubit());
  // Hit several non-adjacent pairs.
  auto [a, c] = b.cx(qs[0], qs[3]);
  auto [d, e] = b.cx(qs[1], qs[2]);
  auto [f, h] = b.cx(a, d);
  (void)c; (void)e; (void)f; (void)h;
  CouplingGraph g = allToAll(4);
  Placement p;
  Layout L = p.run(m, g);
  Routing r;
  auto chip = dummyChip("ionq_forte", 4, true);
  RoutingResult R = r.run(m, chip, g, L);
  EXPECT_EQ(static_cast<int>(R.swapCount), 0);
  EXPECT_TRUE(everyTwoQOnConnected(R.module, g, 4));
}

TEST(M5_routing, target_attribute_set) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto [a, c] = b.cx(q0, q1);
  (void)a; (void)c;
  CouplingGraph g = linear(2);
  Placement p;
  Layout L = p.run(m, g);
  Routing r;
  auto chip = dummyChip("custom_chip_id", 2, false);
  RoutingResult R = r.run(m, chip, g, L);
  EXPECT_EQ(R.module.targetAttr, std::string("custom_chip_id"));
}

TEST(M5_routing, fourth_chip_path) {
  // The 4th chip (ionq_aria_proto) must route through the same code
  // path as the first three. We construct the ChipInfo inline (the
  // YAML loader is exercised separately in M4 tests); routing only
  // cares about the (qubits, allToAll, edges) shape.
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  std::vector<ValueId> qs;
  for (int i = 0; i < 5; ++i) qs.push_back(b.allocQubit());
  auto [a0, b0] = b.cx(qs[0], qs[4]);
  (void)a0; (void)b0;
  auto chip = dummyChip("ionq_aria_proto", 25, true);
  CouplingGraph g = allToAll(25);
  Placement p;
  Layout L = p.run(m, g);
  Routing r;
  RoutingResult R = r.run(m, chip, g, L);
  EXPECT_EQ(static_cast<int>(R.swapCount), 0);
  EXPECT_TRUE(everyTwoQOnConnected(R.module, g, 25));
}

// =============================================================
// M5_routing.invariant_holds — randomised property
// =============================================================

TEST(M5_routing, invariant_holds_random_8q) {
  std::mt19937 rng(123);
  CouplingGraph g = linear(8);
  auto chip = dummyChip("synthetic", 8, false);
  for (int trial = 0; trial < 50; ++trial) {
    Module m;
    m.targetAttr = "generic";
    Builder b(m);
    std::vector<ValueId> qs;
    for (int i = 0; i < 8; ++i) qs.push_back(b.allocQubit());
    int gates = 8 + (rng() % 8);
    for (int gi = 0; gi < gates; ++gi) {
      uint32_t pick = rng() % 4;
      uint32_t i = rng() % 8;
      switch (pick) {
        case 0: qs[i] = b.h(qs[i]); break;
        case 1: qs[i] = b.x(qs[i]); break;
        case 2: qs[i] = b.rz(0.1 * (gi + 1), qs[i]); break;
        case 3: {
          uint32_t j = (i + 1 + (rng() % 7)) % 8;
          if (j == i) j = (j + 1) % 8;
          auto [a, c] = b.cx(qs[i], qs[j]);
          qs[i] = a; qs[j] = c;
          break;
        }
      }
    }
    Placement p;
    Layout L = p.run(m, g);
    Routing r;
    RoutingResult R = r.run(m, chip, g, L);
    EXPECT_TRUE(everyTwoQOnConnected(R.module, g, 8));
  }
}

SPINOR_TEST_MAIN()

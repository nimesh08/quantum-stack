// spinor/tests/m8/m8_test.cpp

#include "spinor/dialect/Spinor.h"
#include "spinor/parser/Parser.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/registry/Registry.h"
#include "spinor/sim/Simulator.h"
#include "test_main.h"

#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace spinor;
using namespace spinor::dialect;
using namespace spinor::sim;

#ifndef SPINOR_M8_CORPUS_DIR
#define SPINOR_M8_CORPUS_DIR "."
#endif
#ifndef SPINOR_REGISTRY_ROOT
#define SPINOR_REGISTRY_ROOT "."
#endif

namespace {

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str();
}
std::filesystem::path corpus(const std::string& n) {
  return std::filesystem::path(SPINOR_M8_CORPUS_DIR) / (n + ".spn");
}

}  // namespace

// =============================================================
// M8_sim: statevector smoke tests
// =============================================================

TEST(M8_sim, bell_state_has_two_peaks) {
  auto r = parser::parse(slurp(corpus("bell")));
  EXPECT_TRUE(r.module.has_value());
  StateVector sv = simulate(*r.module);
  EXPECT_EQ(static_cast<int>(sv.qubits), 2);
  // |00> and |11> each |amp|^2 = 1/2; |01> and |10> = 0.
  EXPECT_TRUE(std::abs(std::norm(sv.amps[0]) - 0.5) < 1e-12);
  EXPECT_TRUE(std::abs(std::norm(sv.amps[3]) - 0.5) < 1e-12);
  EXPECT_TRUE(std::abs(sv.amps[1]) < 1e-12);
  EXPECT_TRUE(std::abs(sv.amps[2]) < 1e-12);
}

TEST(M8_sim, ghz_state_has_two_peaks) {
  auto r = parser::parse(slurp(corpus("ghz")));
  EXPECT_TRUE(r.module.has_value());
  StateVector sv = simulate(*r.module);
  EXPECT_EQ(static_cast<int>(sv.qubits), 3);
  // |000> and |111> each 1/2.
  EXPECT_TRUE(std::abs(std::norm(sv.amps[0]) - 0.5) < 1e-12);
  EXPECT_TRUE(std::abs(std::norm(sv.amps[7]) - 0.5) < 1e-12);
  for (int i = 1; i < 7; ++i) {
    EXPECT_TRUE(std::abs(sv.amps[i]) < 1e-12);
  }
}

TEST(M8_sim, rotations_unitary_norm) {
  auto r = parser::parse(slurp(corpus("rotations")));
  EXPECT_TRUE(r.module.has_value());
  StateVector sv = simulate(*r.module);
  double n = 0.0;
  for (auto a : sv.amps) n += std::norm(a);
  EXPECT_TRUE(std::abs(n - 1.0) < 1e-12);
}

// =============================================================
// M8_equiv: equivalence between portable & routed
// =============================================================

TEST(M8_equiv, route_preserves_meaning_bell) {
  auto r = parser::parse(slurp(corpus("bell")));
  EXPECT_TRUE(r.module.has_value());
  Module portable = *r.module;

  // 2-qubit all-to-all chip whose nativeGates includes the
  // standard set so decomposition is a passthrough and the
  // routing output has exactly 2 physical qubits.
  registry::ChipInfo c;
  c.id = "passthrough_chip";
  c.qubits = 2;
  c.allToAll = true;
  c.nativeGates = {"h", "cx", "x", "y", "z", "s", "t", "rx", "ry", "rz",
                   "cz", "swap"};
  c.decompose.oneQubitRotationGate = "rz";
  c.decompose.oneQubitPi2Gate = "";
  c.decompose.twoQubitEntangler = "cx";
  c.decompose.twoQubitEntanglerCountMax = 3;
  passes::CouplingGraph g(c.qubits, {}, true);
  passes::Placement pl;
  auto layout = pl.run(portable, g);
  passes::Routing routing;
  auto routed = routing.run(portable, c, g, layout);
  auto e = equivalent(portable, routed.module);
  EXPECT_TRUE(e.equivalent);
}

TEST(M8_equiv, deliberate_break_caught) {
  // Build two modules that should differ semantically.
  // a: H then Z
  // b: H then X
  Module a;
  a.targetAttr = "generic";
  Builder ba(a);
  auto qa = ba.allocQubit();
  ba.z(ba.h(qa));

  Module b;
  b.targetAttr = "generic";
  Builder bb(b);
  auto qb = bb.allocQubit();
  bb.x(bb.h(qb));

  auto e = equivalent(a, b);
  EXPECT_FALSE(e.equivalent);
}

// =============================================================
// M8_resource: resource estimator
// =============================================================

TEST(M8_resource, bell_counts) {
  auto r = parser::parse(slurp(corpus("bell")));
  EXPECT_TRUE(r.module.has_value());
  ResourceEstimate est = estimate(*r.module);
  EXPECT_EQ(static_cast<int>(est.qubits), 2);
  EXPECT_EQ(static_cast<int>(est.totalGates), 2);
  EXPECT_EQ(static_cast<int>(est.twoQubitGates), 1);
  EXPECT_EQ(static_cast<int>(est.measurements), 2);
  // depth: H then CX => depth 2.
  EXPECT_EQ(static_cast<int>(est.depth), 2);
}

TEST(M8_resource, with_chip_fills_cost_and_error) {
  auto r = parser::parse(slurp(corpus("bell")));
  EXPECT_TRUE(r.module.has_value());
  registry::ChipInfo c;
  c.id = "test_chip";
  c.qubits = 4;
  c.pricePerShotUsd = 0.001;
  ResourceEstimate est = estimate(*r.module, &c, /*shots=*/1000);
  EXPECT_TRUE(est.totalErrorEstimate.has_value());
  EXPECT_TRUE(est.shotCostUsd.has_value());
  EXPECT_TRUE(*est.shotCostUsd > 0.0);
}

TEST(M8_resource, full_corpus_loads) {
  // Each corpus file produces a sensible estimate.
  for (auto name : std::vector<std::string>{"bell", "ghz", "rotations",
                                             "mid_circuit"}) {
    auto r = parser::parse(slurp(corpus(name)));
    EXPECT_TRUE(r.module.has_value());
    ResourceEstimate est = estimate(*r.module);
    EXPECT_TRUE(est.qubits >= 1);
  }
}

SPINOR_TEST_MAIN()

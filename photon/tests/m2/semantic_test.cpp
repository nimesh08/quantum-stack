#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"
#include "phonon/lower/Lowering.h"
#include "phonon/parser/Parser.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/parser/Parser.h"
#include "spinor/passes/PassManager.h"
#include "spinor/sim/Simulator.h"
#include "test_main.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <stdexcept>

namespace pd = phonon::dialect;
namespace sd = spinor::dialect;

namespace {
sd::Module lowerPhoton(const std::string& body, int qubits) {
  auto parsed = photon::lang::parse("target generic\nkernel test() -> void {\nQReg q(" +
      std::to_string(qubits) + ")\n" + body + "\n}\n");
  if (!parsed.module) throw std::runtime_error("Photon parse failed");
  auto phonon = photon::lang::lowerToPhonon(*parsed.module);
  if (!phonon.module) throw std::runtime_error("Photon lowering failed");
  auto spinor = phonon::lower::lower(*phonon.module);
  if (!spinor.module) {
    for (const auto& d : spinor.diag.items()) std::cerr << d.message << '\n';
    throw std::runtime_error("Phonon lowering failed");
  }
  return std::move(*spinor.module);
}

sd::Module sourceRoundTrip(const sd::Module& m) {
  auto parsed = spinor::parser::parse(phonon::lower::emitSpinorSource(m));
  if (!parsed.module) throw std::runtime_error("Spinor source round-trip failed");
  return std::move(*parsed.module);
}

spinor::registry::ChipInfo chip(int qubits) {
  spinor::registry::ChipInfo c;
  c.id = "semantic_quantinuum";
  c.qubits = qubits;
  c.allToAll = true;
  c.nativeGates = {"u1q", "rz", "rzz"};
  c.decompose.twoQubitEntangler = "rzz";
  c.supports.midCircuitMeasure = true;
  c.supports.feedforward = spinor::registry::CapabilityFlags::Feedforward::Full;
  return c;
}

sd::Module native(const sd::Module& m, spinor::passes::OptimizationLevel level, int n) {
  sd::Diagnostics d;
  auto result = spinor::passes::PassManager{}.compile(m, chip(n), level, d);
  if (d.hasErrors()) {
    for (const auto& item : d.items()) std::cerr << item.message << '\n';
    throw std::runtime_error("native compile failed");
  }
  return result;
}

unsigned reverseBits(unsigned x, unsigned n) {
  unsigned reversed = 0;
  for (unsigned bit = 0; bit < n; ++bit) reversed = (reversed << 1) | ((x >> bit) & 1);
  return reversed;
}

void assertTeleported(const sd::Module& m, char expected) {
  std::mt19937_64 rng(42);
  const auto counts = spinor::sim::sample(m, 256, rng);
  std::size_t total = 0;
  for (const auto& [bits, shots] : counts) {
    EXPECT_TRUE(bits.size() >= 3);
    EXPECT_EQ(bits.at(bits.size() - 1 - 2), expected);
    total += shots;
  }
  EXPECT_EQ(total, std::size_t(256));
  // Exercise all four correction combinations, not a single lucky branch.
  EXPECT_EQ(counts.size(), std::size_t(4));
}
}

TEST(M2_semantics, teleport_preserves_zero_one_and_coherence) {
  for (const auto* preparation : {"", "q.x(0)\n", "q.h(0)\n"}) {
    const bool plus = std::string(preparation).find("h") != std::string::npos;
    const char expected = std::string(preparation).find("x") != std::string::npos ? '1' : '0';
    auto m = lowerPhoton(std::string(preparation) + "q.teleport(0, 1, 2)\n" +
        (plus ? "q.h(2)\n" : "") + "Bit result = q.measure(2)", 3);
    auto wire = sd::flatten(m);
    std::vector<int> measured, conditions;
    for (const auto& op : wire.instructions) {
      if (op.kind == sd::OpKind::Measure) measured.push_back(op.clbit);
      if (op.kind == sd::OpKind::If) conditions.push_back(op.clbit);
    }
    EXPECT_EQ(measured.size(), std::size_t(3));
    EXPECT_EQ(measured[2], 2);
    EXPECT_TRUE(measured[0] != measured[1] && measured[0] != 2 && measured[1] != 2);
    EXPECT_EQ(conditions[0], measured[1]);
    EXPECT_EQ(conditions[1], measured[0]);
    assertTeleported(m, expected);
    assertTeleported(sourceRoundTrip(m), expected);
    for (auto level : {spinor::passes::OptimizationLevel::O0, spinor::passes::OptimizationLevel::O3})
      assertTeleported(native(m, level, 3), expected);
  }
}

TEST(M2_semantics, anonymous_measurements_reserve_later_sparse_destinations) {
  pd::Module m; m.targetAttr = "generic"; pd::Builder b(m);
  auto a = b.allocQubit(), c = b.allocQubit();
  auto first = b.measure(a);
  auto one = b.constInt(1);
  auto predicate = b.cmp("==", first, one);
  auto branch = b.beginIf(predicate); b.globalPhase(0.125); b.endIf(branch);
  auto second = b.measure(c);
  m.opMut(m.producerOf(second)).attributes.push_back({"clbit", 7.0});
  auto lowered = phonon::lower::lower(m);
  EXPECT_TRUE(lowered.module.has_value());
  auto wire = sd::flatten(*lowered.module);
  EXPECT_EQ(wire.numClbits, std::size_t(9));
  std::vector<int> destinations;
  for (const auto& op : wire.instructions) {
    if (op.kind == sd::OpKind::Measure) destinations.push_back(op.clbit);
    if (op.kind == sd::OpKind::If) EXPECT_EQ(op.clbit, 8);
  }
  EXPECT_EQ(destinations[0], 8);
  EXPECT_EQ(destinations[1], 7);
}

TEST(M2_semantics, qft_and_iqft_match_exact_fourier_matrix) {
  for (unsigned n : {2U, 3U}) for (const auto* routine : {"qft", "iqft"}) {
    const double sign = std::string(routine) == "qft" ? 1.0 : -1.0;
    const unsigned size = 1U << n;
    for (unsigned input = 0; input < size; ++input) {
      std::string body;
      for (unsigned q = 0; q < n; ++q) if (input & (1U << q))
        body += "q.x(" + std::to_string(q) + ")\n";
      body += std::string("q.") + routine + "()\n";
      auto m = lowerPhoton(body, n);
      for (const auto& candidate : {m, sourceRoundTrip(m),
              native(m, spinor::passes::OptimizationLevel::O0, n),
              native(m, spinor::passes::OptimizationLevel::O3, n)}) {
        const auto state = spinor::sim::simulate(candidate);
        for (unsigned output = 0; output < size; ++output) {
          // Library order treats q[0] as the most significant Fourier bit;
          // the statevector's integer indices are little endian.
          auto expected = std::polar(1.0 / std::sqrt(double(size)), sign * 2.0 *
              std::numbers::pi * reverseBits(input, n) * reverseBits(output, n) / size);
          unsigned physicalOutput = output;
          if (candidate.finalLayout.size() == n) {
            physicalOutput = 0;
            for (unsigned q = 0; q < n; ++q)
              if (output & (1U << q)) physicalOutput |= 1U << candidate.finalLayout[q];
          }
          if (std::abs(state.amps[physicalOutput] - expected) >= 1e-9)
            std::cerr << routine << " n=" << n << " input=" << input << " output=" << output
                      << " actual=" << state.amps[physicalOutput] << " expected=" << expected << '\n';
          EXPECT_TRUE(std::abs(state.amps[physicalOutput] - expected) < 1e-9);
        }
      }
    }
  }
}

TEST(M2_semantics, phase_source_round_trip_and_rejection) {
  auto parsed = phonon::parser::parse("target generic\nqubit q[1]\ngphase(0.375)\nh q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  auto lowered = phonon::lower::lower(*parsed.module);
  EXPECT_TRUE(lowered.module.has_value());
  auto state = spinor::sim::simulate(sourceRoundTrip(*lowered.module));
  EXPECT_TRUE(std::abs(state.amps[0] - std::polar(1 / std::sqrt(2.0), 0.375)) < 1e-12);
  for (const auto* invalid : {"gphase()", "gphase(1, 2)", "gphase(1) q[0]"}) {
    auto text = std::string("target generic\nqubit q[1]\n") + invalid + "\n";
    EXPECT_FALSE(phonon::parser::parse(text).module.has_value());
    EXPECT_FALSE(spinor::parser::parse(text).module.has_value());
  }
}

TEST(M2_semantics, measurement_returns_reject_ignored_arguments) {
  for (const auto* call : {"measure_int(1)", "measure(0, 1)"}) {
    auto parsed = photon::lang::parse(std::string("target generic\nkernel bad() -> int {\nQReg q(2)\nreturn q.") + call + "\n}\n");
    EXPECT_TRUE(parsed.module.has_value());
    auto lowered = photon::lang::lowerToPhonon(*parsed.module);
    EXPECT_FALSE(lowered.module.has_value());
  }
}

SPINOR_TEST_MAIN()

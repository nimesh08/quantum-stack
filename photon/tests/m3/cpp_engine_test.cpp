// photon/tests/m3/cpp_engine_test.cpp
//
// Pure-C++ tests for the engine wrapper used by the nanobind module.

#include "photon/bindings/Engine.h"
#include "test_main.h"

using photon::bindings::CompiledProgram;

TEST(M3_cpp_engine, compile_phonon_bell) {
  static constexpr const char* kBell =
      "target generic\n"
      "qubit q[2]\n"
      "bit c[2]\n"
      "h q[0]\n"
      "cx q[0], q[1]\n"
      "c[0] = measure q[0]\n"
      "c[1] = measure q[1]\n";
  auto cp = CompiledProgram::fromPhononText(kBell, "generic");
  EXPECT_TRUE(cp.ok());
  if (!cp.ok()) {
    std::cerr << "compile error: " << cp.error() << "\n";
  }
  // Spinor dump should be non-empty.
  std::string sd = cp.dumpSpinor();
  EXPECT_FALSE(sd.empty());
  // Phonon dump should also be non-empty.
  std::string pd = cp.dumpPhonon();
  EXPECT_FALSE(pd.empty());
}

TEST(M3_cpp_engine, estimate_bell) {
  static constexpr const char* kBell =
      "target generic\n"
      "qubit q[2]\n"
      "bit c[2]\n"
      "h q[0]\n"
      "cx q[0], q[1]\n"
      "c[0] = measure q[0]\n"
      "c[1] = measure q[1]\n";
  auto cp = CompiledProgram::fromPhononText(kBell, "generic");
  EXPECT_TRUE(cp.ok());
  auto est = cp.estimate();
  EXPECT_EQ(static_cast<int>(est.num_qubits), 2);
  EXPECT_EQ(static_cast<int>(est.two_qubit_count), 1);
  EXPECT_EQ(static_cast<int>(est.t_count), 0);
  EXPECT_TRUE(est.depth >= 4u);  // h + cx + 2 measure
}

TEST(M3_cpp_engine, error_propagation) {
  auto cp = CompiledProgram::fromPhononText(
      "this is not phonon\n", "generic");
  EXPECT_FALSE(cp.ok());
  EXPECT_FALSE(cp.error().empty());
}

TEST(M3_cpp_engine, unbounded_while_error_does_not_report_success) {
  auto cp = CompiledProgram::fromPhononText(
      "target generic\nqubit q[1]\nwhile (1 == 1) {\nx q[0]\n}\n", "generic");
  EXPECT_FALSE(cp.ok());
  EXPECT_TRUE(cp.error().find("parse failed") != std::string::npos);
  EXPECT_TRUE(cp.error().find("while termination was not established") != std::string::npos);
  EXPECT_TRUE(cp.dumpSpinor().empty());
}

TEST(M3_cpp_engine, bounded_while_compiles_every_iteration) {
  auto cp = CompiledProgram::fromPhononText(
      "target generic\nqubit q[3]\nint i = 0\n"
      "while (i < 3) {\nx q[i]\ni = i + 1\n}\n", "generic");
  EXPECT_TRUE(cp.ok());
  EXPECT_TRUE(cp.error().empty());
  auto est = cp.estimate();
  EXPECT_EQ(est.num_qubits, 3u);
  EXPECT_EQ(est.depth, 3u);
  EXPECT_EQ(est.two_qubit_count, 0u);
}

TEST(M3_cpp_engine, supported_measured_condition_is_preserved) {
  auto cp = CompiledProgram::fromPhononText(
      "target generic\nqubit q[2]\nbit c[1]\nc[0] = measure q[0]\nif (c[0] == 1) {\nx q[1]\n}\n", "generic");
  EXPECT_TRUE(cp.ok());
  EXPECT_TRUE(cp.error().empty());
  EXPECT_TRUE(cp.dumpSpinor().find("spinor.if") != std::string::npos);
  EXPECT_TRUE(cp.dumpSpinor().find("spinor.endif") != std::string::npos);
}

SPINOR_TEST_MAIN()

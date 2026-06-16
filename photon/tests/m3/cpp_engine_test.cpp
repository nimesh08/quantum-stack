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

SPINOR_TEST_MAIN()

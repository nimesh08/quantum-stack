// photon/tests/m5/photonc_cli_test.cpp
//
// Drive the photonc-cxx CLI through the portable process runner and check
// expected resource estimate.

#include "test_main.h"
#include "qs/common/cli/Submit.h"

#include <string>

TEST(M5_photonc_cli, bell_compiles) {
  // CTest sets the working directory for relative source paths in the YAML.
  const auto result = qs::common::cli::runProcess({PHOTONC_CLI, "build.yaml"});
  EXPECT_EQ(result.exit_code, 0);
  const auto& out = result.stdout_text;
  EXPECT_TRUE(out.find("compiled.") != std::string::npos);
  EXPECT_TRUE(out.find("num_qubits=2") != std::string::npos);
  EXPECT_TRUE(out.find("two_qubit_count=1") != std::string::npos);
}

SPINOR_TEST_MAIN()

// photon/tests/m5/photonc_cli_test.cpp
//
// Drive the photonc-cxx CLI via popen() and check it prints the
// expected resource estimate.

#include "test_main.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

std::string runCmd(const std::string& cmd) {
  std::string out;
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) return out;
  char buf[4096];
  while (std::fgets(buf, sizeof(buf), f)) out += buf;
  pclose(f);
  return out;
}

}  // namespace

TEST(M5_photonc_cli, bell_compiles) {
  std::string cli = std::string(PHOTONC_CLI);
  std::string yaml = std::string(PHOTON_TEST_CORPUS_DIR) + "/cpp/build.yaml";
  // The yaml references `bell.cpp` relative to its own directory; cd in.
  std::string cmd = "cd " + std::string(PHOTON_TEST_CORPUS_DIR) +
                    "/cpp && " + cli + " build.yaml";
  std::string out = runCmd(cmd);
  EXPECT_TRUE(out.find("compiled.") != std::string::npos);
  EXPECT_TRUE(out.find("num_qubits=2") != std::string::npos);
  EXPECT_TRUE(out.find("two_qubit_count=1") != std::string::npos);
}

SPINOR_TEST_MAIN()

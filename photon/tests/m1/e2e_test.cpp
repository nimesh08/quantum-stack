// photon/tests/m1/e2e_test.cpp
//
// End-to-end: Photon -> Phonon -> verify. Confirms the lowered module
// passes the Phonon dialect verifier (structural checks the Phonon
// pipeline expects upstream).

#include "phonon/dialect/Phonon.h"
#include "photon/lang/Lower.h"
#include "photon/lang/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

using namespace photon::lang;
namespace pd = phonon::dialect;

namespace {
std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}
std::string corpus(const std::string& name) {
  return std::string(PHOTON_TEST_CORPUS_DIR) + "/" + name;
}
}  // namespace

TEST(M1_photon_e2e, bell_verifies) {
  auto pr = parse(readFile(corpus("bell.pho")), "bell.pho");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = lowerToPhonon(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  pd::Diagnostics d;
  pd::verify(*lr.module, d);
  bool errored = false;
  for (const auto& it : d.items()) {
    if (it.severity == pd::DiagSeverity::Error) {
      errored = true;
      std::cerr << "verify error: " << it.message << "\n";
    }
  }
  EXPECT_FALSE(errored);
}

TEST(M1_photon_e2e, ghz_verifies) {
  auto pr = parse(readFile(corpus("ghz.pho")), "ghz.pho");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = lowerToPhonon(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  pd::Diagnostics d;
  pd::verify(*lr.module, d);
  bool errored = false;
  for (const auto& it : d.items()) {
    if (it.severity == pd::DiagSeverity::Error) errored = true;
  }
  EXPECT_FALSE(errored);
}

SPINOR_TEST_MAIN()

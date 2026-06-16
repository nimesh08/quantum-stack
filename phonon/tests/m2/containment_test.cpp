// phonon/tests/m2/containment_test.cpp
//
// Containment proof: every Phase A `.spn` fixture parses unchanged
// through the Phonon parser and produces a Phonon module.

#include "phonon/parser/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

}  // namespace

#define PHONON_PARSES_SPN(name, file)                                  \
TEST(M2_contain, name) {                                                \
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/" + file); \
  auto r = pp::parse(src, file);                                        \
  if (r.diag.hasErrors()) {                                             \
    for (const auto& it : r.diag.items()) {                             \
      std::cerr << "DIAG: " << it.message << "\n";                      \
    }                                                                   \
  }                                                                     \
  EXPECT_FALSE(r.diag.hasErrors());                                     \
  EXPECT_TRUE(r.module.has_value());                                    \
}

PHONON_PARSES_SPN(bell_spn, "bell.spn")
PHONON_PARSES_SPN(ghz_spn, "ghz.spn")
PHONON_PARSES_SPN(rotations_spn, "rotations.spn")
PHONON_PARSES_SPN(native_ibm_spn, "native_ibm.spn")
PHONON_PARSES_SPN(native_ionq_spn, "native_ionq.spn")
PHONON_PARSES_SPN(mid_circuit_spn, "mid_circuit.spn")

SPINOR_TEST_MAIN()

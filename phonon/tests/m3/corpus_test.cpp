// phonon/tests/m3/corpus_test.cpp
//
// Linear type-check the structured Phonon corpus end-to-end:
// teleportation, bell_pair_func, qft_loop.

#include "phonon/parser/Parser.h"
#include "phonon/types/LinearTypeChecker.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;
namespace pt = phonon::types;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

}  // namespace

TEST(M3_linear, teleportation_passes) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/teleportation.phn");
  auto r = pp::parse(src, "teleportation.phn");
  EXPECT_TRUE(r.module.has_value());
  pt::Options o; o.warnImplicitDiscard = false;
  o.midCircuitMeasure = true;
  bool ok = pt::typecheck(*r.module, o, r.diag);
  if (!ok) {
    for (const auto& it : r.diag.items()) {
      if (it.severity == phonon::dialect::DiagSeverity::Error) {
        std::cerr << "DIAG: " << it.message << "\n";
      }
    }
  }
  EXPECT_TRUE(ok);
}

TEST(M3_linear, bell_pair_func_passes) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/bell_pair_func.phn");
  auto r = pp::parse(src, "bell_pair_func.phn");
  EXPECT_TRUE(r.module.has_value());
  pt::Options o; o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(*r.module, o, r.diag);
  if (!ok) {
    for (const auto& it : r.diag.items()) {
      if (it.severity == phonon::dialect::DiagSeverity::Error) {
        std::cerr << "DIAG: " << it.message << "\n";
      }
    }
  }
  EXPECT_TRUE(ok);
}

TEST(M3_linear, qft_loop_passes) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn");
  auto r = pp::parse(src, "qft_loop.phn");
  EXPECT_TRUE(r.module.has_value());
  pt::Options o; o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(*r.module, o, r.diag);
  EXPECT_TRUE(ok);
}

SPINOR_TEST_MAIN()

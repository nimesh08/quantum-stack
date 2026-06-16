// phonon/tests/m4/pipeline_test.cpp
//
// Lowered Phonon output runs through Phase A's verifier without
// extra fix-up.

#include "phonon/lower/Lowering.h"
#include "phonon/parser/Parser.h"
#include "spinor/verify/Verifier.h"
#include "spinor/verify/TargetInfo.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

namespace pp = phonon::parser;
namespace pl = phonon::lower;

namespace {
std::string slurp(const std::string& path) {
  std::ifstream f(path); std::ostringstream o; o << f.rdbuf(); return o.str();
}
}

TEST(M4_pipeline, bell_through_phase_a_verify) {
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/bell.spn");
  auto pr = pp::parse(src, "bell.spn");
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  spinor::dialect::Diagnostics d;
  bool ok = spinor::verify::verify(*lr.module, gen, d);
  if (!ok) {
    for (const auto& it : d.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_TRUE(ok);
}

TEST(M4_pipeline, qft_loop_through_phase_a_verify) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn");
  auto pr = pp::parse(src, "qft_loop.phn");
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  spinor::dialect::Diagnostics d;
  bool ok = spinor::verify::verify(*lr.module, gen, d);
  EXPECT_TRUE(ok);
}

SPINOR_TEST_MAIN()

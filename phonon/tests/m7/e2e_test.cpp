// phonon/tests/m7/e2e_test.cpp
//
// End-to-end Phase B pipeline tests. For each Phonon corpus
// fixture: parse → typecheck → optimize → lower → Phase A
// verifier accepts.

#include "phonon/lower/Lowering.h"
#include "phonon/optimizer/BorrowedPasses.h"
#include "phonon/optimizer/Pipeline.h"
#include "phonon/parser/Parser.h"
#include "phonon/types/LinearTypeChecker.h"
#include "spinor/verify/TargetInfo.h"
#include "spinor/verify/Verifier.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;
namespace pl = phonon::lower;
namespace po = phonon::optimizer;
namespace pt = phonon::types;
namespace pd = phonon::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path); std::ostringstream o; o << f.rdbuf(); return o.str();
}

bool runPhaseBPipeline(const std::string& path,
                       const spinor::verify::TargetInfo& tgt) {
  auto pr = pp::parse(slurp(path), path);
  if (!pr.module) return false;
  pt::Options o; o.warnImplicitDiscard = false;
  o.midCircuitMeasure = tgt.midCircuitMeasure;
  pd::Diagnostics d;
  if (!pt::typecheck(*pr.module, o, d)) {
    for (const auto& it : d.items())
      if (it.severity == pd::DiagSeverity::Error)
        std::cerr << "TYPE: " << it.message << "\n";
    return false;
  }
  po::PipelineConfig cfg;
  po::runPipeline(*pr.module, std::move(cfg));
  auto lr = pl::lower(*pr.module, &tgt);
  if (!lr.module) {
    for (const auto& it : lr.diag.items())
      if (it.severity == spinor::dialect::DiagSeverity::Error)
        std::cerr << "LOWER: " << it.message << "\n";
    return false;
  }
  spinor::dialect::Diagnostics vd;
  bool ok = spinor::verify::verify(*lr.module, tgt, vd);
  if (!ok) {
    for (const auto& it : vd.items())
      std::cerr << "VERIFY: " << it.message << "\n";
  }
  return ok;
}

}  // namespace

TEST(M7_e2e, bell_full_pipeline) {
  std::string path = std::string(SPINOR_CORPUS_DIR) + "/bell.spn";
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  EXPECT_TRUE(runPhaseBPipeline(path, gen));
}

TEST(M7_e2e, ghz_full_pipeline) {
  std::string path = std::string(SPINOR_CORPUS_DIR) + "/ghz.spn";
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  EXPECT_TRUE(runPhaseBPipeline(path, gen));
}

TEST(M7_e2e, qft_loop_full) {
  std::string path = std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn";
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  EXPECT_TRUE(runPhaseBPipeline(path, gen));
}

TEST(M7_e2e, bell_pair_func_full) {
  std::string path = std::string(PHONON_CORPUS_DIR) + "/bell_pair_func.phn";
  spinor::verify::TargetInfo gen = spinor::verify::generic_target();
  EXPECT_TRUE(runPhaseBPipeline(path, gen));
}

SPINOR_TEST_MAIN()

// phonon/tests/m5/pipeline_test.cpp
//
// Pipeline tests: equivalence-preserving runs on the corpus.

#include "phonon/parser/Parser.h"
#include "phonon/optimizer/Optimizer.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

namespace pp = phonon::parser;
namespace po = phonon::optimizer;
namespace pd = phonon::dialect;

namespace {
std::string slurp(const std::string& path) {
  std::ifstream f(path); std::ostringstream o; o << f.rdbuf(); return o.str();
}
std::size_t totalGates(const pd::Module& m) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    auto k = m.op(pd::OpId{i}).kind;
    if (pd::isSpinorKind(k)) {
      // Count gates only (not alloc / measure / reset / barrier).
      if (k != pd::OpKind::AllocQubit && k != pd::OpKind::AllocBit &&
          k != pd::OpKind::Measure && k != pd::OpKind::Reset &&
          k != pd::OpKind::Barrier) ++n;
    }
  }
  return n;
}
}

TEST(M5_pipeline, bell_unchanged) {
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/bell.spn");
  auto pr = pp::parse(src, "bell.spn");
  std::size_t before = totalGates(*pr.module);
  po::runBuiltPipeline(*pr.module);
  std::size_t after = totalGates(*pr.module);
  // Bell has no redundant gates → counts unchanged.
  EXPECT_EQ(before, after);
}

TEST(M5_pipeline, redundant_h_pair_shrinks) {
  std::string src =
      "target generic\n"
      "qubit q[1]\n"
      "h q[0]\n"
      "h q[0]\n";
  auto pr = pp::parse(src, "<test>");
  EXPECT_TRUE(pr.module.has_value());
  std::size_t before = totalGates(*pr.module);
  po::runBuiltPipeline(*pr.module);
  std::size_t after = totalGates(*pr.module);
  EXPECT_TRUE(after < before);
}

SPINOR_TEST_MAIN()

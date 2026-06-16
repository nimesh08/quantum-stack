// phonon/tests/m7/swap_test.cpp
//
// Swap-policy: replacing the IZxSimplifier impl behind the
// interface does not change observable pipeline output for the
// cassette corpus.

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Pipeline.h"
#include "phonon/optimizer/BorrowedPasses.h"
#include "phonon/parser/Parser.h"
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <cstdlib>

namespace pp = phonon::parser;
namespace po = phonon::optimizer;
namespace pd = phonon::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path); std::ostringstream o; o << f.rdbuf(); return o.str();
}

std::size_t totalGateCount(const pd::Module& m) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    pd::OpKind k = m.op(pd::OpId{i}).kind;
    if (pd::isSpinorKind(k) && k != pd::OpKind::AllocQubit &&
        k != pd::OpKind::AllocBit && k != pd::OpKind::Measure &&
        k != pd::OpKind::Reset && k != pd::OpKind::Barrier) ++n;
  }
  return n;
}

}  // namespace

TEST(M7_swap, null_vs_pyzx_same_corpus) {
  setenv("PHONON_CASSETTE_DIR", PHONON_CASSETTE_DIR, /*overwrite=*/1);
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/bell.spn");

  auto a = pp::parse(src, "bell.spn");
  auto b_ = pp::parse(src, "bell.spn");
  EXPECT_TRUE(a.module.has_value()); EXPECT_TRUE(b_.module.has_value());

  po::PipelineConfig ca; ca.zx = po::makeNullZxSimplifier();
  po::runPipeline(*a.module, std::move(ca));

  po::PipelineConfig cb; cb.zx = po::makePyZXSimplifier();
  po::runPipeline(*b_.module, std::move(cb));

  EXPECT_EQ(totalGateCount(*a.module), totalGateCount(*b_.module));
}

SPINOR_TEST_MAIN()

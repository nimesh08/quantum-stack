// phonon/tests/m1/print_golden_test.cpp
//
// Golden-text tests: build a known module and check the printer
// produces the expected textual format byte-for-byte.

#include "phonon/dialect/Phonon.h"
#include "test_main.h"

#include <fstream>
#include <sstream>

using namespace phonon::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

}  // namespace

TEST(M1_print_golden, bell_phonon) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  // Name values for stable output.
  auto q0 = b.allocQubit();  m.setName(q0, "q0");
  auto q1 = b.allocQubit();  m.setName(q1, "q1");
  auto q0a = b.h(q0);        m.setName(q0a, "q0_1");
  auto [q0b, q1a] = b.cx(q0a, q1);
  m.setName(q0b, "q0_2"); m.setName(q1a, "q1_1");
  auto m0 = b.measure(q0b);  m.setName(m0, "c0");
  auto m1 = b.measure(q1a);  m.setName(m1, "c1");

  std::string got = print(m);
  std::string want = slurp(std::string(SPINOR_TEST_GOLDENS_DIR) + "/bell.phn");
  EXPECT_STREQ(got, want);
}

SPINOR_TEST_MAIN()

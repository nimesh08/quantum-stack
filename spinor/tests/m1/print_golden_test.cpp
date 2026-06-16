// spinor/tests/m1/print_golden_test.cpp
//
// M1-T04/05/06 print.{bell,ghz,rotations}_matches_golden

#include "spinor/dialect/Spinor.h"
#include "test_main.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace spinor::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    std::ostringstream os;
    os << "could not read golden " << path;
    spinor::test::fail(os.str());
  }
  std::ostringstream s;
  s << f.rdbuf();
  return s.str();
}

Module bellModule() {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();  m.setName(q0, "q0");
  auto q1 = b.allocQubit();  m.setName(q1, "q1");
  auto c0 = b.allocBit();    m.setName(c0, "c0");
  auto c1 = b.allocBit();    m.setName(c1, "c1");
  auto q0a = b.h(q0);        m.setName(q0a, "q0_1");
  auto [q0b, q1a] = b.cx(q0a, q1);
  m.setName(q0b, "q0_2"); m.setName(q1a, "q1_1");
  auto m0 = b.measure(q0b);  m.setName(m0, "c0_1");
  auto m1 = b.measure(q1a);  m.setName(m1, "c1_1");
  (void)c0; (void)c1;
  return m;
}

Module ghzModule() {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();  m.setName(q0, "q0");
  auto q1 = b.allocQubit();  m.setName(q1, "q1");
  auto q2 = b.allocQubit();  m.setName(q2, "q2");
  auto bb0 = b.allocBit();   m.setName(bb0, "c0");
  auto bb1 = b.allocBit();   m.setName(bb1, "c1");
  auto bb2 = b.allocBit();   m.setName(bb2, "c2");
  (void)bb0; (void)bb1; (void)bb2;
  auto q0a = b.h(q0);        m.setName(q0a, "q0_1");
  auto [q0b, q1a] = b.cx(q0a, q1);
  m.setName(q0b, "q0_2"); m.setName(q1a, "q1_1");
  auto [q1b, q2a] = b.cx(q1a, q2);
  m.setName(q1b, "q1_2"); m.setName(q2a, "q2_1");
  auto r0 = b.measure(q0b);  m.setName(r0, "c0_1");
  auto r1 = b.measure(q1b);  m.setName(r1, "c1_1");
  auto r2 = b.measure(q2a);  m.setName(r2, "c2_1");
  return m;
}

Module rotationsModule() {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();   m.setName(q, "q");
  auto a = b.rx(0.5, q);     m.setName(a, "a");
  auto bb = b.ry(-1.0, a);   m.setName(bb, "b");
  auto c = b.rz(3.141592653589793, bb);  m.setName(c, "c");
  auto d = b.t(c);           m.setName(d, "d");
  auto e = b.s(d);           m.setName(e, "e");
  (void)e;
  return m;
}

#ifndef SPINOR_TEST_GOLDENS_DIR
#define SPINOR_TEST_GOLDENS_DIR "."
#endif

std::string goldenPath(const std::string& name) {
  return std::string(SPINOR_TEST_GOLDENS_DIR) + "/" + name + ".spnir";
}

}  // namespace

TEST(M1_print, bell_matches_golden) {
  std::string actual = print(bellModule());
  std::string expected = slurp(goldenPath("bell"));
  EXPECT_STREQ(actual, expected);
}

TEST(M1_print, ghz_matches_golden) {
  std::string actual = print(ghzModule());
  std::string expected = slurp(goldenPath("ghz"));
  EXPECT_STREQ(actual, expected);
}

TEST(M1_print, rotations_matches_golden) {
  std::string actual = print(rotationsModule());
  std::string expected = slurp(goldenPath("rotations"));
  EXPECT_STREQ(actual, expected);
}

SPINOR_TEST_MAIN()

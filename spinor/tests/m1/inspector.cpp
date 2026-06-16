// spinor/tests/m1/inspector.cpp
//
// Small CLI used during development to generate / update the
// M1 golden files. Not run in CI.
//
// Usage:
//   spinor_m1_inspector bell    > goldens/bell.spnir
//   spinor_m1_inspector ghz     > goldens/ghz.spnir
//   spinor_m1_inspector rotations > goldens/rotations.spnir

#include "spinor/dialect/Spinor.h"

#include <iostream>
#include <string>
#include <vector>

using namespace spinor::dialect;

static Module bell() {
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

static Module ghz() {
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

static Module rotations() {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();  m.setName(q, "q");
  auto a = b.rx(0.5, q);    m.setName(a, "a");
  auto b1 = b.ry(-1.0, a);  m.setName(b1, "b");
  auto c = b.rz(3.141592653589793, b1);  m.setName(c, "c");
  auto d = b.t(c);          m.setName(d, "d");
  auto e = b.s(d);          m.setName(e, "e");
  return m;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: spinor_m1_inspector <bell|ghz|rotations>\n";
    return 2;
  }
  std::string what = argv[1];
  Module m;
  if (what == "bell") m = bell();
  else if (what == "ghz") m = ghz();
  else if (what == "rotations") m = rotations();
  else {
    std::cerr << "unknown program: " << what << "\n";
    return 2;
  }
  std::cout << print(m);
  return 0;
}

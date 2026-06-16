// phonon/tests/m1/verify_test.cpp
//
// Structural verification tests.

#include "phonon/dialect/Phonon.h"
#include "test_main.h"

using namespace phonon::dialect;

TEST(M1_verify, unpaired_if_rejected) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  auto bit_ = b.measure(q);
  (void)b.beginIf(bit_);
  // No endIf!
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  bool found = false;
  for (const auto& it : d.items()) {
    if (it.message.find("phonon.if") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(M1_verify, return_outside_def_rejected) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  ValueId vs[1] = {q};
  b.returnOp(std::span<const ValueId>(vs, 1));
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
}

TEST(M1_verify, cmp_predicate_type) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();   // qubit, not a valid predicate
  (void)b.beginIf(q);
  // Don't pair it, but the predicate type should already be flagged.
  Diagnostics d;
  verify(m, d);
  EXPECT_TRUE(d.hasErrors());
  bool found = false;
  for (const auto& it : d.items()) {
    if (it.message.find("predicate") != std::string::npos) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(M1_verify, valid_program_passes) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q = b.allocQubit();
  q = b.h(q);
  (void)b.measure(q);
  Diagnostics d;
  verify(m, d);
  EXPECT_FALSE(d.hasErrors());
}

SPINOR_TEST_MAIN()

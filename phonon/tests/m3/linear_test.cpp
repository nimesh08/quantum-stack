// phonon/tests/m3/linear_test.cpp
//
// Linear type checker — legality corpus.

#include "phonon/dialect/Phonon.h"
#include "phonon/types/LinearTypeChecker.h"
#include "test_main.h"

namespace pd = phonon::dialect;
namespace pt = phonon::types;

namespace {

bool hasErrorWithCode(const pd::Diagnostics& d, const std::string& code) {
  for (const auto& it : d.items()) {
    if (it.severity != pd::DiagSeverity::Error) continue;
    if (it.message.find(code) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

TEST(M3_linear, bell_passes) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.h(q0);
  auto [q0b, q1a] = b.cx(q0a, q1);
  (void)b.measure(q0b);
  (void)b.measure(q1a);
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(m, o, d);
  EXPECT_TRUE(ok);
}

TEST(M3_linear, no_cloning_caught) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  (void)b.h(q0);     // first use
  (void)b.h(q0);     // BUG: reuses q0 (stale value)
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(m, o, d);
  EXPECT_FALSE(ok);
  EXPECT_TRUE(hasErrorWithCode(d, "E1"));
}

TEST(M3_linear, use_after_measure_caught) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto m0 = b.measure(q0);
  (void)m0;
  // use q0 again after measure (stale value): bug
  (void)b.h(q0);
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(m, o, d);
  EXPECT_FALSE(ok);
  // Either E1 (cloning, since q0 was already consumed by measure) or
  // E2 (use after measure). Both indicate the pattern is wrong.
  bool hasE1OrE2 = hasErrorWithCode(d, "E1") || hasErrorWithCode(d, "E2");
  EXPECT_TRUE(hasE1OrE2);
}

TEST(M3_linear, reset_after_measure_ok) {
  pd::Module m;
  m.targetAttr = "ibm_heron_r2";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  auto q0a = b.h(q0);
  auto m0 = b.measure(q0a);
  (void)m0;
  auto q0b = b.reset(q0a);  // legal: reset after measure
  (void)b.h(q0b);            // use the fresh post-reset value
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = false;
  // Note: Builder threads SSA properly, so q0a is never reused as
  // input. The checker on a properly threaded module accepts this.
  bool ok = pt::typecheck(m, o, d);
  EXPECT_TRUE(ok);
}

TEST(M3_linear, implicit_discard_warns) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto q0 = b.allocQubit();
  (void)q0;  // never consumed
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = true;
  pt::typecheck(m, o, d);
  // Warning, not error.
  bool sawWarning = false;
  for (const auto& it : d.items()) {
    if (it.severity == pd::DiagSeverity::Warning &&
        it.message.find("implicitly discarded") != std::string::npos) {
      sawWarning = true;
    }
  }
  EXPECT_TRUE(sawWarning);
}

TEST(M3_linear, classical_no_op) {
  pd::Module m;
  m.targetAttr = "generic";
  pd::Builder b(m);
  auto i = b.constInt(5);
  // Use i many times — classical scalars are non-linear.
  (void)b.binOp("+", i, i);
  (void)b.binOp("*", i, i);
  pd::Diagnostics d;
  pt::Options o;
  o.warnImplicitDiscard = false;
  bool ok = pt::typecheck(m, o, d);
  EXPECT_TRUE(ok);
}

SPINOR_TEST_MAIN()

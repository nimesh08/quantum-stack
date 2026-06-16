// spinor/tests/m3/verifier_test.cpp
//
// All M3 W-rule and type-check tests live in this single executable.
// The test_main harness collects them by suite name.

#include "spinor/dialect/Spinor.h"
#include "spinor/verify/Verifier.h"
#include "spinor/verify/TargetInfo.h"
#include "test_main.h"

using namespace spinor::dialect;
using namespace spinor::verify;

namespace {

TargetInfo ibmHeronR2Stub(std::size_t qubits = 4) {
  // Linear-4 stub — enough to test connectivity. Real coupling
  // map ships in M4.
  TargetInfo t;
  t.id = "ibm_heron_r2";
  t.generic = false;
  t.nativeGates = {"ecr", "rz", "sx", "x"};
  t.allToAll = false;
  t.qubitCount = qubits;
  t.midCircuitMeasure = true;
  for (std::size_t i = 0; i + 1 < qubits; ++i) {
    t.coupling.push_back({static_cast<int>(i), static_cast<int>(i + 1)});
  }
  return t;
}

TargetInfo earlyChip(std::size_t qubits = 2) {
  TargetInfo t = ibmHeronR2Stub(qubits);
  t.id = "early_chip";
  t.midCircuitMeasure = false;
  return t;
}

TargetInfo ionqForteStub(std::size_t qubits = 4) {
  TargetInfo t;
  t.id = "ionq_forte";
  t.generic = false;
  t.nativeGates = {"ms", "gpi", "gpi2"};
  t.allToAll = true;
  t.qubitCount = qubits;
  t.midCircuitMeasure = false;
  return t;
}

std::string joinErrors(const Diagnostics& d) {
  std::string out;
  for (const auto& di : d.items()) {
    out += di.message + "\n";
  }
  return out;
}

}  // namespace

// =============================================================
// W2 — indices in range
// =============================================================

TEST(M3_w2, in_range_accept) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q0a = b.sx(q0);
  auto [q0b, q1a] = b.ecr(q0a, q1);
  (void)q0b; (void)q1a;
  Diagnostics d;
  bool ok = verify(m, ibmHeronR2Stub(4), d);
  if (!ok) {
    std::ostringstream os;
    os << "unexpected verifier errors:\n" << joinErrors(d);
    spinor::test::fail(os.str());
  }
}

TEST(M3_w2, out_of_range_reject) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  // Allocate 5 qubits but the target only has 2.
  std::vector<ValueId> qs;
  for (int i = 0; i < 5; ++i) qs.push_back(b.allocQubit());
  // Use the 5th qubit (physIdx=5, target.qubitCount=2 → out of range).
  (void)b.sx(qs[4]);
  Diagnostics d;
  EXPECT_FALSE(verify(m, ibmHeronR2Stub(2), d));
  EXPECT_CONTAINS(joinErrors(d), "qubit index");
}

// =============================================================
// W3 — distinct operands in multi-qubit gates
// =============================================================

TEST(M3_w3, distinct_accept) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto [a, c] = b.cx(q0, q1);
  (void)a; (void)c;
  Diagnostics d;
  EXPECT_TRUE(verify(m, generic_target(), d));
}

TEST(M3_w3, duplicate_reject) {
  // Hand-build a `cx q[0], q[0]` (the Builder doesn't let you do
  // it with the well-typed API, but malformed IR is what the
  // verifier exists to catch).
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  // Manually append a CX op with the same operand twice.
  Op op;
  op.kind = OpKind::Cx;
  op.operands = {q0, q0};
  OpId id = m.addOp(std::move(op));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  Diagnostics d;
  EXPECT_FALSE(verify(m, generic_target(), d));
  EXPECT_CONTAINS(joinErrors(d), "distinct operands");
}

// =============================================================
// W4 — no operations on a measured qubit until reset
// =============================================================

TEST(M3_w4, measure_then_use_reject) {
  Module m;
  m.targetAttr = "early_chip";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q0a = b.sx(q0);
  (void)b.measure(q0a);  // sets measured=true
  // Now another gate on the same qubit lineage:
  (void)b.sx(q0a);  // <— W4 violation
  Diagnostics d;
  EXPECT_FALSE(verify(m, earlyChip(2), d));
  EXPECT_CONTAINS(joinErrors(d), "measured");
}

TEST(M3_w4, measure_then_reset_then_use_accept) {
  Module m;
  m.targetAttr = "early_chip";
  Builder b(m);
  auto q0 = b.allocQubit();
  (void)b.measure(q0);
  auto q0_after_reset = b.reset(q0);
  (void)b.sx(q0_after_reset);
  Diagnostics d;
  EXPECT_TRUE(verify(m, earlyChip(2), d));
}

TEST(M3_w4, mid_circuit_relaxes) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q0a = b.sx(q0);
  (void)b.measure(q0a);
  (void)b.sx(q0a);  // would be W4 on early_chip, but ibm allows it
  Diagnostics d;
  EXPECT_TRUE(verify(m, ibmHeronR2Stub(2), d));
}

// =============================================================
// W5 — portable contract: standard gates only
// =============================================================

TEST(M3_w5, standard_only_accept) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto a = b.h(q0);
  auto [c, d_] = b.cx(a, q1);
  (void)b.rz(0.5, c);
  (void)d_;
  Diagnostics d;
  EXPECT_TRUE(verify(m, generic_target(), d));
}

TEST(M3_w5, native_under_generic_reject) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  (void)b.ecr(q0, q1);
  Diagnostics d;
  EXPECT_FALSE(verify(m, generic_target(), d));
  EXPECT_CONTAINS(joinErrors(d), "native gate");
}

// =============================================================
// W6 — hardware contract: native only, on connected pairs
// =============================================================

TEST(M3_w6, native_under_hw_accept) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  (void)b.ecr(q0, q1);
  Diagnostics d;
  EXPECT_TRUE(verify(m, ibmHeronR2Stub(4), d));
}

TEST(M3_w6, standard_under_hw_reject) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  (void)b.cx(q0, q1);
  Diagnostics d;
  EXPECT_FALSE(verify(m, ibmHeronR2Stub(4), d));
  EXPECT_CONTAINS(joinErrors(d), "not native");
}

TEST(M3_w6, unconnected_pair_reject) {
  Module m;
  m.targetAttr = "ibm_heron_r2";
  Builder b(m);
  // Linear-4 coupling: edges (0-1, 1-2, 2-3). 0 and 3 are not
  // connected.
  std::vector<ValueId> qs;
  for (int i = 0; i < 4; ++i) qs.push_back(b.allocQubit());
  (void)b.ecr(qs[0], qs[3]);
  Diagnostics d;
  EXPECT_FALSE(verify(m, ibmHeronR2Stub(4), d));
  EXPECT_CONTAINS(joinErrors(d), "connected pair");
}

TEST(M3_w6, all_to_all_accept) {
  Module m;
  m.targetAttr = "ionq_forte";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto q1 = b.allocQubit();
  auto q2 = b.allocQubit();
  (void)b.ms(q0, q2);  // any pair is fine on all-to-all
  (void)q1;
  Diagnostics d;
  EXPECT_TRUE(verify(m, ionqForteStub(3), d));
}

// =============================================================
// type-check (M3 layer)
// =============================================================

TEST(M3_typecheck, measure_writes_a_bit) {
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  ValueId bit = b.measure(q0);
  EXPECT_TRUE(m.typeOf(bit) == bitType());
  Diagnostics d;
  EXPECT_TRUE(verify(m, generic_target(), d));
}

TEST(M3_typecheck, reset_on_bit_rejected) {
  // Hand-corrupt: a reset op whose operand is a bit value.
  Module m;
  m.targetAttr = "generic";
  Builder b(m);
  auto q0 = b.allocQubit();
  auto bit = b.measure(q0);
  Op op;
  op.kind = OpKind::Reset;
  op.operands = {bit};  // wrong type
  OpId id = m.addOp(std::move(op));
  m.opMut(id).results.push_back(m.addValue(qubitType(), id));
  Diagnostics d;
  EXPECT_FALSE(verify(m, generic_target(), d));
  EXPECT_CONTAINS(joinErrors(d), "qubit");
}

// =============================================================
// Golden error phrases
// =============================================================

TEST(M3_golden_errors, w_codes_grep_friendly) {
  // For each rule, build a violating module, run verify, and
  // assert the error string contains the rule's W-code.
  {
    Module m;
    m.targetAttr = "ibm_heron_r2";
    Builder b(m);
    std::vector<ValueId> qs;
    for (int i = 0; i < 5; ++i) qs.push_back(b.allocQubit());
    (void)b.sx(qs[4]);
    Diagnostics d;
    verify(m, ibmHeronR2Stub(2), d);
    EXPECT_CONTAINS(joinErrors(d), "W2:");
  }
  {
    Module m;
    m.targetAttr = "generic";
    Builder b(m);
    auto q0 = b.allocQubit();
    Op op; op.kind = OpKind::Cx; op.operands = {q0, q0};
    OpId id = m.addOp(std::move(op));
    m.opMut(id).results.push_back(m.addValue(qubitType(), id));
    m.opMut(id).results.push_back(m.addValue(qubitType(), id));
    Diagnostics d;
    verify(m, generic_target(), d);
    EXPECT_CONTAINS(joinErrors(d), "W3:");
  }
  {
    Module m;
    m.targetAttr = "early_chip";
    Builder b(m);
    auto q0 = b.allocQubit();
    auto q1 = b.allocQubit();
    (void)q1;
    auto q0a = b.sx(q0);
    (void)b.measure(q0a);
    (void)b.sx(q0a);
    Diagnostics d;
    verify(m, earlyChip(2), d);
    EXPECT_CONTAINS(joinErrors(d), "W4:");
  }
  {
    Module m;
    m.targetAttr = "generic";
    Builder b(m);
    auto q0 = b.allocQubit();
    auto q1 = b.allocQubit();
    (void)b.ecr(q0, q1);
    Diagnostics d;
    verify(m, generic_target(), d);
    EXPECT_CONTAINS(joinErrors(d), "W5:");
  }
  {
    Module m;
    m.targetAttr = "ibm_heron_r2";
    Builder b(m);
    auto q0 = b.allocQubit();
    auto q1 = b.allocQubit();
    (void)b.cx(q0, q1);
    Diagnostics d;
    verify(m, ibmHeronR2Stub(4), d);
    EXPECT_CONTAINS(joinErrors(d), "W6:");
  }
}

SPINOR_TEST_MAIN()

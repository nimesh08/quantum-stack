// spinor/tests/m9/m9_test.cpp

#include "spinor/dialect/Spinor.h"
#include "spinor/emit/Emitters.h"
#include "spinor/parser/Parser.h"
#include "spinor/parser/Qasm.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/registry/Registry.h"
#include "test_main.h"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace spinor;
using namespace spinor::dialect;
using namespace spinor::emit;

#ifndef SPINOR_M9_CORPUS_DIR
#define SPINOR_M9_CORPUS_DIR "."
#endif

namespace {

std::string slurp(const std::filesystem::path& p) {
  std::ifstream f(p); std::ostringstream s; s << f.rdbuf(); return s.str();
}
std::filesystem::path corpus(const std::string& n) {
  return std::filesystem::path(SPINOR_M9_CORPUS_DIR) / (n + ".spn");
}
Module compileBell(const registry::ChipInfo& chip) {
  auto r = parser::parse(slurp(corpus("bell")));
  passes::CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  passes::Placement pl;
  auto layout = pl.run(*r.module, g);
  passes::Routing routing;
  auto routed = routing.run(*r.module, chip, g, layout);
  passes::Decomposition dec;
  Diagnostics d;
  return dec.run(routed.module, chip, d);
}
registry::ChipInfo ibm() {
  registry::ChipInfo c; c.id = "ibm_test"; c.qubits = 4;
  c.allToAll = false;
  c.nativeGates = {"ecr", "rz", "sx", "x"};
  c.decompose.oneQubitRotationGate = "rz";
  c.decompose.oneQubitPi2Gate = "sx";
  c.decompose.twoQubitEntangler = "ecr";
  c.decompose.twoQubitEntanglerCountMax = 3;
  for (int i = 0; i + 1 < 4; ++i) c.coupling.push_back({i, i + 1});
  return c;
}
registry::ChipInfo quantinuum() {
  registry::ChipInfo c; c.id = "quantinuum_test"; c.qubits = 4;
  c.qirPlatform = "quantinuum-h2";
  c.allToAll = true;
  c.nativeGates = {"u1q", "rzz"};
  c.decompose.oneQubitRotationGate = "u1q";
  c.decompose.oneQubitPi2Gate = "";
  c.decompose.twoQubitEntangler = "rzz";
  c.decompose.twoQubitEntanglerCountMax = 3;
  c.supports.feedforward = registry::CapabilityFlags::Feedforward::Full;
  return c;
}

}  // namespace

// =============================================================
// QASM3
// =============================================================

TEST(M9_qasm3, bell_emits_header) {
  auto r = parser::parse(slurp(corpus("bell")));
  std::string out = emitQasm3(*r.module);
  EXPECT_CONTAINS(out, "OPENQASM");
  EXPECT_CONTAINS(out, "qubit[");
  EXPECT_CONTAINS(out, "h q[");
  EXPECT_CONTAINS(out, "cx q[");
  EXPECT_CONTAINS(out, "measure q[");
}

TEST(M9_qasm3, braket_verbatim_box_present) {
  registry::ChipInfo chip = ibm();
  Module compiled = compileBell(chip);
  EmitOptions opts; opts.braketVerbatim = true;
  std::string out = emitQasm3(compiled, &chip, opts);
  EXPECT_CONTAINS(out, "#pragma braket verbatim");
  EXPECT_CONTAINS(out, "box {");
  EXPECT_CONTAINS(out, "$0");      // physical qubit syntax
  EXPECT_CONTAINS(out, "ecr ");    // native gate
}

TEST(M9_qasm3, round_trip_with_qasm_importer) {
  // emit then re-import through M11; counts must match.
  auto r = parser::parse(slurp(corpus("bell")));
  std::string emitted = emitQasm3(*r.module);
  auto reim = parser::qasm::import(emitted);
  EXPECT_TRUE(reim.module.has_value());
  EXPECT_EQ(reim.module->numOps(), r.module->numOps());
}

TEST(M9_qasm3, importer_preserves_native_parameters_and_rejects_invalid_input) {
  auto native=parser::qasm::import("OPENQASM 3.0;qubit[2] q;u1q(0.3,0.4) q[0];rxx(0.7) q[0],q[1];rzz(-0.9) q[1],q[0];");
  EXPECT_TRUE(native.module.has_value());
  EXPECT_CONTAINS(print(*native.module),"theta = 0.3");
  EXPECT_CONTAINS(print(*native.module),"angle = -0.9");
  for(auto statement:{"x(1) q[0]","rx(0.3foo) q[0]","cx q[0],q[0]","x q[2]","x q[-1]"}){
    auto bad=parser::qasm::import(std::string("OPENQASM 3.0;qubit[2] q;")+statement+";");
    EXPECT_FALSE(bad.module.has_value());
  }
}

// =============================================================
// QIR
// =============================================================

TEST(M9_qir, bell_emits_base_profile) {
  auto r = parser::parse(slurp(corpus("bell")));
  std::string out = emitQir(*r.module);
  EXPECT_CONTAINS(out, "@__quantum__qis__h__body(ptr)");
  EXPECT_CONTAINS(out, "!llvm.module.flags");
  EXPECT_CONTAINS(out, "@__quantum__rt__result_record_output");
  EXPECT_CONTAINS(out, "@__quantum__qis__h__body");
  EXPECT_CONTAINS(out, "@__quantum__qis__cnot__body");
  EXPECT_CONTAINS(out, "@__quantum__qis__mz__body");
  EXPECT_CONTAINS(out, "base_profile");
  // QIR 1.0 base profile: initialization, transformations, irreversible
  // operations, output. Labels are required by the labeled output schema.
  EXPECT_CONTAINS(out, "define i64 @main()");
  EXPECT_CONTAINS(out, "ret i64 0");
  EXPECT_CONTAINS(out, "entry:\n");
  EXPECT_CONTAINS(out, "body:\n");
  EXPECT_CONTAINS(out, "measurements:\n");
  EXPECT_CONTAINS(out, "output:\n");
  EXPECT_CONTAINS(out, "ptr writeonly");
  EXPECT_CONTAINS(out, "\"irreversible\"");
  EXPECT_CONTAINS(out, "\"output_labeling_schema\"=\"labeled\"");
  EXPECT_CONTAINS(out, "@output_label0 = private constant");
  EXPECT_TRUE(out.find("bool_record_output") == std::string::npos);
}

TEST(M9_qir, quantinuum_emits_adaptive_profile) {
  registry::ChipInfo chip = quantinuum();
  Module compiled = compileBell(chip);
  std::string out = emitQir(compiled, &chip);
  EXPECT_CONTAINS(out, "adaptive_profile");
}

TEST(M9_qir, qubit_count_metadata_present) {
  auto r = parser::parse(slurp(corpus("ghz")));
  std::string out = emitQir(*r.module);
  EXPECT_CONTAINS(out, "required_num_qubits");
  EXPECT_CONTAINS(out, "required_num_results");
}

// =============================================================
// Quil
// =============================================================

TEST(M9_quil, bell_emits_quil) {
  auto r = parser::parse(slurp(corpus("bell")));
  std::string out = emitQuil(*r.module);
  EXPECT_CONTAINS(out, "DECLARE ro BIT[2]");
  EXPECT_CONTAINS(out, "H 0");
  EXPECT_CONTAINS(out, "CNOT 0 1");
  EXPECT_CONTAINS(out, "MEASURE 0 ro[0]");
}

// =============================================================
// Verbatim invariants (M9 spec §)
// =============================================================

TEST(M9_verbatim_invariants, no_standard_gates_in_box) {
  registry::ChipInfo chip = ibm();
  Module compiled = compileBell(chip);
  EmitOptions opts; opts.braketVerbatim = true;
  std::string out = emitQasm3(compiled, &chip, opts);
  // No `cx ` or `h ` (lowercase, with trailing space) inside the
  // verbatim box — only native gates.
  // We do a simple substring scan; the body is small.
  EXPECT_TRUE(out.find("\n  cx ") == std::string::npos);
  EXPECT_TRUE(out.find("\n  h ")  == std::string::npos);
}

SPINOR_TEST_MAIN()

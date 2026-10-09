// phonon/tests/m4/lower_test.cpp
//
// Phonon → Spinor lowering: structural and pipeline tests.

#include "phonon/lower/Lowering.h"
#include "phonon/parser/Parser.h"
#include "spinor/dialect/Spinor.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/sim/Simulator.h"
#include "spinor/emit/Emitters.h"
#include <random>
#include <algorithm>
#include "test_main.h"

#include <fstream>
#include <sstream>
#include <string>

namespace pp = phonon::parser;
namespace pl = phonon::lower;
namespace sd = spinor::dialect;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

std::size_t countOpKind(const sd::Module& m, sd::OpKind k) {
  std::size_t n = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (m.op(sd::OpId{i}).kind == k) ++n;
  }
  return n;
}

}  // namespace

TEST(M4_lower, bell_passthrough) {
  // Parse bell.spn through the Phonon parser, then lower.
  std::string src = slurp(std::string(SPINOR_CORPUS_DIR) + "/bell.spn");
  auto pr = pp::parse(src, "bell.spn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  if (lr.diag.hasErrors()) {
    for (const auto& it : lr.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_TRUE(lr.module.has_value());
  // Bell: 2 alloc_qubit, 2 alloc_bit, 1 h, 1 cx, 2 measure.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocQubit), static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocBit),   static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H),          static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Cx),         static_cast<std::size_t>(1));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Measure),    static_cast<std::size_t>(2));
}

TEST(M4_lower, qft_loop_unrolls) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/qft_loop.phn");
  auto pr = pp::parse(src, "qft_loop.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  // Each iteration must address its own physical logical slot.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H), static_cast<std::size_t>(4));
  const auto source = pl::emitSpinorSource(*lr.module);
  for (int i = 0; i < 4; ++i)
    EXPECT_TRUE(source.find("h q[" + std::to_string(i) + "]") != std::string::npos);
}

TEST(M4_lower, bell_pair_func_inlines) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/bell_pair_func.phn");
  auto pr = pp::parse(src, "bell_pair_func.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  if (lr.diag.hasErrors()) {
    for (const auto& it : lr.diag.items()) std::cerr << "DIAG: " << it.message << "\n";
  }
  EXPECT_TRUE(lr.module.has_value());
  // Inlined twice: 2 H ops, 2 CX ops; 4 alloc_qubit, 4 alloc_bit; 4 measure.
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocQubit), static_cast<std::size_t>(4));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::AllocBit),   static_cast<std::size_t>(4));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::H),          static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Cx),         static_cast<std::size_t>(2));
  EXPECT_EQ(countOpKind(*lr.module, sd::OpKind::Measure),    static_cast<std::size_t>(4));
}

TEST(M4_lower, dynamic_branches_preserve_classical_conditions) {
  std::string src = slurp(std::string(PHONON_CORPUS_DIR) + "/teleportation.phn");
  auto pr = pp::parse(src, "teleportation.phn");
  EXPECT_TRUE(pr.module.has_value());
  auto lr = pl::lower(*pr.module);
  EXPECT_TRUE(lr.module.has_value());
  EXPECT_FALSE(lr.diag.hasErrors());
  if(!lr.module)return;
  EXPECT_EQ(countOpKind(*lr.module,sd::OpKind::If),std::size_t(2));
  EXPECT_EQ(countOpKind(*lr.module,sd::OpKind::EndIf),std::size_t(2));
  auto source=pl::emitSpinorSource(*lr.module);
  EXPECT_CONTAINS(source,"if c[1] == 1 {");
  EXPECT_CONTAINS(source,"if c[0] == 1 {");
}

TEST(M4_lower, nested_loops_angles_and_readout_permutation) {
  auto parsed = pp::parse("target generic\nqubit q[4]\nbit c[5]\nfor i in 0..2 {\nfor j in 0..2 {\nrx((i * 2 + j) / 4) q[i * 2 + j]\n}\n}\nc[4] = measure q[0]\nc[1] = measure q[3]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_TRUE(source.find("rx(0.25) q[1]") != std::string::npos);
  EXPECT_TRUE(source.find("rx(0.75) q[3]") != std::string::npos);
  EXPECT_TRUE(source.find("bit c[5]") != std::string::npos);
  EXPECT_TRUE(source.find("c[4] = measure q[0]") != std::string::npos);
  EXPECT_TRUE(source.find("c[1] = measure q[3]") != std::string::npos);
}

TEST(M4_lower, static_else_retains_following_qubit_value) {
  auto parsed = pp::parse("target generic\nqubit q[1]\nif (1 == 0) {\nx q[0]\n} else {\nz q[0]\n}\nh q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  auto source = pl::emitSpinorSource(*result.module);
  EXPECT_TRUE(source.find("x q[0]") == std::string::npos);
  EXPECT_TRUE(source.find("z q[0]\nh q[0]") != std::string::npos);
}

TEST(M4_lower, invalid_indices_sizes_and_parameters_fail) {
  for (const auto* body : {"qubit q[-1]\n", "qubit q[2]\nx q[0.5]\n",
                           "qubit q[1]\nrx(theta) q[0]\n", "qubit q[1]\nrx q[0]\n",
                           "qubit q[1]\nx q[9]\n", "}\n",
                           "qubit q[2]\nbit c[1]\nc[0] = measure q\n",
                           "reset missing\n", "barrier missing\n",
                           "qubit q[999999999999999999999999999999999999]\n"}) {
    auto parsed = pp::parse(std::string("target generic\n") + body);
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, native_rxx_parameter_survives_frontend) {
  auto parsed = pp::parse("target generic\nqubit q[2]\nrxx(0.125) q[0], q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (result.module) EXPECT_TRUE(pl::emitSpinorSource(*result.module).find(
      "rxx(0.125) q[0], q[1]") != std::string::npos);
}

TEST(M4_lower, bounded_while_rebinds_indices_angles_and_following_scalar) {
  auto parsed = pp::parse("target generic\nqubit q[4]\nint i = 3\n"
      "while (i >= 0) {\nrx(i / 4) q[i]\ni = i - 1\n}\n"
      "if (i == -1) {\nx q[0]\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "rx(0.75) q[3]\nrx(0.5) q[2]\nrx(0.25) q[1]\nrx(0) q[0]");
  EXPECT_CONTAINS(source, "x q[0]");
}

TEST(M4_lower, while_requires_static_finite_termination) {
  for (const auto& body : {"while (1 == 1) {\n}\n",
      "bit c[1]\nc[0] = measure q[0]\nwhile (c[0] == 1) {\n}\n"}) {
    auto parsed = pp::parse(std::string("target generic\nqubit q[1]\n") + body);
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, function_angles_bind_per_call_and_shadow_outer_constants) {
  auto parsed = pp::parse("target generic\nangle theta = 9\n"
      "def rotate(qubit a, angle theta) {\nangle doubled = theta * 2\n"
      "rx(doubled) a\nu1q(theta, -theta / 2) a\ngphase(theta / 4)\n}\n"
      "qubit q[2]\nrotate(q[0], 0.25)\nrotate(q[1], 0.5)\nrz(theta) q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  phonon::dialect::Diagnostics structural;
  phonon::dialect::verify(*parsed.module, structural);
  EXPECT_FALSE(structural.hasErrors());
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "rx(0.5) q[0]");
  EXPECT_CONTAINS(source, "rx(1) q[1]");
  EXPECT_CONTAINS(source, "u1q(0.25, -0.125) q[0]");
  EXPECT_CONTAINS(source, "u1q(0.5, -0.25) q[1]");
  EXPECT_CONTAINS(source, "rz(9) q[1]");
}

TEST(M4_lower, function_scope_does_not_leak_or_replace_outer_registers) {
  auto parsed = pp::parse("target generic\nqubit q[1]\nangle theta = 0.125\n"
      "def rotate(qubit q, angle theta) {\nangle local = theta + 0.25\nrx(local) q\n}\n"
      "rotate(q[0], 0.5)\nrz(theta) q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "rx(0.75) q[0]\nrz(0.125) q[0]");
  auto leaking = pp::parse("target generic\ndef f(qubit a) {\nint secret = 3\n}\n"
                          "qubit q[1]\nrx(secret) q[0]\n");
  EXPECT_FALSE(leaking.module.has_value());
}

TEST(M4_lower, invalid_bound_function_angles_fail_instead_of_becoming_zero) {
  auto parsed = pp::parse("target generic\ndef f(qubit a, angle theta) {\nrx(theta / 0) a\n}\n"
                         "qubit q[1]\nf(q[0], 0.5)\n");
  EXPECT_FALSE(parsed.module.has_value());
  EXPECT_TRUE(parsed.diag.hasErrors());
}

TEST(M4_lower, scalar_assignments_update_later_static_gate_parameters) {
  auto parsed = pp::parse("target generic\nqubit q[2]\nint index = 0\n"
      "index = index + 1\nangle theta = 0.125\ntheta = theta * 2\nrx(theta) q[index]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (result.module) EXPECT_CONTAINS(pl::emitSpinorSource(*result.module), "rx(0.25) q[1]");
}

TEST(M4_lower, whole_register_and_scalar_readouts_keep_declared_destinations) {
  auto parsed = pp::parse("target generic\nqubit q[3]\nbit scalar[1]\nbit pair[2]\n"
      "qubit other[2]\npair = measure other\nscalar = measure q[2]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "bit c[3]");
  EXPECT_CONTAINS(source, "c[1] = measure q[3]");
  EXPECT_CONTAINS(source, "c[2] = measure q[4]");
  EXPECT_CONTAINS(source, "c[0] = measure q[2]");
}

TEST(M4_lower, nested_function_parameters_and_bit_predicates_resolve) {
  auto parsed = pp::parse("target generic\n"
      "def inner(qubit a, angle value) {\nrz(value + 0.25) a\n}\n"
      "def outer(qubit a, angle value, bit flag) {\n"
      "if (flag != 0) {\ninner(a, value * 2)\n}\n}\n"
      "qubit q[2]\nbit c[1]\nc = measure q[0]\nouter(q[1], 0.25, c)\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "if c[0] == 1 {");
  EXPECT_CONTAINS(source, "rz(0.75) q[1]");
}

TEST(M4_lower, specialized_indices_bounds_and_lexical_scalars_bind_at_each_call) {
  const std::string program = "target generic\nqubit q[4]\nint slot = 0\nint count = 9\n"
      "def apply(int count, angle scale) {\nfor i in 0..count {\n"
      "rx(scale * (i + 1)) q[slot]\n}\ncount = 0\n}\n"
      "slot = 1\napply(2, 0.125)\nslot = 3\napply(1, 0.5)\n"
      "rz(count) q[0]\n";
  auto parsed = pp::parse(program);
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "rx(0.125) q[1]\nrx(0.25) q[1]\nrx(0.5) q[3]");
  EXPECT_CONTAINS(source, "rz(9) q[0]");
  auto again = pp::parse(program);
  auto repeated = pl::lower(*again.module);
  EXPECT_EQ(source, pl::emitSpinorSource(*repeated.module));
}

TEST(M4_lower, nested_specializations_keep_lexical_capture_and_parameter_aliases) {
  auto parsed = pp::parse("target generic\nqubit q[4]\nint slot = 0\n"
      "def inner(int count) {\nfor i in 0..count {\nx q[slot]\n}\n}\n"
      "def outer(qubit q, int slot, int count) {\ninner(count)\nrx(slot / 4) q\n}\n"
      "def wrapper(qubit a) {\nouter(a, 2, 1)\n}\n"
      "slot = 1\nouter(q[3], 3, 2)\nwrapper(q[1])\nh q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  phonon::dialect::Diagnostics structural;
  phonon::dialect::verify(*parsed.module, structural);
  EXPECT_FALSE(structural.hasErrors());
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_CONTAINS(source, "x q[1]\nx q[1]\nrx(0.75) q[3]");
  EXPECT_CONTAINS(source, "x q[1]\nrx(0.5) q[1]\nh q[1]");
}

TEST(M4_lower, specialized_allocation_while_and_readout_parameters) {
  auto parsed = pp::parse("target generic\n"
      "def prepare(qubit a, bit out, int width) {\nqubit local[width]\n"
      "int i = 0\nwhile (i < width) {\nx local[i]\ni = i + 1\n}\n"
      "rx(width / 4) a\nout = measure a\n}\n"
      "qubit q[2]\nbit c[3]\nprepare(q[0], c[2], 1)\nprepare(q[1], c[0], 2)\n"
      "if (c[2] == 1) {\nx q[1]\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::AllocQubit), std::size_t(5));
  const auto flat=sd::flatten(*result.module);
  EXPECT_EQ(flat.instructions.size(),std::size_t(10));
  EXPECT_TRUE(flat.instructions[0].kind==sd::OpKind::X);EXPECT_EQ(flat.instructions[0].qubits[0],2u);
  EXPECT_TRUE(flat.instructions[1].kind==sd::OpKind::Rx);EXPECT_EQ(flat.instructions[1].qubits[0],0u);
  EXPECT_EQ(sd::parameter(flat.instructions[1]),0.25);
  EXPECT_TRUE(flat.instructions[2].kind==sd::OpKind::Measure);EXPECT_EQ(flat.instructions[2].clbit,2);
  EXPECT_TRUE(flat.instructions[3].kind==sd::OpKind::X);EXPECT_EQ(flat.instructions[3].qubits[0],3u);
  EXPECT_TRUE(flat.instructions[4].kind==sd::OpKind::X);EXPECT_EQ(flat.instructions[4].qubits[0],4u);
  EXPECT_TRUE(flat.instructions[5].kind==sd::OpKind::Rx);EXPECT_EQ(flat.instructions[5].qubits[0],1u);
  EXPECT_EQ(sd::parameter(flat.instructions[5]),0.5);
  EXPECT_TRUE(flat.instructions[6].kind==sd::OpKind::Measure);EXPECT_EQ(flat.instructions[6].clbit,0);
  EXPECT_TRUE(flat.instructions[7].kind==sd::OpKind::If);EXPECT_EQ(flat.instructions[7].clbit,2);
  EXPECT_TRUE(flat.quantumInputs==std::vector<int>({0,1}));
  EXPECT_TRUE(flat.reservedPool==std::vector<int>({2,3,4}));
  sd::Diagnostics diagnostics;const auto reparsed=sd::parse(source,diagnostics);
  EXPECT_TRUE(reparsed.has_value());
  if(reparsed)EXPECT_EQ(sd::print(*reparsed),source);
}

TEST(M4_lower, specialized_static_return_exits_loops_without_emitting_dead_body) {
  auto parsed = pp::parse("target generic\ndef first(qubit a, int count) {\n"
      "for i in 0..count {\nif (i == 1) {\nreturn a\n}\nx a\n}\n"
      "qubit dead[7]\nz a\n}\nqubit q[2]\nfirst(q[0], 3)\nfirst(q[1], 1)\nh q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::AllocQubit), std::size_t(9));
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::X), std::size_t(2));
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::Z), std::size_t(1));
  std::vector<sd::WireOp> gates;for(const auto& op:sd::flatten(*result.module).instructions)if(!sd::isClassical(op.kind))gates.push_back(op);
  EXPECT_EQ(gates.size(),std::size_t(4));
  if(gates.size()==4){
    EXPECT_TRUE(gates[0].kind==sd::OpKind::X);EXPECT_EQ(gates[0].qubits[0],0u);
    EXPECT_TRUE(gates[1].kind==sd::OpKind::X);EXPECT_EQ(gates[1].qubits[0],1u);
    EXPECT_TRUE(gates[2].kind==sd::OpKind::Z);EXPECT_EQ(gates[2].qubits[0],1u);
    EXPECT_TRUE(gates[3].kind==sd::OpKind::H);EXPECT_EQ(gates[3].qubits[0],0u);
  }
}

TEST(M4_lower, specialization_rejects_invalid_bindings_recursion_and_scope_leaks) {
  for (const auto* body : {
      "qubit q[2]\ndef f(int index) {\nx q[index]\n}\nf(2)\n",
      "qubit q[2]\ndef f(int index) {\nx q[index]\n}\nf(-1)\n",
      "qubit q[2]\ndef f(int index) {\nx q[index]\n}\nf(0.5)\n",
      "qubit q[1]\ndef f(int count) {\nqubit local[count]\n}\nf(0)\n",
      "qubit q[1]\ndef f(qubit a, int count) {\nx a\n}\nf(q, 1, 2)\n",
      "qubit q[2]\ndef f(qubit a, int count) {\nx a\n}\nf(q, 1)\n",
      "qubit q[1]\ndef f(qubit a, qubit b, int count) {\ncx a, b\n}\nf(q, q, 1)\n",
      "def f(int count) {\nf(count - 1)\n}\nf(2)\n",
      "qubit q[1]\nbit c[1]\nc = measure q\ndef f(int count) {\n}\nf(c)\n",
      "qubit q[1]\ndef f(qubit a, int count) {\nint local = count\nx a\n}\nf(q, 1)\nrx(local) q\n"}) {
    auto parsed = pp::parse(std::string("target generic\n") + body);
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, function_specialization_has_a_deterministic_depth_budget) {
  std::string source = "target generic\ndef leaf(int n) {\n}\n";
  for (int i = 0; i < 129; ++i) {
    source += "def f" + std::to_string(i) + "(int n) {\n";
    source += i == 0 ? "leaf(n)\n" : "f" + std::to_string(i - 1) + "(n)\n";
    source += "}\n";
  }
  source += "f128(1)\n";
  auto parsed = pp::parse(source);
  EXPECT_FALSE(parsed.module.has_value());
  EXPECT_TRUE(parsed.diag.hasErrors());
  bool bounded = false;
  for (const auto& diagnostic : parsed.diag.items())
    bounded |= diagnostic.message.find("128 nested calls") != std::string::npos;
  EXPECT_TRUE(bounded);
}

TEST(M4_lower, conditional_function_return_permutations_materialize_on_fixed_wires) {
  for (const bool specialized : {false, true}) {
    const std::string param = specialized ? ", int count" : "";
    const std::string arg = specialized ? ", 1" : "";
    auto parsed = pp::parse("target generic\n"
        "def cycle(qubit a, qubit b, qubit d" + param + ") {\n"
        "x a\nreturn b, d, a\n}\n"
        "qubit q[4]\nbit c[4]\nc[3] = measure q[3]\n"
        "if (c[3] == 0) {\ncycle(q[0], q[1], q[2]" + arg + ")\n"
        "} else {\nif (c[3] == 1) {\nz q[1]\n}\n}\n"
        "c[0] = measure q[0]\nc[1] = measure q[1]\nc[2] = measure q[2]\n");
    EXPECT_TRUE(parsed.module.has_value());
    if (!parsed.module) continue;
    auto result = pl::lower(*parsed.module);
    EXPECT_TRUE(result.module.has_value());
    if (!result.module) continue;
    const auto source = pl::emitSpinorSource(*result.module);
    EXPECT_CONTAINS(source, "if c[3] == 0 {\nx q[0]\nswap q[0], q[1]\nswap q[1], q[2]\n");
    EXPECT_CONTAINS(source, "c[0] = measure q[0]\nc[1] = measure q[1]\nc[2] = measure q[2]");
  }
}

TEST(M4_lower, unconditional_return_aliases_preserve_existing_wire_semantics) {
  auto parsed = pp::parse("target generic\ndef exchange(qubit a, qubit b) {\n"
      "return b, a\n}\nqubit q[2]\nbit c[2]\nx q[0]\nexchange(q[0], q[1])\n"
      "c[0] = measure q[0]\nc[1] = measure q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  const auto source = pl::emitSpinorSource(*result.module);
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::Swap), std::size_t(0));
  EXPECT_CONTAINS(source, "c[0] = measure q[1]\nc[1] = measure q[0]");
}

TEST(M4_lower, duplicate_returns_are_rejected_and_fresh_wire_joins_are_materialized) {
  for (const bool specialized : {false, true}) for (const bool duplicate : {false, true}) {
    const std::string param = specialized ? ", int count" : "";
    const std::string arg = specialized ? ", 1" : "";
    auto parsed = pp::parse("target generic\ndef invalid(qubit a, qubit b" + param + ") {\n" +
        (duplicate ? "return a, a\n" : "qubit fresh[1]\nreturn a, fresh\n") +
        "}\nqubit q[3]\nbit c[1]\nc = measure q[2]\n"
        "if (c == 0) {\ninvalid(q[0], q[1]" + arg + ")\n}\n");
    if (parsed.module) {
      auto result = pl::lower(*parsed.module);
      EXPECT_EQ(result.module.has_value(),!duplicate);
      if(!duplicate&&result.module)EXPECT_EQ(countOpKind(*result.module,sd::OpKind::Swap),std::size_t(1));
      if(duplicate)EXPECT_TRUE(result.diag.hasErrors());
    } else EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, runtime_declarations_cannot_hide_by_reusing_an_existing_name) {
  for (const auto* declaration : {"qubit q[1]", "bit c[1]"}) {
    auto parsed = pp::parse(std::string("target generic\nqubit q[1]\nqubit flag[1]\nbit c[1]\n") +
        "x q[0]\nc = measure flag\nif (c == 0) {\n" + declaration + "\n}\n");
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
  // The same boundary applies to callers of the programmatic logical IR API.
  for (const bool quantum : {false, true}) {
    phonon::dialect::Module source;
    phonon::dialect::Builder builder(source);
    auto q = builder.allocQubit();
    auto bit = builder.measure(q);
    auto one = builder.constInt(1);
    auto predicate = builder.cmp("==", bit, one);
    auto branch = builder.beginIf(predicate);
    if (quantum) builder.allocQubit(); else builder.allocBit();
    builder.endIf(branch);
    auto result = pl::lower(source);
    EXPECT_EQ(result.module.has_value(),quantum);
    if(!quantum)EXPECT_TRUE(result.diag.hasErrors());
  }
}

TEST(M4_lower, constant_measured_bit_predicates_preserve_surviving_wire_values) {
  for (const auto* predicate : {"c == -1", "c > 2", "c != 2", "c < 2"}) {
    auto parsed = pp::parse(std::string("target generic\nqubit q[2]\nbit c[1]\nc = measure q[0]\n") +
        "if (" + predicate + ") {\nh q[1]\n} else {\nx q[1]\n}\nz q[1]\n");
    EXPECT_TRUE(parsed.module.has_value());
    if (!parsed.module) continue;
    auto result = pl::lower(*parsed.module);
    EXPECT_TRUE(result.module.has_value());
    if (!result.module) continue;
    const auto source = pl::emitSpinorSource(*result.module);
    EXPECT_EQ(countOpKind(*result.module, sd::OpKind::If), std::size_t(0));
    EXPECT_CONTAINS(source, "z q[1]");
    const bool takeThen = std::string(predicate) == "c != 2" || std::string(predicate) == "c < 2";
    EXPECT_EQ(countOpKind(*result.module, sd::OpKind::H), std::size_t(takeThen));
    EXPECT_EQ(countOpKind(*result.module, sd::OpKind::X), std::size_t(!takeThen));
  }
}

TEST(M4_lower, measured_scalar_copies_do_not_alias_overwritten_readout_registers) {
  for (const auto* copy : {"angle saved = c[0]", "saved = c[0]"}) {
    auto parsed = pp::parse(std::string("target generic\nqubit q[2]\nbit c[2]\n") +
        "x q[0]\nc[0] = measure q[0]\n" + copy +
        "\nreset q[0]\nc[0] = measure q[0]\nif (saved == 1) {\nx q[1]\n}\n");
    EXPECT_FALSE(parsed.module.has_value());
    EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, concrete_device_names_are_quoted_and_invalid_characters_not_discarded) {
  auto parsed = pp::parse("target \"provider:device-with-hyphens/processor\"\nqubit q[1]\nx q[0]\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (parsed.module) EXPECT_EQ(parsed.module->targetAttr, std::string("provider:device-with-hyphens/processor"));
  for (const auto* source : {"target generic\nqubit q[1]\nrx(1!+2) q[0]\n",
                            "target generic\nqubit q[1]\nx @q[0]\n",
                            "target generic\nqubit q[1]\nrx(1e-) q[0]\n",
                            "target generic\nqubit q[1]\nrx(1e2e3) q[0]\n",
                            "target \"unterminated\nqubit q[1]\n"}) {
    auto invalid = pp::parse(source);
    EXPECT_FALSE(invalid.module.has_value());
    EXPECT_TRUE(invalid.diag.hasErrors());
  }
}

TEST(M4_lower, exponent_literals_keep_their_real_value_in_runtime_predicates) {
  auto parsed = pp::parse("target generic\nqubit q[2]\nbit c[1]\nc = measure q[0]\n"
      "if (c == 1e-3) {\nx q[1]\n} else {\nz q[1]\n}\n");
  EXPECT_TRUE(parsed.module.has_value());
  if (!parsed.module) return;
  auto result = pl::lower(*parsed.module);
  EXPECT_TRUE(result.module.has_value());
  if (!result.module) return;
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::If), std::size_t(0));
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::X), std::size_t(0));
  EXPECT_EQ(countOpKind(*result.module, sd::OpKind::Z), std::size_t(1));
}

TEST(M4_lower, exact_large_integer_conditions_do_not_round_through_double) {
  for(const auto* prefix:{"int a = 9007199254740993\nint b = 9007199254740992\n",
                          "int a = 9223372036854775807\nint b = 9223372036854775806\n"}){
    auto parsed=pp::parse(std::string("target generic\nqubit q[1]\n")+prefix+
        "if (a == b) {\nx q[0]\n} else {\nz q[0]\n}\n");
    EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    auto result=pl::lower(*parsed.module);EXPECT_TRUE(result.module.has_value());
    if(result.module){EXPECT_EQ(countOpKind(*result.module,sd::OpKind::X),std::size_t(0));EXPECT_EQ(countOpKind(*result.module,sd::OpKind::Z),std::size_t(1));}
  }
  auto invalid=pp::parse("target generic\nint bad = 9223372036854775807 + 1\n");
  EXPECT_FALSE(invalid.module.has_value());EXPECT_TRUE(invalid.diag.hasErrors());
}

TEST(M4_lower, programmatic_exact_integer_constants_roundtrip_and_compare) {
  phonon::dialect::Module source;source.targetAttr="generic";phonon::dialect::Builder builder(source);
  auto q=builder.allocQubit();auto a=builder.constInt(9007199254740993LL);auto b_=builder.constInt(9007199254740992LL);
  auto condition=builder.cmp("==",a,b_);auto branch=builder.beginIf(condition);builder.x(q);builder.endIf(branch);
  phonon::dialect::Diagnostics parseDiagnostics;
  auto restored=phonon::dialect::parse(phonon::dialect::print(source),parseDiagnostics);
  EXPECT_TRUE(restored.has_value());if(!restored)return;
  auto result=pl::lower(*restored);EXPECT_TRUE(result.module.has_value());
  if(result.module)EXPECT_EQ(countOpKind(*result.module,sd::OpKind::X),std::size_t(0));
}


TEST(M4_lower, controller_snapshots_uint_branch_joins_and_serializers) {
  for(int initial:{0,1}){
    const auto source=std::string("target generic\nqubit q[2]\nbit c[2]\n")+(initial?"x q[0]\n":"")+
      "c[0] = measure q[0]\nint saved = c[0]\nuint[8] value = 250\n"
      "reset q[0]\nc[0] = measure q[0]\nif (saved == 1) {\nvalue = value + 10\nx q[1]\n} else {\nvalue = value - 10\n}\n"
      "c[1] = measure q[1]\noutput value\noutput saved\n";
    auto parsed=pp::parse(source);
    if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    phonon::dialect::Diagnostics roundtripDiag;
    auto roundtrip=phonon::dialect::parse(phonon::dialect::print(*parsed.module),roundtripDiag);
    if(!roundtrip)for(const auto& d:roundtripDiag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(roundtrip.has_value());if(!roundtrip)continue;
    auto lowered=pl::lower(*roundtrip);
    if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    EXPECT_TRUE(countOpKind(*lowered.module,sd::OpKind::CSelect)>0);
    std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,32,rng);
    EXPECT_EQ(counts.size(),std::size_t(1));const auto& bits=counts.begin()->first;
    EXPECT_EQ(bits[bits.size()-1],'0');EXPECT_EQ(bits[bits.size()-2],initial?'1':'0');
    for(const auto& output:lowered.module->classicalOutputs){
      const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& v){return v.id==output.value;});
      std::uint64_t value=0;for(std::size_t bit=0;bit<info->storage.size();++bit)if(bits[bits.size()-1-info->storage[bit]]=='1')value|=std::uint64_t(1)<<bit;
      EXPECT_EQ(value,output.name=="saved"?std::uint64_t(initial):std::uint64_t(initial?4:240));
    }
    EXPECT_TRUE(spinor::emit::emitQasm3(*lowered.module).find("uint[8]")!=std::string::npos);
    EXPECT_TRUE(spinor::emit::emitQir(*lowered.module).find("add i8")!=std::string::npos);
    EXPECT_TRUE(spinor::emit::emitPhysicalJson(*lowered.module).find("classical_values")!=std::string::npos);
  }
}


TEST(M4_lower, bounded_controller_loops_report_exhaustion_without_dropping_shots) {
  for(int bound:{2,4}){
    auto parsed=pp::parse("target generic\nqubit q[1]\nbit c[1]\nuint[8] counter=0\n"
        "bounded while (counter < 3) max_iterations "+std::to_string(bound)+" {\nx q[0]\ncounter=counter+1\n}\n"
        "c=measure q\noutput counter\n");
    if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    auto lowered=pl::lower(*parsed.module);
    if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,20,rng);EXPECT_EQ(counts.size(),std::size_t(1));EXPECT_EQ(counts.begin()->second,std::size_t(20));
    const auto& bits=counts.begin()->first;
    for(const auto& output:lowered.module->classicalOutputs){
      const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& v){return v.id==output.value;});
      std::uint64_t value=0;for(std::size_t bit=0;bit<info->storage.size();++bit)if(bits[bits.size()-1-info->storage[bit]]=='1')value|=std::uint64_t(1)<<bit;
      EXPECT_EQ(value,output.role=="loop_exhausted"?std::uint64_t(bound==2):std::uint64_t(std::min(bound,3)));
    }
  }
}
TEST(M4_lower, bounded_measured_loop_joins_last_write_and_static_branch_capacity) {
  auto parsed=pp::parse("target generic\nqubit q[2]\nbit c[2]\nx q[0]\nc[0]=measure q[0]\n"
      "uint[8] count=0\nbounded while (c[0] == 1) max_iterations 3 {\n"
      "qubit ancilla[1]\nh ancilla[0]\ncx ancilla[0],q[1]\nreset q[0]\nc[0]=measure q[0]\ncount=count+1\n}\noutput count\nc[1]=measure q[1]\n");
  EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::AllocQubit),std::size_t(5));
  std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,100,rng);
  std::size_t total=0;for(const auto& [bits,shots]:counts){total+=shots;EXPECT_EQ(bits.back(),'0');}
  EXPECT_EQ(total,std::size_t(100));
}


TEST(M4_lower, bounded_break_continue_and_discard_reuse_are_device_operations) {
  auto parsed=pp::parse("target generic\nqubit q[1]\nbit c[1]\nuint[8] n=0\nuint[8] total=0\n"
      "while (n < 8) max_iterations 8 {\nn=n+1\nif (n == 2) {\ncontinue\n}\n"
      "if (n == 4) {\nbreak\n}\nqubit ancilla[1]\nh ancilla[0]\ncx ancilla[0],q[0]\ndiscard ancilla\ntotal=total+1\n}\n"
      "c=measure q\noutput n\noutput total\n");
  if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::AllocQubit),std::size_t(2));
  EXPECT_EQ(lowered.module->quantumInputs.size(),std::size_t(1));EXPECT_EQ(lowered.module->reservedPool.size(),std::size_t(1));
  std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,40,rng);
  for(const auto& [bits,shots]:counts)for(const auto& output:lowered.module->classicalOutputs){
    const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& value){return value.id==output.value;});
    std::uint64_t value=0;for(std::size_t bit=0;bit<info->storage.size();++bit)if(bits[bits.size()-1-info->storage[bit]]=='1')value|=std::uint64_t(1)<<bit;
    EXPECT_EQ(value,output.role=="loop_exhausted"?std::uint64_t(0):output.name=="n"?std::uint64_t(4):std::uint64_t(2));
  }
}
TEST(M4_lower, conditional_function_returns_materialize_and_guard_continuations) {
  for(int initial:{0,1}){
    auto parsed=pp::parse(std::string("target generic\ndef choose(qubit a, qubit b, bit flag) {\nif (flag == 1) {\nreturn b,a\n}\nx a\nreturn a,b\n}\n")+
      "qubit q[3]\nbit c[3]\nx q[0]\n"+(initial?"x q[2]\n":"")+"c[2]=measure q[2]\nchoose(q[0],q[1],c[2])\nc[0]=measure q[0]\nc[1]=measure q[1]\n");
    if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,16,rng);
    for(const auto& [bits,shots]:counts){EXPECT_EQ(bits.back(),'0');EXPECT_EQ(bits[bits.size()-2],initial?'1':'0');}
  }
}
TEST(M4_lower, bool_requires_explicit_numeric_conversion_and_preserves_finite_domain_comparisons) {
  for(const auto* source:{"bool flag=1\nflag=flag+1\n","uint[8] n=2\nbool flag=n\n"}){
    auto parsed=pp::parse(std::string("target generic\n")+source);
    if(parsed.module){const auto lowered=pl::lower(*parsed.module);EXPECT_FALSE(lowered.module.has_value());}
    else EXPECT_TRUE(parsed.diag.hasErrors());
  }
  for(const auto* expression:{"flag < 0.5","flag >= 2","-1 < flag"}){
    auto parsed=pp::parse(std::string("target generic\nqubit q[1]\nbit c[1]\nx q\nc=measure q\nbool flag=c[0]\nbool result=")+expression+"\noutput result\n");
    EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    auto lowered=pl::lower(*parsed.module);EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,4,rng);
    const auto& output=lowered.module->classicalOutputs.back();
    const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& v){return v.id==output.value;});
    EXPECT_EQ(counts.begin()->first[counts.begin()->first.size()-1-info->storage[0]],std::string(expression)=="-1 < flag"?'1':'0');
  }
}
TEST(M4_lower, static_integer_helpers_indices_and_loops_remain_exact_above_binary64) {
  const auto parsed=pp::parse("target generic\n"
    "def f(qubit a,int big) {\nif (big == 9007199254740993) {\nx a\n}\n}\n"
    "qubit q[2]\nf(q[0],9007199254740993)\nint base=9007199254740992\n"
    "x q[(base+1)-base]\nfor i in base..base+2 {\nz q[i-base]\n}\n"
    "int j=base\nwhile (j < base+2) {\nh q[j-base]\nj=j+1\n}\n"
    "rx(((base+1)-base)/4) q[0]\n"
    "for limit in 9223372036854775806..9223372036854775807 {\nx q[0]\n}\n");
  if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  const auto lowered=pl::lower(*parsed.module);EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::X),std::size_t(3));
  EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::Z),std::size_t(2));
  EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::H),std::size_t(2));
  EXPECT_CONTAINS(pl::emitSpinorSource(*lowered.module),"x q[0]\nx q[1]\nz q[0]\nz q[1]\nh q[0]\nh q[1]\nrx(0.25) q[0]\nx q[0]");
  for(const auto* invalid:{"int huge=9223372036854775808.0\n","int a=9223372036854775807\nint b=a+1\n"}){
    const auto rejected=pp::parse(std::string("target generic\n")+invalid);EXPECT_FALSE(rejected.module.has_value());EXPECT_TRUE(rejected.diag.hasErrors());
  }
}
TEST(M4_lower, helper_local_scalar_types_cannot_change_caller_widths) {
  const auto parsed=pp::parse("target generic\nqubit q[1]\nuint[16] value=511\n"
    "def f(qubit a,int unused) {\nuint[8] value=1\nx a\n}\nf(q[0],0)\nvalue=value+1\noutput value\n");
  EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  const auto lowered=pl::lower(*parsed.module);EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  const auto& output=lowered.module->classicalOutputs.back();EXPECT_EQ(output.width,16u);
  const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& value){return value.id==output.value;});
  std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,1,rng);std::uint64_t value=0;
  for(std::size_t bit=0;bit<info->storage.size();++bit)if(counts.begin()->first[counts.begin()->first.size()-1-info->storage[bit]]=='1')value|=std::uint64_t{1}<<bit;
  EXPECT_EQ(value,512u);
}
TEST(M4_lower, programmatic_bounded_builder_keeps_exact_carried_state) {
  namespace pd=phonon::dialect;pd::Module source;source.targetAttr="generic";pd::Builder b(source);
  const std::vector<pd::ValueId> initial{b.constUInt(0,8)};
  const auto result=b.boundedWhile(4,initial,[&](auto values){return b.cmp("<",values[0],b.constUInt(3,8));},[&](auto values){
    const auto next=b.binOp("+",values[0],b.constUInt(1,8));return pd::Builder::LoopStep{{next},b.cmp("==",next,b.constUInt(2,8))};
  });b.output("counter",result.values[0]);
  auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,1,rng);const auto& bits=counts.begin()->first;
  for(const auto& output:lowered.module->classicalOutputs){
    const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& v){return v.id==output.value;});
    unsigned value=0;for(unsigned bit=0;bit<info->storage.size();++bit)if(bits[bits.size()-1-info->storage[bit]]=='1')value|=1u<<bit;
    EXPECT_EQ(value,output.role=="loop_exhausted"?0u:2u);
  }
}
TEST(M4_lower, programmatic_unknown_runtime_comparison_is_rejected) {
  namespace pd=phonon::dialect;pd::Module source;source.targetAttr="generic";pd::Builder b(source);
  const auto left=b.constUInt(1,8),right=b.constUInt(2,8);
  b.output("invalid",b.cmp("unknown",left,right));
  const auto lowered=pl::lower(source);
  EXPECT_FALSE(lowered.module.has_value());EXPECT_TRUE(lowered.diag.hasErrors());
}
TEST(M4_lower, raw_conditional_return_markers_suppress_following_operations) {
  namespace pd=phonon::dialect;
  for(bool inElse:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);
    const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::bitType(),"flag"}};
    const auto function=b.beginDef("raw",params);const auto q=b.paramValue(function,0),flag=b.paramValue(function,1);
    const auto branch=b.beginIf(b.cmp("==",flag,b.constInt(1)));
    if(inElse)b.elseIf(branch);
    const std::vector<pd::ValueId> returned{q};b.returnOp(returned);b.endIf(branch);const auto changed=b.x(q);const std::vector<pd::ValueId> fallback{changed};b.returnOp(fallback);b.endDef(function);
    const auto input=b.allocQubit(),control=b.x(b.allocQubit()),measured=b.measure(control);b.constUInt(0,8);
    const std::vector<pd::ValueId> args{input,measured};const std::vector<pd::Type> results{pd::qubitType()};const auto result=b.call("raw",args,results);b.output("result",b.measure(result[0]));
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    const auto& output=lowered.module->classicalOutputs.back();const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& v){return v.id==output.value;});
    std::mt19937_64 rng(1);const auto counts=spinor::sim::sample(*lowered.module,4,rng);for(const auto& [bits,_]:counts)EXPECT_EQ(bits[bits.size()-1-info->storage[0]],inElse?'1':'0');
  }
}

TEST(M4_lower, programmatic_for_uses_exact_signed_bounds_and_checked_spans) {
  namespace pd=phonon::dialect;
  const std::vector<std::int64_t> starts{9007199254740992LL,9007199254740993LL,std::numeric_limits<std::int64_t>::max()-1,std::numeric_limits<std::int64_t>::min()};
  for(auto start:starts){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);const auto q=b.allocQubit();
    const auto loop=b.beginFor("i",b.constInt(start),b.constInt(start+1));b.x(q);b.endFor(loop);b.measure(q);
    const auto lowered=pl::lower(source);EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::X),std::size_t(1));
    std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,4,rng);EXPECT_EQ(counts.begin()->first,"1");
  }
  pd::Module source;source.targetAttr="generic";pd::Builder b(source);
  const auto loop=b.beginFor("i",b.constInt(std::numeric_limits<std::int64_t>::min()),b.constInt(std::numeric_limits<std::int64_t>::max()));b.endFor(loop);
  EXPECT_FALSE(pl::lower(source).module.has_value());
}

static std::uint64_t controllerOutput(const spinor::dialect::Module& module,const std::string& name,const std::string& bits){
  const auto output=std::find_if(module.classicalOutputs.begin(),module.classicalOutputs.end(),[&](const auto& item){return item.name==name;});
  if(output==module.classicalOutputs.end())throw std::runtime_error("missing output "+name);
  const auto value=std::find_if(module.classicalValues.begin(),module.classicalValues.end(),[&](const auto& item){return item.id==output->value;});
  std::uint64_t result=0;for(unsigned k=0;k<value->width;++k)if(bits[bits.size()-1-value->storage[k]]=='1')result|=std::uint64_t{1}<<k;return result;
}

TEST(M4_lower, builder_break_continue_commit_transfer_state_and_skip_continuation) {
  namespace pd=phonon::dialect;pd::Module source;source.targetAttr="generic";pd::Builder b(source);auto q=b.allocQubit();
  const std::vector<pd::ValueId> initial{b.constUInt(0,8),b.constUInt(0,8)};
  const auto result=b.boundedWhile(5,initial,[&](auto values){return b.cmp("<",values[0],b.constUInt(4,8));},[&](auto values){
    const auto next=b.binOp("+",values[0],b.constUInt(1,8));const std::vector<pd::ValueId> state{next,values[1]};
    auto branch=b.beginIf(b.cmp("==",next,b.constUInt(1,8)));b.continueLoop(state);b.endIf(branch);
    branch=b.beginIf(b.cmp("==",next,b.constUInt(3,8)));b.breakLoop(state);b.endIf(branch);
    q=b.x(q);return pd::Builder::LoopStep{{next,b.binOp("+",values[1],b.constUInt(1,8))},std::nullopt};
  });b.output("i",result.values[0]);b.output("total",result.values[1]);b.output("q",b.measure(q));
  pd::Diagnostics roundtripDiag;const auto roundtrip=pd::parse(pd::print(source),roundtripDiag);EXPECT_TRUE(roundtrip.has_value());
  const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,4,rng);
  if(roundtrip){const auto restored=pl::lower(*roundtrip);EXPECT_TRUE(restored.module.has_value());if(restored.module){std::mt19937_64 restoredRng(0x51C1E);EXPECT_TRUE(spinor::sim::sample(*restored.module,4,restoredRng)==counts);}}
  for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"i",bits),3u);EXPECT_EQ(controllerOutput(*lowered.module,"total",bits),1u);EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),1u);
    for(const auto& output:lowered.module->classicalOutputs)if(output.role=="loop_exhausted")EXPECT_EQ(controllerOutput(*lowered.module,output.name,bits),0u);}
}

TEST(M4_lower, builder_early_function_return_suppresses_loop_and_exhaustion_continuations) {
  namespace pd=phonon::dialect;
  for(bool flagValue:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);
    const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::bitType(),"flag"}};
    const auto function=b.beginDef("loopReturn",params);auto q=b.paramValue(function,0);const auto flag=b.paramValue(function,1);
    const std::vector<pd::ValueId> initial{b.constUInt(0,8)};
    b.boundedWhile(2,initial,[&](auto){return b.copy(b.constInt(1),pd::bitType());},[&](auto values){
      const auto branch=b.beginIf(flag);const std::vector<pd::ValueId> returned{q,b.constUInt(9,8)};b.returnOp(returned);b.endIf(branch);
      q=b.x(q);return pd::Builder::LoopStep{{b.binOp("+",values[0],b.constUInt(1,8))},std::nullopt};
    });const std::vector<pd::ValueId> fallback{q,b.constUInt(7,8)};b.returnOp(fallback);b.endDef(function);
    const auto input=b.allocQubit();const std::vector<pd::ValueId> args{input,b.copy(b.constInt(flagValue),pd::bitType())};const std::vector<pd::Type> types{pd::qubitType(),pd::uintType(8)};
    const auto result=b.call("loopReturn",args,types);b.output("value",result[1]);b.output("q",b.measure(result[0]));
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,4,rng);
    for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"value",bits),flagValue?9u:7u);EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),0u);
      for(const auto& output:lowered.module->classicalOutputs)if(output.role=="loop_exhausted")EXPECT_EQ(controllerOutput(*lowered.module,output.name,bits),flagValue?0u:1u);}
  }
}

TEST(M4_lower, typed_text_helpers_merge_classical_returns_and_preserve_widths) {
  const auto parsed=pp::parse("target generic\n"
    "def choose(bool flag,uint[64] value) -> (uint[64],bool) {\nif (flag) {\nreturn value+1,flag\n}\nreturn value,flag\n}\n"
    "def forward(bool flag,uint[64] value) -> uint[64] {\nuint[64] answer=0\nbool other=0\nanswer,other=choose(flag,value)\nreturn answer\n}\n"
    "qubit q[1]\nbit c[1]\nx q\nc=measure q\nbool saved=c[0]\nreset q\nc=measure q\n"
    "uint[64] first=forward(saved,18446744073709551615)\nuint[64] second=forward(c[0],9007199254740993)\noutput first\noutput second\n");
  if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  const auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  std::mt19937_64 rng(0x51C1E);const auto counts=spinor::sim::sample(*lowered.module,4,rng);for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"first",bits),0u);EXPECT_EQ(controllerOutput(*lowered.module,"second",bits),9007199254740993ULL);}
}

TEST(M4_lower, typed_helpers_reject_missing_returns_and_inconsistent_types) {
  for(const auto* body:{"if (flag) {\nreturn uint[8](1)\n}\n","if (flag) {\nreturn uint[8](1)\n}\nreturn uint[16](2)\n","return uint[8](1),flag\n"}){
    const auto parsed=pp::parse(std::string("target generic\ndef helper(bool flag) -> uint[8] {\n")+body+"}\nbool flag=1\nuint[8] value=helper(flag)\noutput value\n");
    if(parsed.module)EXPECT_FALSE(pl::lower(*parsed.module).module.has_value());else EXPECT_TRUE(parsed.diag.hasErrors());
  }
}

TEST(M4_lower, builder_fresh_quantum_returns_require_every_path_and_unique_ownership) {
  namespace pd=phonon::dialect;
  for(bool complete:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);
    const std::vector<pd::Builder::Param> params{{pd::bitType(),"flag"}};
    const auto function=b.beginDef("fresh",params);const auto flag=b.paramValue(function,0);
    const auto branch=b.beginIf(flag);const auto q=b.x(b.allocQubit());const std::vector<pd::ValueId> yes{q};b.returnOp(yes);
    if(complete){b.elseIf(branch);const std::vector<pd::ValueId> no{b.allocQubit()};b.returnOp(no);}b.endIf(branch);b.endDef(function);
    const std::vector<pd::ValueId> args{b.copy(b.constInt(1),pd::bitType())};const std::vector<pd::Type> types{pd::qubitType()};
    const auto result=b.call("fresh",args,types);b.output("result",b.measure(result[0]));
    const auto lowered=pl::lower(source);EXPECT_EQ(lowered.module.has_value(),complete);if(!lowered.module)continue;
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);for(const auto& [bits,_]:counts)EXPECT_EQ(controllerOutput(*lowered.module,"result",bits),1u);
  }
  pd::Module source;source.targetAttr="generic";pd::Builder b(source);const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::bitType(),"flag"}};
  const std::vector<pd::Type> types{pd::qubitType(),pd::qubitType()};const auto f=b.beginTypedDef("clone",params,types);
  const auto q=b.paramValue(f,0);const std::vector<pd::ValueId> duplicate{q,q};b.returnOp(duplicate);b.endDef(f);
  const std::vector<pd::ValueId> args{b.allocQubit(),b.copy(b.constInt(1),pd::bitType())};b.call("clone",args,types);EXPECT_FALSE(pl::lower(source).module.has_value());
}

TEST(M4_lower, builder_predicates_stop_after_break_and_first_false_result) {
  namespace pd=phonon::dialect;
  for(bool breaking:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);auto q=b.allocQubit();const std::vector<pd::ValueId> initial{b.constUInt(0,8)};
    const auto loop=b.boundedWhile(3,initial,[&](auto){q=b.x(q);return b.copy(b.constInt(breaking),pd::bitType());},[&](auto values){
      b.breakLoop(values);return pd::Builder::LoopStep{{values[0]},std::nullopt};
    });b.output("exhausted",loop.exhausted);b.output("q",b.measure(q));
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),1u);EXPECT_EQ(controllerOutput(*lowered.module,"exhausted",bits),0u);}
  }
}

TEST(M4_lower, helper_int_results_keep_exact_static_values_and_measured_snapshots) {
  const auto parsed=pp::parse("target generic\ndef identity(int n) -> int {\nreturn n\n}\n"
    "qubit q[1]\nbit c[1]\nx q\nc=measure q\nint saved=c[0]\n"
    "int old=identity(saved)\nint wide=identity(9007199254740993)\n"
    "uint[64] exact=uint[64](wide)\noutput exact\noutput old\n");
  if(!parsed.module)for(const auto& d:parsed.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)return;
  const auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"exact",bits),9007199254740993ULL);EXPECT_EQ(controllerOutput(*lowered.module,"old",bits),1u);}
}

TEST(M4_lower, bit_domain_comparisons_use_exact_int64_constants_in_both_orders) {
  for(bool controller:{false,true})for(int bit:{0,1})for(const auto bound:{std::numeric_limits<std::int64_t>::min(),std::numeric_limits<std::int64_t>::max()})for(bool reverse:{false,true})for(const std::string comparison:{"==","!=","<","<=",">",">="}){
    const std::int64_t left=reverse?bound:bit,right=reverse?bit:bound;
    const bool expected=comparison=="=="?left==right:comparison=="!="?left!=right:comparison=="<"?left<right:comparison=="<="?left<=right:comparison==">"?left>right:left>=right;
    const auto bitName=controller?"saved":"c[0]";
    const auto expression=reverse?std::to_string(bound)+comparison+bitName:std::string(bitName)+comparison+std::to_string(bound);
    const auto source=std::string("target generic\nqubit q[2]\nbit c[2]\n")+(bit?"x q[0]\n":"")+"c[0]=measure q[0]\n"+(controller?"bool saved=c[0]\n":"")+"if ("+expression+") {\nx q[1]\n}\nc[1]=measure q[1]\n";
    const auto parsed=pp::parse(source);EXPECT_TRUE(parsed.module.has_value());if(!parsed.module)continue;
    const auto lowered=pl::lower(*parsed.module);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,1,rng);const auto& bits=counts.begin()->first;EXPECT_EQ(bits[bits.size()-2],expected?'1':'0');
  }
}

TEST(M4_lower, nested_builder_transfers_and_helper_returns_keep_their_own_scope) {
  namespace pd=phonon::dialect;pd::Module source;source.targetAttr="generic";pd::Builder b(source);
  const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"}};const auto helper=b.beginDef("flip",params);
  const std::vector<pd::ValueId> returned{b.x(b.paramValue(helper,0))};b.returnOp(returned);b.endDef(helper);
  auto q=b.allocQubit();const std::vector<pd::ValueId> initial{b.constUInt(0,8),b.constUInt(0,8)};
  const auto outer=b.boundedWhile(2,initial,[&](auto values){return b.cmp("<",values[0],b.constUInt(2,8));},[&](auto values){
    const std::vector<pd::ValueId> innerInitial{b.constUInt(0,8)};
    b.boundedWhile(3,innerInitial,[&](auto){return b.copy(b.constInt(1),pd::bitType());},[&](auto inner){
      const auto next=b.binOp("+",inner[0],b.constUInt(1,8));const auto branch=b.beginIf(b.cmp("==",next,b.constUInt(1,8)));
      const std::vector<pd::ValueId> state{next};b.breakLoop(state);b.endIf(branch);q=b.x(q);return pd::Builder::LoopStep{{next},std::nullopt};
    });
    const std::vector<pd::ValueId> args{q};const std::vector<pd::Type> resultTypes{pd::qubitType()};q=b.call("flip",args,resultTypes)[0];
    const auto next=b.binOp("+",values[0],b.constUInt(1,8));const auto branch=b.beginIf(b.cmp("==",next,b.constUInt(1,8)));
    const std::vector<pd::ValueId> state{next,values[1]};b.continueLoop(state);b.endIf(branch);q=b.x(q);
    return pd::Builder::LoopStep{{next,b.binOp("+",values[1],b.constUInt(1,8))},std::nullopt};
  });b.output("i",outer.values[0]);b.output("count",outer.values[1]);b.output("q",b.measure(q));
  const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);
  for(const auto& [bits,_]:counts){EXPECT_EQ(controllerOutput(*lowered.module,"i",bits),2u);EXPECT_EQ(controllerOutput(*lowered.module,"count",bits),1u);EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),1u);
    for(const auto& output:lowered.module->classicalOutputs)if(output.role=="loop_exhausted")EXPECT_EQ(controllerOutput(*lowered.module,output.name,bits),0u);}
}

TEST(M4_lower, zero_trip_builder_for_forwards_existing_quantum_identity_without_allocating) {
  namespace pd=phonon::dialect;
  for(const auto bound:{std::int64_t{0},std::numeric_limits<std::int64_t>::max()}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);auto q=b.allocQubit();
    const auto loop=b.beginFor("i",b.constInt(bound),b.constInt(bound));q=b.x(q);const auto unused=b.h(b.allocQubit());b.measure(unused);b.endFor(loop);b.output("q",b.measure(q));
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::AllocQubit),std::size_t(1));EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::X),std::size_t(0));
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);for(const auto& [bits,_]:counts)EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),0u);
  }
}

TEST(M4_lower, terminated_function_paths_do_not_reserve_dead_quantum_capacity) {
  namespace pd=phonon::dialect;
  for(bool conditional:{false,true})for(bool flagValue:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);
    const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::bitType(),"flag"}};const std::vector<pd::Type> types{pd::qubitType()};
    const auto function=b.beginTypedDef("returnBeforeAllocation",params,types);const auto q=b.paramValue(function,0),flag=b.paramValue(function,1);
    const auto branch=conditional?std::optional<pd::OpId>{b.beginIf(flag)}:std::nullopt;
    const std::vector<pd::ValueId> returned{q};b.returnOp(returned);auto dead=b.allocQubit();dead=b.x(dead);b.measure(dead);
    if(branch){b.elseIf(*branch);const std::vector<pd::ValueId> fallback{b.x(q)};b.returnOp(fallback);b.endIf(*branch);}b.endDef(function);
    const std::vector<pd::ValueId> args{b.allocQubit(),b.copy(b.constInt(flagValue?1:0),pd::bitType())};const auto result=b.call("returnBeforeAllocation",args,types);b.output("q",b.measure(result[0]));
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    EXPECT_EQ(countOpKind(*lowered.module,sd::OpKind::AllocQubit),std::size_t(1));
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);for(const auto& [bits,_]:counts)EXPECT_EQ(controllerOutput(*lowered.module,"q",bits),conditional&&!flagValue?1u:0u);
  }
}
TEST(M4_lower, programmatic_calls_bind_immutable_bits_and_restore_callee_state) {
  namespace pd=phonon::dialect;
  for(bool overwrite:{false,true}){
    pd::Module source;source.targetAttr="generic";pd::Builder b(source);
    const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::bitType(),"flag"}};
    const auto function=b.beginDef("conditional_x",params);const auto q=b.paramValue(function,0),flag=b.paramValue(function,1);
    const auto branch=b.beginIf(b.cmp("==",flag,b.constInt(1)));b.x(q);b.endIf(branch);b.endDef(function);
    auto control=b.x(b.allocQubit());auto target=b.allocQubit();const auto first=b.measure(control);
    source.opMut(source.producerOf(first)).attributes.push_back({"clbit",0.0});
    control=b.reset(control);const auto second=b.measure(control);
    source.opMut(source.producerOf(second)).attributes.push_back({"clbit",overwrite?0.0:1.0});
    b.constUInt(0,8);const std::vector<pd::Type> resultTypes{pd::qubitType()};
    auto call=[&](pd::ValueId argument){const std::vector<pd::ValueId> args{target,argument};target=b.call("conditional_x",args,resultTypes)[0];};
    call(first);if(!overwrite)call(second);
    const auto measured=b.measure(target);source.opMut(source.producerOf(measured)).attributes.push_back({"clbit",2.0});b.output("result",measured);
    const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
    EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)continue;
    std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,8,rng);
    for(const auto& [bits,shots]:counts)EXPECT_EQ(bits[bits.size()-1-2],'1');
  }
}
TEST(M4_lower, repeated_function_loop_exhaustion_is_cumulative) {
  namespace pd=phonon::dialect;pd::Module source;source.targetAttr="generic";pd::Builder b(source);
  const std::vector<pd::Builder::Param> params{{pd::qubitType(),"q"},{pd::intType(),"limit"}};
  const auto function=b.beginDef("loop",params);const auto limit=b.paramValue(function,1);
  const std::vector<pd::ValueId> initial{b.constUInt(0,8)};
  b.boundedWhile(2,initial,[&](auto values){return b.cmp("<",values[0],b.copy(limit,pd::uintType(8)));},
      [&](auto values){return pd::Builder::LoopStep{{b.binOp("+",values[0],b.constUInt(1,8))},std::nullopt};});
  b.endDef(function);auto q=b.allocQubit();const std::vector<pd::Type> resultTypes{pd::qubitType()};
  for(auto maximum:{3,1}){const std::vector<pd::ValueId> args{q,b.constInt(maximum)};q=b.call("loop",args,resultTypes)[0];}
  const auto lowered=pl::lower(source);if(!lowered.module)for(const auto& d:lowered.diag.items())std::cerr<<d.message<<'\n';
  EXPECT_TRUE(lowered.module.has_value());if(!lowered.module)return;
  EXPECT_EQ(lowered.module->classicalOutputs.size(),std::size_t(1));const auto& output=lowered.module->classicalOutputs[0];
  const auto info=std::find_if(lowered.module->classicalValues.begin(),lowered.module->classicalValues.end(),[&](const auto& value){return value.id==output.value;});
  std::mt19937_64 rng(123);const auto counts=spinor::sim::sample(*lowered.module,2,rng);
  EXPECT_EQ(counts.begin()->first[counts.begin()->first.size()-1-info->storage[0]],'1');
}

SPINOR_TEST_MAIN()

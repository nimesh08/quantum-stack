// phonon/tests/m4/lower_test.cpp
//
// Phonon → Spinor lowering: structural and pipeline tests.

#include "phonon/lower/Lowering.h"
#include "phonon/parser/Parser.h"
#include "spinor/dialect/Spinor.h"
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
  EXPECT_CONTAINS(source, "x q[2]\nrx(0.25) q[0]\nc[2] = measure q[0]");
  EXPECT_CONTAINS(source, "x q[3]\nx q[4]\nrx(0.5) q[1]\nc[0] = measure q[1]");
  EXPECT_CONTAINS(source, "if c[2] == 1 {");
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
  EXPECT_CONTAINS(source, "x q[0]\nx q[1]\nz q[1]\nh q[0]");
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
      "qubit q[1]\ndef f(qubit a, int count) {\nint local = count\nx a\n}\nf(q, 1)\nrx(local) q\n",
      "qubit q[1]\nbit c[1]\nc = measure q\n"
      "def f(qubit a, bit flag, int n) {\nif (flag == 1) {\nreturn a\n}\n}\nf(q, c, 1)\n"}) {
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

SPINOR_TEST_MAIN()

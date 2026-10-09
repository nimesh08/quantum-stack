// spinor/dialect/lib/Print.cpp
//
// Textual format printer for the Spinor dialect. Round-trippable
// (paired with Parse.cpp). MLIR-style.

#include "spinor/dialect/Spinor.h"

#include <cassert>
#include <charconv>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace spinor::dialect {

namespace {

// Round-trippable double printing. std::to_chars with no precision
// argument produces the shortest representation that round-trips.
std::string formatDouble(double d) {
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf), d);
  return std::string(buf, res.ptr);
}

void printAttr(std::ostream& os, const Attribute& a) {
  os << a.name << " = ";
  if (std::holds_alternative<double>(a.value)) {
    os << formatDouble(std::get<double>(a.value));
  } else {
    os << '"' << std::get<std::string>(a.value) << '"';
  }
}

void printAttrs(std::ostream& os, std::span<const Attribute> attrs) {
  if (attrs.empty()) return;
  os << " {";
  for (std::size_t i = 0; i < attrs.size(); ++i) {
    if (i) os << ", ";
    printAttr(os, attrs[i]);
  }
  os << "}";
}

void printOpHeader(std::ostream& os, const Module& m, OpId id) {
  const Op& op = m.op(id);
  // results
  if (!op.results.empty()) {
    for (std::size_t i = 0; i < op.results.size(); ++i) {
      if (i) os << ", ";
      os << m.nameOf(op.results[i]);
    }
    os << " = ";
  }
  // mnemonic
  os << opMnemonic(op.kind);
  // operands
  if (!op.operands.empty()) {
    os << " ";
    for (std::size_t i = 0; i < op.operands.size(); ++i) {
      if (i) os << ", ";
      os << m.nameOf(op.operands[i]);
    }
  }
  // attributes
  printAttrs(os, op.attributes);
  // result type list
  if (!op.results.empty()) {
    os << " : ";
    for (std::size_t i = 0; i < op.results.size(); ++i) {
      if (i) os << ", ";
      os << typeName(m.typeOf(op.results[i]));
    }
  }
}

}  // namespace

std::string print(const Module& m) {
  std::ostringstream os;
  os << "spinor.module @" << m.name << " attributes {target = \"" << m.targetAttr << "\"";
  std::size_t declaredBits = 0;
  for (const auto& op : m.ops()) if (op.kind == OpKind::AllocBit) ++declaredBits;
  if (m.numClbits > declaredBits) os << ", num_clbits = " << m.numClbits;
  if (m.globalPhase != 0.0) os << ", global_phase = " << formatDouble(m.globalPhase);
  if (!m.finalLayout.empty()) {
    os << ", final_layout = \"";
    for (std::size_t i=0;i<m.finalLayout.size();++i) {
      if(i) os << ',';
      os << m.finalLayout[i];
    }
    os << '\"';
  }
  if (!m.initialLayout.empty()) {
    os << ", initial_layout = \"";
    for (std::size_t i=0;i<m.initialLayout.size();++i) {
      if(i) os << ',';
      os << m.initialLayout[i];
    }
    os << '\"';
  }
  if (!m.resonatorQubits.empty()) {
    os << ", resonator_qubits = \"";
    for (std::size_t i=0;i<m.resonatorQubits.size();++i) {
      if(i) os << ',';
      os << m.resonatorQubits[i];
    }
    os << '\"';
  }
  os << "} {\n";
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    OpId id{i};
    os << "  ";
    printOpHeader(os, m, id);
    os << "\n";
  }
  os << "}\n";
  return os.str();
}

}  // namespace spinor::dialect

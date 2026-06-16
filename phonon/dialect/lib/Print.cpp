// phonon/dialect/lib/Print.cpp
//
// Textual printer for the Phonon dialect. Round-trippable.
// MLIR-style; deliberately a strict superset of the Spinor format.

#include "phonon/dialect/Phonon.h"

#include <charconv>
#include <ostream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace phonon::dialect {

namespace {

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

void printOp(std::ostream& os, const Module& m, OpId id) {
  const Op& op = m.op(id);
  if (!op.results.empty()) {
    for (std::size_t i = 0; i < op.results.size(); ++i) {
      if (i) os << ", ";
      os << m.nameOf(op.results[i]);
    }
    os << " = ";
  }
  os << opMnemonic(op.kind);
  if (!op.operands.empty()) {
    os << " ";
    for (std::size_t i = 0; i < op.operands.size(); ++i) {
      if (i) os << ", ";
      os << m.nameOf(op.operands[i]);
    }
  }
  printAttrs(os, op.attributes);
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
  os << "phonon.module @" << m.name
     << " attributes {target = \"" << m.targetAttr << "\"} {\n";
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    OpId id{i};
    os << "  ";
    printOp(os, m, id);
    os << "\n";
  }
  os << "}\n";
  return os.str();
}

}  // namespace phonon::dialect

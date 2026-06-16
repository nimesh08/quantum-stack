// spinor/emit/lib/Quil.cpp
//
// Quil emitter (Rigetti). Spec: arXiv:1608.03355.

#include "spinor/emit/Emitters.h"

#include <charconv>
#include <map>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

namespace spinor::emit {

namespace {

using namespace spinor::dialect;

std::string fmtDouble(double d) {
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf), d);
  return std::string(buf, res.ptr);
}

struct Lineage {
  std::vector<int> physOf;
  int nAlloc = 0;
};
Lineage buildLineage(const Module& m) {
  Lineage L;
  L.physOf.assign(m.numValues(), -1);
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      L.physOf[op.results.front().v] = L.nAlloc++;
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      L.physOf[op.results[k].v] = L.physOf[op.operands[k].v];
    }
  }
  return L;
}

double extractAngle(const Op& op) {
  for (const auto& a : op.attributes) {
    if (a.name == "angle" && std::holds_alternative<double>(a.value)) {
      return std::get<double>(a.value);
    }
  }
  return 0.0;
}

}  // namespace

std::string emitQuil(const Module& m) {
  std::ostringstream os;
  Lineage L = buildLineage(m);
  std::size_t nBits = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i)
    if (m.op(OpId{i}).kind == OpKind::AllocBit) ++nBits;
  if (nBits > 0) os << "DECLARE ro BIT[" << nBits << "]\n";

  std::size_t bitIdx = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit || op.kind == OpKind::AllocBit) continue;
    int nq = qubitArity(op.kind);
    int q0 = (op.operands.size() > 0) ? L.physOf[op.operands[0].v] : -1;
    int q1 = (op.operands.size() > 1) ? L.physOf[op.operands[1].v] : -1;
    switch (op.kind) {
      case OpKind::H:    os << "H "    << q0 << "\n"; break;
      case OpKind::X:    os << "X "    << q0 << "\n"; break;
      case OpKind::Y:    os << "Y "    << q0 << "\n"; break;
      case OpKind::Z:    os << "Z "    << q0 << "\n"; break;
      case OpKind::S:    os << "S "    << q0 << "\n"; break;
      case OpKind::T:    os << "T "    << q0 << "\n"; break;
      case OpKind::Rx:
        os << "RX(" << fmtDouble(extractAngle(op)) << ") " << q0 << "\n"; break;
      case OpKind::Ry:
        os << "RY(" << fmtDouble(extractAngle(op)) << ") " << q0 << "\n"; break;
      case OpKind::Rz:
        os << "RZ(" << fmtDouble(extractAngle(op)) << ") " << q0 << "\n"; break;
      case OpKind::Cx:   os << "CNOT " << q0 << " " << q1 << "\n"; break;
      case OpKind::Cz:   os << "CZ "   << q0 << " " << q1 << "\n"; break;
      case OpKind::Swap: os << "SWAP " << q0 << " " << q1 << "\n"; break;
      case OpKind::Reset:os << "RESET " << q0 << "\n"; break;
      case OpKind::Measure:
        os << "MEASURE " << q0 << " ro[" << bitIdx << "]\n";
        ++bitIdx;
        break;
      case OpKind::Barrier:
        os << "PRAGMA BARRIER\n"; break;
      default:
        os << "; unsupported op " << opMnemonic(op.kind) << "\n";
        if (nq <= 0) break;
        break;
    }
  }
  return os.str();
}

}  // namespace spinor::emit

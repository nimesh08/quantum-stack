// spinor/emit/lib/Qasm3.cpp

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

// Recover physical qubit index from the operand value lineage.
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

double extractAngle(const Op& op, const std::string& key = "angle") {
  for (const auto& a : op.attributes) {
    if (a.name == key && std::holds_alternative<double>(a.value)) {
      return std::get<double>(a.value);
    }
  }
  return 0.0;
}

// Output qubit reference: $N when `verbatim` else q[N].
std::string qref(int p, bool verbatim) {
  if (verbatim) return "$" + std::to_string(p);
  return "q[" + std::to_string(p) + "]";
}

}  // namespace

std::string emitQasm3(const Module& m, const registry::ChipInfo* chip,
                      EmitOptions opts) {
  std::ostringstream os;
  os << "OPENQASM 3.1;\n";
  if (!opts.braketVerbatim) {
    os << "include \"stdgates.inc\";\n";
  }

  Lineage L = buildLineage(m);
  std::size_t nQ = static_cast<std::size_t>(L.nAlloc);
  // Count bit registers (we accumulate a single flat `bit[]`).
  std::size_t nBits = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    if (m.op(OpId{i}).kind == OpKind::AllocBit) ++nBits;
  }

  if (!opts.braketVerbatim) {
    if (nQ > 0) os << "qubit[" << nQ << "] q;\n";
    if (nBits > 0) os << "bit[" << nBits << "] c;\n";
  } else {
    // Verbatim: physical-qubit mode; the runtime infers the
    // qubit mapping from `$N` references. We still need a bit
    // declaration for measurements.
    if (nBits > 0) os << "bit[" << nBits << "] c;\n";
  }

  if (opts.braketVerbatim) {
    os << "#pragma braket verbatim\n";
    os << "box {\n";
  }

  // Walk ops.
  std::size_t nextBitIdx = 0;
  std::map<std::uint32_t, std::size_t> bitIdxOf;  // value -> bit index
  std::size_t allocBitCount = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) continue;
    if (op.kind == OpKind::AllocBit) {
      bitIdxOf[op.results.front().v] = allocBitCount++;
      continue;
    }
    auto indent = opts.braketVerbatim ? "  " : "";
    int nq = qubitArity(op.kind);
    if (op.kind == OpKind::Measure) {
      int p = L.physOf[op.operands[0].v];
      // The measurement target is the latest bit value the
      // sim/parser has produced. We assume the bit reg target
      // index is the per-program ordinal of the measurement.
      os << indent << "c[" << nextBitIdx << "] = measure "
         << qref(p, opts.braketVerbatim) << ";\n";
      ++nextBitIdx;
      continue;
    }
    if (op.kind == OpKind::Reset) {
      int p = L.physOf[op.operands[0].v];
      os << indent << "reset " << qref(p, opts.braketVerbatim) << ";\n";
      continue;
    }
    if (op.kind == OpKind::Barrier) {
      os << indent << "barrier";
      for (std::size_t k = 0; k < op.operands.size(); ++k) {
        os << (k == 0 ? " " : ", ");
        os << qref(L.physOf[op.operands[k].v], opts.braketVerbatim);
      }
      os << ";\n";
      continue;
    }
    if (nq <= 0) continue;
    // Mnemonic.
    std::string mn{opMnemonic(op.kind)};
    if (mn.starts_with("spinor.")) mn = mn.substr(7);
    os << indent << mn;
    if (op.kind == OpKind::Rx || op.kind == OpKind::Ry ||
        op.kind == OpKind::Rz || op.kind == OpKind::Rzz ||
        op.kind == OpKind::Gpi || op.kind == OpKind::Gpi2) {
      os << "(" << fmtDouble(extractAngle(op)) << ")";
    } else if (op.kind == OpKind::U1q) {
      double th = 0, ph = 0;
      for (const auto& a : op.attributes) {
        if (a.name == "theta") th = std::get<double>(a.value);
        else if (a.name == "phi") ph = std::get<double>(a.value);
      }
      os << "(" << fmtDouble(th) << ", " << fmtDouble(ph) << ")";
    }
    for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
      os << (k == 0 ? " " : ", ");
      os << qref(L.physOf[op.operands[k].v], opts.braketVerbatim);
    }
    os << ";\n";
  }

  if (opts.braketVerbatim) {
    os << "}\n";
  }
  return os.str();
}

}  // namespace spinor::emit

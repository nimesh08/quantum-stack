// spinor/emit/lib/Qir.cpp
//
// QIR (LLVM-IR text) emitter. We produce the textual `.ll`
// form; converting to bitcode is a `llc` invocation away.
// The QIR Alliance spec defines the function names and metadata
// used here.

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

std::string qPtr(int idx) {
  // QIR represents qubits as `%Qubit*` constants, indexed.
  return "inttoptr (i64 " + std::to_string(idx) + " to %Qubit*)";
}
std::string rPtr(int idx) {
  return "inttoptr (i64 " + std::to_string(idx) + " to %Result*)";
}

}  // namespace

std::string emitQir(const Module& m, const registry::ChipInfo* chip) {
  std::ostringstream os;

  bool adaptive =
      chip && chip->supports.feedforward != registry::CapabilityFlags::Feedforward::None;

  os << "; Spinor → QIR (" << (adaptive ? "Adaptive" : "Base") << " profile)\n";
  os << "; target: " << m.targetAttr << "\n\n";

  os << "%Qubit  = type opaque\n";
  os << "%Result = type opaque\n\n";

  // Declarations for the QIR functions we use. The QIR Alliance
  // spec (qir-alliance/qir-spec) defines these signatures.
  os << "declare void @__quantum__qis__h__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__x__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__y__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__z__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__s__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__t__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__rx__body(double, %Qubit*)\n";
  os << "declare void @__quantum__qis__ry__body(double, %Qubit*)\n";
  os << "declare void @__quantum__qis__rz__body(double, %Qubit*)\n";
  os << "declare void @__quantum__qis__sx__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__cnot__body(%Qubit*, %Qubit*)\n";
  os << "declare void @__quantum__qis__cz__body(%Qubit*, %Qubit*)\n";
  os << "declare void @__quantum__qis__swap__body(%Qubit*, %Qubit*)\n";
  os << "declare void @__quantum__qis__ecr__body(%Qubit*, %Qubit*)\n";
  os << "declare void @__quantum__qis__rzz__body(double, %Qubit*, %Qubit*)\n";
  os << "declare void @__quantum__qis__reset__body(%Qubit*)\n";
  os << "declare void @__quantum__qis__mz__body(%Qubit*, %Result*)\n";
  os << "\n";

  Lineage L = buildLineage(m);

  // Profile metadata as required by QIR.
  os << "define void @main() #0 {\n";
  os << "entry:\n";

  // Walk ops.
  std::size_t bitIdx = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit || op.kind == OpKind::AllocBit) continue;

    auto callQ = [&](const std::string& name, int q0) {
      os << "  call void @__quantum__qis__" << name << "__body("
         << qPtr(q0) << ")\n";
    };
    auto callQa = [&](const std::string& name, double a, int q0) {
      os << "  call void @__quantum__qis__" << name << "__body(double "
         << fmtDouble(a) << ", " << qPtr(q0) << ")\n";
    };
    auto callQQ = [&](const std::string& name, int q0, int q1) {
      os << "  call void @__quantum__qis__" << name << "__body("
         << qPtr(q0) << ", " << qPtr(q1) << ")\n";
    };
    auto callAQQ = [&](const std::string& name, double a, int q0, int q1) {
      os << "  call void @__quantum__qis__" << name << "__body(double "
         << fmtDouble(a) << ", " << qPtr(q0) << ", " << qPtr(q1) << ")\n";
    };

    int q0 = (op.operands.size() > 0) ? L.physOf[op.operands[0].v] : -1;
    int q1 = (op.operands.size() > 1) ? L.physOf[op.operands[1].v] : -1;

    switch (op.kind) {
      case OpKind::H:    callQ("h", q0); break;
      case OpKind::X:    callQ("x", q0); break;
      case OpKind::Y:    callQ("y", q0); break;
      case OpKind::Z:    callQ("z", q0); break;
      case OpKind::S:    callQ("s", q0); break;
      case OpKind::T:    callQ("t", q0); break;
      case OpKind::Sx:   callQ("sx", q0); break;
      case OpKind::Rx:   callQa("rx", extractAngle(op), q0); break;
      case OpKind::Ry:   callQa("ry", extractAngle(op), q0); break;
      case OpKind::Rz:   callQa("rz", extractAngle(op), q0); break;
      case OpKind::Cx:   callQQ("cnot", q0, q1); break;
      case OpKind::Cz:   callQQ("cz", q0, q1); break;
      case OpKind::Swap: callQQ("swap", q0, q1); break;
      case OpKind::Ecr:  callQQ("ecr", q0, q1); break;
      case OpKind::Rzz:  callAQQ("rzz", extractAngle(op), q0, q1); break;
      case OpKind::Reset:callQ("reset", q0); break;
      case OpKind::Measure:
        os << "  call void @__quantum__qis__mz__body(" << qPtr(q0)
           << ", " << rPtr((int)bitIdx) << ")\n";
        ++bitIdx;
        break;
      case OpKind::Barrier:
        // QIR has no barrier; emit a comment so the round-trip
        // information isn't lost.
        os << "  ; barrier\n";
        break;
      default:
        os << "  ; unsupported op " << opMnemonic(op.kind) << "\n";
        break;
    }
  }

  os << "  ret void\n";
  os << "}\n\n";
  os << "attributes #0 = { \"entry_point\" \"qir_profiles\"=\""
     << (adaptive ? "adaptive_profile" : "base_profile")
     << "\" \"required_num_qubits\"=\"" << L.nAlloc
     << "\" \"required_num_results\"=\"" << bitIdx << "\" }\n";
  return os.str();
}

}  // namespace spinor::emit

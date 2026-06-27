// spinor/emit/lib/QiskitPython.cpp
//
// Emit a self-contained Qiskit Python program from the Spinor IR.
//
// For IBM targets, Spinor lets Qiskit's `transpile()` do the
// lowering to the chip's native basis — Qiskit's
// `Optimize1qGatesDecomposition` and `TwoQubitPeepholeOptimization`
// passes substantially outperform Spinor's scaffolded equivalents
// on deep circuits (e.g. ~5x fewer 1Q gates on Grover-3).
//
// The emitter walks the IR at its INPUT level (universal gates:
// h, x, cx, ccx, rz, rx, ry, swap, ...). Spinor's decompose +
// cleanup passes are intentionally skipped upstream when this
// emitter is selected (see spinorc_main.cpp).
//
// Output shape:
//   - imports
//   - `def build() -> QuantumCircuit:` containing the circuit
//   - `if __name__ == "__main__":` block that:
//       reads QISKIT_IBM_TOKEN / QISKIT_IBM_INSTANCE from env,
//       constructs QiskitRuntimeService(channel="ibm_cloud"),
//       transpile(..., optimization_level=N, seed_transpiler=42),
//       SamplerV2(mode=backend).run([qt], shots=N),
//       prints a single-line JSON to stdout:
//         {"histogram": {...}, "job_id": "...",
//          "depth": N, "gates": {...}}
//
// The generated program is callable in three ways:
//   1) spinorc spawns it (the `run` subcommand).
//   2) A user runs `python3 generated.py` directly.
//   3) The compiler-worker can run it inside the Qiskit venv.

#include "spinor/emit/Emitters.h"

#include <charconv>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace spinor::emit {

namespace {

using namespace spinor::dialect;

std::string fmtDouble(double d) {
  // Use repr-like precision so the Python side reads back the
  // exact angle. std::to_chars round-trips IEEE-754 doubles.
  char buf[64];
  auto res = std::to_chars(buf, buf + sizeof(buf), d);
  return std::string(buf, res.ptr);
}

// Recover physical qubit index per operand value. Same scheme as
// Qasm3.cpp uses.
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

// Translate one Spinor IR op into a Qiskit Python statement.
// Returns the formatted line WITHOUT trailing newline, or empty
// string for ops we skip (alloc, barrier, reset for now).
std::string opToPython(const Op& op, const Lineage& L,
                       int& nextClbit) {
  auto q = [&](int operand_idx) -> int {
    return L.physOf[op.operands[operand_idx].v];
  };
  std::ostringstream s;
  switch (op.kind) {
    // 1Q standard gates — same names in Qiskit.
    case OpKind::H:    s << "    qc.h("    << q(0) << ")"; break;
    case OpKind::X:    s << "    qc.x("    << q(0) << ")"; break;
    case OpKind::Y:    s << "    qc.y("    << q(0) << ")"; break;
    case OpKind::Z:    s << "    qc.z("    << q(0) << ")"; break;
    case OpKind::S:    s << "    qc.s("    << q(0) << ")"; break;
    case OpKind::Sdg:  s << "    qc.sdg("  << q(0) << ")"; break;
    case OpKind::T:    s << "    qc.t("    << q(0) << ")"; break;
    case OpKind::Tdg:  s << "    qc.tdg("  << q(0) << ")"; break;
    case OpKind::Sx:   s << "    qc.sx("   << q(0) << ")"; break;
    case OpKind::Sxdg: s << "    qc.sxdg(" << q(0) << ")"; break;
    // 1Q parameterised.
    case OpKind::Rx:   s << "    qc.rx("   << fmtDouble(extractAngle(op))
                         << ", " << q(0) << ")"; break;
    case OpKind::Ry:   s << "    qc.ry("   << fmtDouble(extractAngle(op))
                         << ", " << q(0) << ")"; break;
    case OpKind::Rz:   s << "    qc.rz("   << fmtDouble(extractAngle(op))
                         << ", " << q(0) << ")"; break;
    // 2Q standard.
    case OpKind::Cx:   s << "    qc.cx("   << q(0) << ", " << q(1) << ")"; break;
    case OpKind::Cz:   s << "    qc.cz("   << q(0) << ", " << q(1) << ")"; break;
    case OpKind::Swap: s << "    qc.swap(" << q(0) << ", " << q(1) << ")"; break;
    // 2Q native (Qiskit has these built in).
    case OpKind::Ecr:  s << "    qc.ecr("  << q(0) << ", " << q(1) << ")"; break;
    case OpKind::Rzz:  s << "    qc.rzz("  << fmtDouble(extractAngle(op))
                         << ", " << q(0) << ", " << q(1) << ")"; break;
    // Measurement: c[i] receives qubit i. The Qiskit convention is
    // qc.measure(qubit, clbit) — we assign clbits in source order.
    case OpKind::Measure: {
      int p = q(0);
      s << "    qc.measure(" << p << ", " << nextClbit << ")";
      ++nextClbit;
      break;
    }
    case OpKind::Reset:
      s << "    qc.reset(" << q(0) << ")";
      break;
    case OpKind::Barrier: {
      s << "    qc.barrier(";
      bool first = true;
      for (const auto& v : op.operands) {
        int p = L.physOf[v.v];
        if (p < 0) continue;
        if (!first) s << ", ";
        s << p;
        first = false;
      }
      s << ")";
      break;
    }
    // Allocs are handled at header time; emit nothing here.
    case OpKind::AllocQubit:
    case OpKind::AllocBit:
      return "";
    // Vendor-native single-qubit gates that don't have a Qiskit
    // core equivalent. Targeting IBM with these is a user error
    // — we emit a TODO comment so the user sees it in the file.
    case OpKind::Ms:
    case OpKind::Gpi:
    case OpKind::Gpi2:
    case OpKind::U1q:
      s << "    raise RuntimeError('unsupported non-IBM gate: "
        << opMnemonic(op.kind) << "')";
      break;
    default:
      return "";
  }
  return s.str();
}

// Count distinct qubits and classical bits used by the module.
struct Counts {
  int numQubits = 0;
  int numClbits = 0;
};

Counts countResources(const Module& m) {
  Counts c;
  int maxClbit = -1;
  int clbitIdx = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      ++c.numQubits;
    } else if (op.kind == OpKind::AllocBit) {
      // Each `bit c[N]` declaration generates N AllocBit ops.
      // Count those that get referenced via Measure later.
    } else if (op.kind == OpKind::Measure) {
      maxClbit = std::max(maxClbit, clbitIdx);
      ++clbitIdx;
    }
  }
  c.numClbits = clbitIdx > 0 ? clbitIdx : (maxClbit + 1);
  if (c.numClbits == 0) c.numClbits = c.numQubits;  // sensible default
  return c;
}

}  // namespace

std::string emitQiskitPython(const Module& m,
                             const registry::ChipInfo& chip,
                             int optimizationLevel) {
  Lineage L = buildLineage(m);
  Counts cnt = countResources(m);
  // If allocs gave us 0 qubits, fall back to nAlloc.
  if (cnt.numQubits == 0) cnt.numQubits = L.nAlloc;
  // Clamp optimization level to Qiskit's accepted range.
  if (optimizationLevel < 0) optimizationLevel = 0;
  if (optimizationLevel > 3) optimizationLevel = 3;

  std::ostringstream out;
  out << "# Generated by spinorc — emits standard Qiskit Python.\n"
      << "# target chip : " << chip.id << "\n"
      << "# provider    : " << chip.provider << "\n"
      << "# -O level    : " << optimizationLevel << "\n"
      << "#\n"
      << "# This file is the same code a normal Qiskit user would\n"
      << "# write. spinorc compiled the Spinor source down to this.\n"
      << "\n"
      << "import json\n"
      << "import os\n"
      << "import sys\n"
      << "from qiskit import QuantumCircuit, transpile\n"
      << "from qiskit_ibm_runtime import QiskitRuntimeService, SamplerV2\n"
      << "\n"
      << "TARGET_CHIP = " << '"' << chip.id << '"' << "\n"
      << "OPTIMIZATION_LEVEL = " << optimizationLevel << "\n"
      << "DEFAULT_SHOTS = int(os.environ.get(\"SPINOR_SHOTS\", \"1024\"))\n"
      << "\n"
      << "\n"
      << "def build() -> QuantumCircuit:\n"
      << "    qc = QuantumCircuit(" << cnt.numQubits << ", "
      << cnt.numClbits << ")\n";

  // Walk ops and emit one Python line each.
  int nextClbit = 0;
  bool anyGate = false;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    std::string line = opToPython(op, L, nextClbit);
    if (!line.empty()) {
      out << line << "\n";
      anyGate = true;
    }
  }
  if (!anyGate) out << "    pass\n";
  out << "    return qc\n"
      << "\n"
      << "\n"
      << "def _resolve_service() -> QiskitRuntimeService:\n"
      << "    token = os.environ.get(\"QISKIT_IBM_TOKEN\")\n"
      << "    inst = os.environ.get(\"QISKIT_IBM_INSTANCE\")\n"
      << "    if not token or not inst:\n"
      << "        print(json.dumps({\"error\": \"missing QISKIT_IBM_TOKEN or QISKIT_IBM_INSTANCE\"}))\n"
      << "        sys.exit(2)\n"
      << "    return QiskitRuntimeService(\n"
      << "        channel=\"ibm_cloud\", token=token, instance=inst,\n"
      << "    )\n"
      << "\n"
      << "\n"
      << "def main() -> int:\n"
      << "    qc = build()\n"
      << "    service = _resolve_service()\n"
      << "    backend = service.backend(TARGET_CHIP)\n"
      << "    qt = transpile(qc, backend=backend,\n"
      << "                   optimization_level=OPTIMIZATION_LEVEL,\n"
      << "                   seed_transpiler=42)\n"
      << "    sampler = SamplerV2(mode=backend)\n"
      << "    job = sampler.run([qt], shots=DEFAULT_SHOTS)\n"
      << "    result = job.result()\n"
      << "    # Pull histogram from the first classical-register name.\n"
      << "    pub = result[0]\n"
      << "    data = pub.data\n"
      << "    # Iterate the data containers to find a BitArray.\n"
      << "    counts = None\n"
      << "    for attr in dir(data):\n"
      << "        if attr.startswith(\"_\"): continue\n"
      << "        val = getattr(data, attr, None)\n"
      << "        if hasattr(val, \"get_counts\"):\n"
      << "            counts = val.get_counts()\n"
      << "            break\n"
      << "    if counts is None:\n"
      << "        counts = {}\n"
      << "    out = {\n"
      << "        \"histogram\": {str(k): int(v) for k, v in counts.items()},\n"
      << "        \"job_id\": job.job_id(),\n"
      << "        \"backend\": TARGET_CHIP,\n"
      << "        \"shots\": DEFAULT_SHOTS,\n"
      << "        \"depth\": qt.depth(),\n"
      << "        \"gates\": {k: int(v) for k, v in qt.count_ops().items()},\n"
      << "        \"optimization_level\": OPTIMIZATION_LEVEL,\n"
      << "    }\n"
      << "    print(json.dumps(out))\n"
      << "    return 0\n"
      << "\n"
      << "\n"
      << "if __name__ == \"__main__\":\n"
      << "    sys.exit(main())\n";
  return out.str();
}

}  // namespace spinor::emit

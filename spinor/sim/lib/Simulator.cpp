// spinor/sim/lib/Simulator.cpp
//
// Statevector simulator + equivalence + resource estimator.

#include "spinor/sim/Simulator.h"

#include "../../passes/lib/Complex2x2.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace spinor::sim {

namespace {

using namespace spinor::dialect;
using spinor::passes::la::Mat2;
using spinor::passes::la::Mat4;
using spinor::passes::la::cdbl;
using spinor::passes::la::Rx;
using spinor::passes::la::Ry;
using spinor::passes::la::Rz;
using spinor::passes::la::H;
using spinor::passes::la::X;
using spinor::passes::la::Y;
using spinor::passes::la::Z;
using spinor::passes::la::S;
using spinor::passes::la::Sdg;
using spinor::passes::la::T;
using spinor::passes::la::Tdg;
using spinor::passes::la::SX;
using spinor::passes::la::SXdg;
using spinor::passes::la::CX;
using spinor::passes::la::CZ;
using spinor::passes::la::SWAP;
using spinor::passes::la::ECR;
using spinor::passes::la::MS;
using spinor::passes::la::RZZ;

// Apply a 2×2 unitary to qubit `q` (0-indexed) in-place.
void apply1q(StateVector& sv, int q, const Mat2& u) {
  std::size_t n = sv.qubits;
  std::size_t mask = std::size_t(1) << q;
  for (std::size_t i = 0; i < (std::size_t(1) << n); ++i) {
    if (i & mask) continue;  // visit pairs (i, i|mask)
    std::size_t j = i | mask;
    cdbl a = sv.amps[i];
    cdbl b = sv.amps[j];
    sv.amps[i] = u(0, 0) * a + u(0, 1) * b;
    sv.amps[j] = u(1, 0) * a + u(1, 1) * b;
  }
}

// Apply a 4×4 unitary to qubits (qa, qb). Convention: bit qa is
// the higher-order qubit (operand 0 in the matrix's row index).
void apply2q(StateVector& sv, int qa, int qb, const Mat4& u) {
  std::size_t maskA = std::size_t(1) << qa;
  std::size_t maskB = std::size_t(1) << qb;
  std::size_t n = sv.qubits;
  std::size_t total = std::size_t(1) << n;
  // For each base index i with bits qa=0, qb=0, gather the four
  // amplitudes (00, 01, 10, 11) and apply u.
  for (std::size_t i = 0; i < total; ++i) {
    if (i & maskA) continue;
    if (i & maskB) continue;
    std::size_t i00 = i;
    std::size_t i01 = i | maskB;
    std::size_t i10 = i | maskA;
    std::size_t i11 = i | maskA | maskB;
    cdbl a00 = sv.amps[i00];
    cdbl a01 = sv.amps[i01];
    cdbl a10 = sv.amps[i10];
    cdbl a11 = sv.amps[i11];
    sv.amps[i00] = u(0, 0) * a00 + u(0, 1) * a01 +
                   u(0, 2) * a10 + u(0, 3) * a11;
    sv.amps[i01] = u(1, 0) * a00 + u(1, 1) * a01 +
                   u(1, 2) * a10 + u(1, 3) * a11;
    sv.amps[i10] = u(2, 0) * a00 + u(2, 1) * a01 +
                   u(2, 2) * a10 + u(2, 3) * a11;
    sv.amps[i11] = u(3, 0) * a00 + u(3, 1) * a01 +
                   u(3, 2) * a10 + u(3, 3) * a11;
  }
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

StateVector simulate(const Module& m) {
  StateVector sv;
  // Count qubits = number of alloc_qubit ops; assign each its
  // index by allocation order.
  std::map<std::uint32_t, int> qubitOf;
  int n = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      qubitOf[op.results.front().v] = n++;
    }
  }
  if (n > 24) {
    throw std::runtime_error(
        "simulate: refusing to allocate state vector for >24 qubits");
  }
  sv.qubits = static_cast<std::size_t>(n);
  sv.amps.assign(std::size_t(1) << n, cdbl(0, 0));
  if (n > 0) sv.amps[0] = cdbl(1, 0);

  // Track per-value qubit index lineage.
  std::map<std::uint32_t, int> lineage = qubitOf;

  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    int nq = qubitArity(op.kind);
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    // Early return for ops we don't simulate semantically:
    if (op.kind == OpKind::AllocQubit || op.kind == OpKind::AllocBit ||
        op.kind == OpKind::Measure || op.kind == OpKind::Barrier) {
      // Propagate lineage if applicable.
      if (op.kind == OpKind::Measure) {
        // Measure consumes the qubit; for equivalence we don't
        // collapse — treat as identity on the state vector.
      }
      continue;
    }
    if (op.kind == OpKind::Reset) {
      // Reset to |0> — model as projecting and renormalising,
      // but for equivalence checks we treat it as identity (the
      // pre-measure state captures program meaning). Future:
      // explicit reset handling. M8 baseline: skip.
      if (qResults > 0) {
        for (int k = 0; k < qResults; ++k) {
          lineage[op.results[k].v] = lineage[op.operands[k].v];
        }
      }
      continue;
    }

    // Recover physical qubit index of operand 0 (and 1 for 2q).
    int q0 = (op.operands.size() > 0) ? lineage[op.operands[0].v] : -1;
    int q1 = (op.operands.size() > 1) ? lineage[op.operands[1].v] : -1;

    switch (op.kind) {
      case OpKind::H:    apply1q(sv, q0, H()); break;
      case OpKind::X:    apply1q(sv, q0, X()); break;
      case OpKind::Y:    apply1q(sv, q0, Y()); break;
      case OpKind::Z:    apply1q(sv, q0, Z()); break;
      case OpKind::S:    apply1q(sv, q0, S()); break;
      case OpKind::Sdg:  apply1q(sv, q0, Sdg()); break;
      case OpKind::T:    apply1q(sv, q0, T()); break;
      case OpKind::Tdg:  apply1q(sv, q0, Tdg()); break;
      case OpKind::Rx:   apply1q(sv, q0, Rx(extractAngle(op))); break;
      case OpKind::Ry:   apply1q(sv, q0, Ry(extractAngle(op))); break;
      case OpKind::Rz:   apply1q(sv, q0, Rz(extractAngle(op))); break;
      case OpKind::Sx:   apply1q(sv, q0, SX()); break;
      case OpKind::Sxdg: apply1q(sv, q0, SXdg()); break;
      case OpKind::Cx:   apply2q(sv, q0, q1, CX()); break;
      case OpKind::Cz:   apply2q(sv, q0, q1, CZ()); break;
      case OpKind::Swap: apply2q(sv, q0, q1, SWAP()); break;
      case OpKind::Ecr:  apply2q(sv, q0, q1, ECR()); break;
      case OpKind::Ms:   apply2q(sv, q0, q1, MS()); break;
      case OpKind::Rzz:  apply2q(sv, q0, q1, RZZ(extractAngle(op))); break;
      // Native 1q gates we don't model exactly; fall through to
      // identity (these only appear in chip-specific paths and
      // M8's baseline equivalence is gated to standard gates).
      case OpKind::Gpi:
      case OpKind::Gpi2:
      case OpKind::U1q:
        // Treat as identity for the baseline check lane.
        break;
      default:
        break;
    }

    // Propagate lineage.
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      lineage[op.results[k].v] = lineage[op.operands[k].v];
    }
  }
  return sv;
}

EquivResult equivalent(const Module& a, const Module& b, double tol) {
  EquivResult r;
  StateVector sa = simulate(a);
  StateVector sb = simulate(b);
  if (sa.qubits != sb.qubits) {
    r.equivalent = false;
    r.maxAbsDiff = std::numeric_limits<double>::infinity();
    return r;
  }
  // Find phase factor: pick any non-zero amp in sa and divide.
  cdbl phase{1, 0};
  for (std::size_t i = 0; i < sa.amps.size(); ++i) {
    if (std::abs(sb.amps[i]) > 1e-7) {
      phase = sa.amps[i] / sb.amps[i];
      break;
    }
  }
  if (std::abs(std::abs(phase) - 1.0) > tol) {
    r.equivalent = false;
    r.maxAbsDiff = std::abs(std::abs(phase) - 1.0);
    return r;
  }
  r.phase = phase;
  double m = 0.0;
  for (std::size_t i = 0; i < sa.amps.size(); ++i) {
    m = std::max(m, std::abs(sa.amps[i] - phase * sb.amps[i]));
  }
  r.maxAbsDiff = m;
  r.equivalent = m <= tol;
  return r;
}

ResourceEstimate estimate(const Module& m, const registry::ChipInfo* chip,
                          std::size_t shots) {
  ResourceEstimate r;
  // Per-physical-qubit "depth" tracker.
  std::map<int, std::size_t> depthAt;
  std::size_t allocCount = 0;
  std::map<std::uint32_t, int> lineage;

  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      lineage[op.results.front().v] = static_cast<int>(allocCount++);
      continue;
    }
    if (op.kind == OpKind::AllocBit) continue;
    if (op.kind == OpKind::Measure) {
      ++r.measurements;
      continue;
    }
    if (op.kind == OpKind::Reset || op.kind == OpKind::Barrier) {
      // Don't count, but propagate lineage for Reset.
      int nq = qubitArity(op.kind);
      for (int k = 0; k < nq && k < (int)op.results.size(); ++k) {
        lineage[op.results[k].v] = lineage[op.operands[k].v];
      }
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    ++r.totalGates;
    if (nq == 2) ++r.twoQubitGates;

    // Depth: 1 + max(prev depth on each touched qubit).
    std::size_t prev = 0;
    for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
      int q = lineage[op.operands[k].v];
      auto it = depthAt.find(q);
      if (it != depthAt.end()) prev = std::max(prev, it->second);
    }
    std::size_t newDepth = prev + 1;
    for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
      int q = lineage[op.operands[k].v];
      depthAt[q] = newDepth;
      // Propagate lineage to results.
      if (k < (int)op.results.size()) {
        lineage[op.results[k].v] = q;
      }
    }
    if (newDepth > r.depth) r.depth = newDepth;
  }
  r.qubits = allocCount;

  if (chip) {
    // Pessimistic per-2q-gate error of 1% on unknown chips; per-1q
    // 0.1%. Calibration-driven fidelities will refine this in M8+.
    double e2q = 0.01;
    double e1q = 0.001;
    double pNoErr = 1.0;
    pNoErr *= std::pow(1.0 - e2q, static_cast<double>(r.twoQubitGates));
    pNoErr *=
        std::pow(1.0 - e1q, static_cast<double>(r.totalGates - r.twoQubitGates));
    r.totalErrorEstimate = 1.0 - pNoErr;
    r.shotCostUsd = chip->pricePerShotUsd * static_cast<double>(shots);
  }
  return r;
}

}  // namespace spinor::sim

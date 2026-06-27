// spinor/passes/lib/Cleanup.cpp
//
// Local post-decomposition peephole. The only optimization in
// Spinor (Rule 2) — exists because these specific redundancies
// only become visible once gates are in native form. Spec
// docs/build/phaseA/M6_decomposition.md §5.

#include "spinor/passes/Cleanup.h"

#include <cmath>
#include <map>
#include <unordered_map>
#include <variant>

namespace spinor::passes {

using namespace spinor::dialect;

namespace {

// Recover the per-physical-qubit "lineage" by walking allocs in
// order: the k-th alloc_qubit corresponds to physical qubit k.
struct Lineage {
  std::vector<int> physOfValue;  // -1 if not a qubit value
  int allocCount = 0;
};

Lineage buildLineage(const Module& m) {
  Lineage L;
  L.physOfValue.assign(m.numValues(), -1);
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      L.physOfValue[op.results.front().v] = L.allocCount++;
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      L.physOfValue[op.results[k].v] = L.physOfValue[op.operands[k].v];
    }
  }
  return L;
}

double rotAttr(const Op& op) {
  for (const auto& a : op.attributes) {
    if (a.name == "angle" && std::holds_alternative<double>(a.value)) {
      return std::get<double>(a.value);
    }
  }
  return 0.0;
}

double normalizeAngle(double a) {
  // Normalise to (-π, π].
  while (a >  M_PI) a -= 2 * M_PI;
  while (a <= -M_PI) a += 2 * M_PI;
  return a;
}

bool isFenceOp(OpKind k) {
  return k == OpKind::Measure || k == OpKind::Reset ||
         k == OpKind::Barrier;
}

bool isOneQRotZ(OpKind k) {
  return k == OpKind::Rz || k == OpKind::Gpi || k == OpKind::U1q;
}

}  // namespace

Module Cleanup::run(const Module& in) const {
  Module out;
  out.name = in.name;
  out.targetAttr = in.targetAttr;
  Builder b(out);

  Lineage L = buildLineage(in);
  std::vector<ValueId> live;
  std::map<int, int> gen;
  std::vector<int> outPhysCount;

  // First, allocate qubits in the same order as the input.
  std::vector<ValueId> allocVals;
  for (uint32_t i = 0; i < in.numOps(); ++i) {
    if (in.op(OpId{i}).kind == OpKind::AllocQubit) {
      ValueId v = b.allocQubit();
      out.setName(v, "q" + std::to_string(allocVals.size()));
      gen[(int)allocVals.size()] = 0;
      allocVals.push_back(v);
      live.push_back(v);
    }
  }

  // Pending state per physical qubit:
  //   pending_rz[p]: accumulated Rz angle waiting to be flushed.
  //   pending_pi2[p]: 0/1/-1 — count of consecutive sx/sxdg ops
  //   (positive = sx, negative = sxdg). They annihilate when
  //   they cancel.
  std::map<int, double> pendingRz;
  std::map<int, int> pendingPi2;  // > 0 means sx, < 0 means sxdg

  auto flushPi2 = [&](int p) {
    int n = pendingPi2[p];
    while (n > 0) {
      ValueId v = b.sx(live[p]);
      ++gen[p];
      out.setName(v, "q" + std::to_string(p) + "_" +
                     std::to_string(gen[p]));
      live[p] = v;
      --n;
    }
    while (n < 0) {
      ValueId v = b.sxdg(live[p]);
      ++gen[p];
      out.setName(v, "q" + std::to_string(p) + "_" +
                     std::to_string(gen[p]));
      live[p] = v;
      ++n;
    }
    pendingPi2[p] = 0;
  };
  auto flushRz = [&](int p) {
    double a = pendingRz[p];
    if (std::abs(normalizeAngle(a)) > 1e-15) {
      ValueId v = b.rz(normalizeAngle(a), live[p]);
      ++gen[p];
      out.setName(v, "q" + std::to_string(p) + "_" +
                     std::to_string(gen[p]));
      live[p] = v;
    }
    pendingRz[p] = 0.0;
  };
  auto flushAll = [&](int p) {
    flushPi2(p);
    flushRz(p);
  };

  // Walk ops in input order.
  for (uint32_t i = 0; i < in.numOps(); ++i) {
    const Op& op = in.op(OpId{i});
    if (op.kind == OpKind::AllocQubit || op.kind == OpKind::AllocBit) {
      // alloc_bit later; we re-emit fresh bits as `measure` runs.
      continue;
    }
    int nq = qubitArity(op.kind);
    if (op.kind == OpKind::Rz && !op.operands.empty()) {
      int p = L.physOfValue[op.operands[0].v];
      // If there's a pending Pi2 run, the new Rz comes AFTER it
      // in source order — flush the Pi2 first so the Rz lands in
      // the correct sequence. (Without this flush, the Rz would
      // commute backward across the Sxs, which is NOT a valid
      // commutation in general.)
      if (pendingPi2[p] != 0) flushPi2(p);
      pendingRz[p] += rotAttr(op);
      continue;
    }
    if (op.kind == OpKind::Sx && !op.operands.empty()) {
      int p = L.physOfValue[op.operands[0].v];
      // Flush pending Rz BEFORE the Sx (Rz·Sx is not Sx·Rz).
      flushRz(p);
      pendingPi2[p] += 1;
      // Annihilate if combined with prior sxdg.
      continue;
    }
    if (op.kind == OpKind::Sxdg && !op.operands.empty()) {
      int p = L.physOfValue[op.operands[0].v];
      flushRz(p);
      pendingPi2[p] -= 1;
      continue;
    }

    // For any other op, flush all pending rotations on the
    // qubits it touches before emitting it.
    if (op.kind == OpKind::Measure || op.kind == OpKind::Reset ||
        op.kind == OpKind::Barrier || nq >= 1) {
      // Determine which physical positions are touched.
      std::vector<int> touched;
      if (op.kind == OpKind::Barrier) {
        for (auto v : op.operands) {
          int p = L.physOfValue[v.v];
          if (p >= 0) touched.push_back(p);
        }
      } else if (nq > 0) {
        for (int k = 0; k < nq && k < (int)op.operands.size(); ++k) {
          int p = L.physOfValue[op.operands[k].v];
          if (p >= 0) touched.push_back(p);
        }
      }
      for (int p : touched) flushAll(p);
    }

    // Re-emit the op in the output, threading current live values.
    if (op.kind == OpKind::Measure) {
      int p = L.physOfValue[op.operands[0].v];
      ValueId bit = b.measure(live[p]);
      out.setName(bit, "c" + std::to_string(p));
      continue;
    }
    if (op.kind == OpKind::Reset) {
      int p = L.physOfValue[op.operands[0].v];
      ValueId nv = b.reset(live[p]);
      ++gen[p];
      out.setName(nv, "q" + std::to_string(p) + "_" +
                      std::to_string(gen[p]));
      live[p] = nv;
      continue;
    }
    if (op.kind == OpKind::Barrier) {
      std::vector<ValueId> qs;
      for (auto v : op.operands) {
        int p = L.physOfValue[v.v];
        qs.push_back(live[p]);
      }
      b.barrier(qs);
      continue;
    }
    // Pass-through 1q (e.g. gpi/gpi2/u1q which we don't merge yet).
    if (nq == 1 && !op.operands.empty()) {
      int p = L.physOfValue[op.operands[0].v];
      double angle = rotAttr(op);
      ValueId v;
      switch (op.kind) {
        case OpKind::X:    v = b.x(live[p]); break;
        case OpKind::Y:    v = b.y(live[p]); break;
        case OpKind::Z:    v = b.z(live[p]); break;
        case OpKind::S:    v = b.s(live[p]); break;
        case OpKind::Sdg:  v = b.sdg(live[p]); break;
        case OpKind::T:    v = b.t(live[p]); break;
        case OpKind::Tdg:  v = b.tdg(live[p]); break;
        case OpKind::Rx:   v = b.rx(angle, live[p]); break;
        case OpKind::Ry:   v = b.ry(angle, live[p]); break;
        case OpKind::H:    v = b.h(live[p]); break;
        case OpKind::Gpi:  v = b.gpi(angle, live[p]); break;
        case OpKind::Gpi2: v = b.gpi2(angle, live[p]); break;
        case OpKind::U1q:  v = b.u1q(angle, 0.0, live[p]); break;
        default: continue;
      }
      ++gen[p];
      out.setName(v, "q" + std::to_string(p) + "_" +
                     std::to_string(gen[p]));
      live[p] = v;
      continue;
    }
    // 2q gates.
    if (nq == 2 && op.operands.size() >= 2) {
      int pa = L.physOfValue[op.operands[0].v];
      int pb = L.physOfValue[op.operands[1].v];
      double angle = rotAttr(op);
      std::pair<ValueId, ValueId> r;
      switch (op.kind) {
        case OpKind::Cx:   r = b.cx(live[pa], live[pb]); break;
        case OpKind::Cz:   r = b.cz(live[pa], live[pb]); break;
        case OpKind::Swap: r = b.swap(live[pa], live[pb]); break;
        case OpKind::Ecr:  r = b.ecr(live[pa], live[pb]); break;
        case OpKind::Ms:   r = b.ms(live[pa], live[pb]); break;
        case OpKind::Rzz:  r = b.rzz(angle, live[pa], live[pb]); break;
        default: continue;
      }
      ++gen[pa]; ++gen[pb];
      out.setName(r.first, "q" + std::to_string(pa) + "_" +
                           std::to_string(gen[pa]));
      out.setName(r.second, "q" + std::to_string(pb) + "_" +
                            std::to_string(gen[pb]));
      live[pa] = r.first;
      live[pb] = r.second;
      continue;
    }
  }
  // Flush any remaining pending rotations.
  for (int p = 0; p < (int)live.size(); ++p) flushAll(p);
  return out;
}

// Vendor-aware overload. For Phase A we delegate to the
// IBM-style {rz, sx, sxdg} peephole when the chip uses rz+sx
// (i.e. IBM-family). For IonQ (gpi+gpi2), AQT (gpi+gpi2), and
// Quantinuum (u1q+""), there is no merger today and the pass
// passes ops through unchanged — same behavior as before this
// signature change.
Module Cleanup::run(const Module& in,
                    const registry::ChipInfo& chip) const {
  // The chip-id-agnostic path. The current peephole only
  // matches {Rz, Sx, Sxdg} — that subset is reached only by
  // chips whose decomposition emits those ops. Other 1Q natives
  // (Gpi, Gpi2, U1q) flow through the pass-through branch
  // already. So calling the IBM-style peephole on any chip is
  // safe (idempotent for non-IBM bases).
  (void)chip;
  return run(in);
}

}  // namespace spinor::passes

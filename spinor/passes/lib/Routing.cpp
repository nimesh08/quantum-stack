// spinor/passes/lib/Routing.cpp
//
// SABRE forward routing pass. Spec: docs/build/phaseA/M5_placement_routing.md.

#include "spinor/passes/Routing.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace spinor::passes {

namespace {

using namespace spinor::dialect;

constexpr double kExtendedSetWeight = 0.5;
constexpr std::size_t kExtendedSetSize = 20;

// Per-IR-value → virtual-qubit-index (root alloc).
struct VMap {
  std::vector<int> ofValue;
  int total = 0;
};

VMap buildVMap(const Module& m) {
  VMap v;
  v.ofValue.assign(m.numValues(), -1);
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      v.ofValue[op.results.front().v] = v.total++;
      continue;
    }
    int nq = qubitArity(op.kind);
    if (nq <= 0) continue;
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      ValueId in = op.operands[k];
      ValueId out = op.results[k];
      if (in.v < v.ofValue.size() && v.ofValue[in.v] >= 0 &&
          out.v < v.ofValue.size()) {
        v.ofValue[out.v] = v.ofValue[in.v];
      }
    }
  }
  return v;
}

// Per-virtual-qubit op order: for each op, the set of virtual
// qubits it touches and the in-program-order index.
struct GateInfo {
  uint32_t opIdx;          // index into module's ops
  std::vector<int> vqs;    // virtual qubits this op touches
  bool twoQ() const { return vqs.size() == 2; }
};

std::vector<GateInfo> collectGates(const Module& m, const VMap& vm) {
  std::vector<GateInfo> out;
  out.reserve(m.numOps());
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit || op.kind == OpKind::AllocBit) {
      continue;
    }
    GateInfo g;
    g.opIdx = i;
    for (auto v : op.operands) {
      if (v.v < vm.ofValue.size() && vm.ofValue[v.v] >= 0) {
        g.vqs.push_back(vm.ofValue[v.v]);
      }
    }
    out.push_back(std::move(g));
  }
  return out;
}

// Helper: append an op directly (no result allocation; caller
// supplies result types via Module API), used when we are
// constructing the routed output below.

}  // namespace

RoutingResult Routing::run(const Module& in, const registry::ChipInfo& chip,
                           const CouplingGraph& g, Layout initial) const {
  RoutingResult R;
  R.initialLayout = initial;
  R.finalLayout = initial;

  // Build the output module with chip.id as target and pre-allocate
  // all physical qubits.
  Module& out = R.module;
  out.name = in.name;
  out.targetAttr = chip.id;
  Builder builder(out);

  std::size_t P = g.qubits();
  std::vector<ValueId> phys;  // phys[k] = the *current* live value at physical k
  phys.reserve(P);
  for (std::size_t k = 0; k < P; ++k) {
    ValueId v = builder.allocQubit();
    out.setName(v, "q" + std::to_string(k));
    phys.push_back(v);
  }
  // Per-virtual-bit tracking for `measure`.
  std::map<int, ValueId> bitOfVB;  // virtual bit index -> bit value
  // Allocate output bits as we encounter measure ops; named
  // by their order of first appearance.
  int nextBitId = 0;

  // Per-physical-qubit generation counter for naming.
  std::vector<int> pgen(P, 0);
  auto nameAfter = [&](std::size_t k) {
    return "q" + std::to_string(k) + "_" + std::to_string(++pgen[k]);
  };

  // Build VMap and gate list of the input.
  VMap vm = buildVMap(in);
  std::vector<GateInfo> gates = collectGates(in, vm);

  // Per-virtual-qubit "next gate index" pointer.
  int V = vm.total;
  std::vector<std::vector<std::size_t>> dag(V);  // gate indices per vq, in order
  for (std::size_t gi = 0; gi < gates.size(); ++gi) {
    for (int vq : gates[gi].vqs) {
      if (vq >= 0 && vq < V) dag[vq].push_back(gi);
    }
  }
  std::vector<std::size_t> head(V, 0);  // next-unprocessed gate per vq
  std::vector<bool> done(gates.size(), false);

  Layout L = initial;
  R.swapCount = 0;

  // A gate is in the front layer if its predecessors on each operand
  // virtual qubit are all done.
  auto isFront = [&](std::size_t gi) {
    if (done[gi]) return false;
    const GateInfo& g_ = gates[gi];
    for (int vq : g_.vqs) {
      if (head[vq] >= dag[vq].size()) return false;
      if (dag[vq][head[vq]] != gi) return false;
    }
    return true;
  };

  auto refreshFront = [&](std::set<std::size_t>& front) {
    for (std::size_t gi = 0; gi < gates.size(); ++gi) {
      if (isFront(gi)) front.insert(gi);
    }
  };

  // Map a virtual qubit to its current physical phys-id.
  auto v2p = [&](int vq) { return L.v2p[vq]; };

  // Physical pair distance (under L) for a 2q gate.
  auto distFor = [&](const GateInfo& gi) -> int {
    if (gi.vqs.size() != 2) return 0;
    return g.distance(v2p(gi.vqs[0]), v2p(gi.vqs[1]));
  };

  // Score a SWAP (between physical pair) given the front layer
  // and an extended-set look-ahead of upcoming gates.
  auto scoreSwap =
      [&](int pa, int pb, const std::set<std::size_t>& front,
          const std::vector<std::size_t>& extended) -> double {
    Layout La = L;
    La.apply_swap(pa, pb);
    auto distInLa = [&](const GateInfo& gi) {
      if (gi.vqs.size() != 2) return 0;
      return g.distance(La.v2p[gi.vqs[0]], La.v2p[gi.vqs[1]]);
    };
    double s1 = 0.0;
    int n1 = 0;
    for (std::size_t fi : front) {
      if (gates[fi].twoQ()) {
        int d = distInLa(gates[fi]);
        if (d == INT_MAX) return 1e9;
        s1 += d;
        ++n1;
      }
    }
    if (n1 > 0) s1 /= n1;
    double s2 = 0.0;
    int n2 = 0;
    for (std::size_t ei : extended) {
      if (gates[ei].twoQ()) {
        int d = distInLa(gates[ei]);
        if (d == INT_MAX) continue;
        s2 += d;
        ++n2;
      }
    }
    if (n2 > 0) s2 /= n2;
    return s1 + kExtendedSetWeight * s2;
  };

  // Emit a non-routing op from the input by translating its operands
  // through the current physical map and threading new live values.
  auto commitGate = [&](std::size_t gi) {
    const GateInfo& gi_ = gates[gi];
    const Op& src = in.op(OpId{gi_.opIdx});
    Op out_op;
    out_op.kind = src.kind;
    out_op.attributes = src.attributes;
    out_op.loc = src.loc;
    // Translate qubit operands.
    std::vector<int> physOperands;
    for (int vq : gi_.vqs) {
      int p = v2p(vq);
      out_op.operands.push_back(phys[p]);
      physOperands.push_back(p);
    }
    OpId id = out.addOp(std::move(out_op));
    Op& live = out.opMut(id);
    // Allocate results: for each qubit operand, a fresh qubit value
    // (except measure which produces only a bit).
    int nq = qubitArity(src.kind);
    int qResults = nq;
    bool producesBit = (src.kind == OpKind::Measure ||
                        src.kind == OpKind::AllocBit);
    if (src.kind == OpKind::Measure) qResults = 0;
    if (src.kind == OpKind::Reset) qResults = 1;
    for (int k = 0; k < qResults; ++k) {
      ValueId nv = out.addValue(qubitType(), id);
      live.results.push_back(nv);
      // Update phys[k] to the new value and rename.
      phys[physOperands[k]] = nv;
      out.setName(nv, nameAfter(physOperands[k]));
    }
    if (producesBit) {
      ValueId bv = out.addValue(bitType(), id);
      live.results.push_back(bv);
      // Name by virtual register element index.
      out.setName(bv, "c" + std::to_string(nextBitId++));
    }
    done[gi] = true;
    for (int vq : gi_.vqs) {
      if (!dag[vq].empty() && head[vq] < dag[vq].size() &&
          dag[vq][head[vq]] == gi) {
        ++head[vq];
      }
    }
  };

  auto insertSwap = [&](int pa, int pb) {
    // SWAP at physical pair (pa, pb).
    Op op;
    op.kind = OpKind::Swap;
    op.operands = {phys[pa], phys[pb]};
    OpId id = out.addOp(std::move(op));
    Op& live = out.opMut(id);
    ValueId na = out.addValue(qubitType(), id);
    ValueId nb = out.addValue(qubitType(), id);
    live.results.push_back(na);
    live.results.push_back(nb);
    out.setName(na, nameAfter(pa));
    out.setName(nb, nameAfter(pb));
    phys[pa] = na;
    phys[pb] = nb;
    L.apply_swap(pa, pb);
    ++R.swapCount;
  };

  // Main loop: at each step, advance any local 1q or already-local
  // 2q ops; then if the front layer has a non-local 2q gate, pick
  // the best SWAP and apply it.
  std::set<std::size_t> front;
  refreshFront(front);

  while (!front.empty()) {
    // 1) Drain any locally-executable gates.
    bool progressed = true;
    while (progressed) {
      progressed = false;
      for (auto it = front.begin(); it != front.end();) {
        std::size_t gi = *it;
        const GateInfo& gi_ = gates[gi];
        bool exec = true;
        if (gi_.twoQ()) {
          if (!g.connected(v2p(gi_.vqs[0]), v2p(gi_.vqs[1]))) exec = false;
        }
        if (exec) {
          commitGate(gi);
          it = front.erase(it);
          progressed = true;
        } else {
          ++it;
        }
      }
      if (progressed) {
        // Refresh front layer with newly-frontable gates.
        for (std::size_t gi = 0; gi < gates.size(); ++gi) {
          if (isFront(gi)) front.insert(gi);
        }
      }
    }

    if (front.empty()) break;

    // 2) Pick the best SWAP.
    // Build the extended look-ahead set.
    std::vector<std::size_t> extended;
    {
      std::vector<std::size_t> tmpHead = head;
      std::vector<std::size_t> tmpFront(front.begin(), front.end());
      // Walk forward up to kExtendedSetSize gates beyond the front.
      std::set<std::size_t> seen(tmpFront.begin(), tmpFront.end());
      std::size_t added = 0;
      // For each virtual qubit, scan beyond head to find next 2q
      // gate not in the front.
      while (added < kExtendedSetSize) {
        bool any = false;
        for (int vq = 0; vq < V; ++vq) {
          for (std::size_t k = tmpHead[vq]; k < dag[vq].size(); ++k) {
            std::size_t gi = dag[vq][k];
            if (!gates[gi].twoQ()) continue;
            if (seen.contains(gi)) continue;
            extended.push_back(gi);
            seen.insert(gi);
            ++added;
            any = true;
            if (added >= kExtendedSetSize) break;
          }
          if (added >= kExtendedSetSize) break;
        }
        if (!any) break;
      }
    }

    // Candidate swaps: any edge where one endpoint is a physical
    // qubit holding a virtual qubit involved in some front-layer
    // 2q gate.
    std::set<std::pair<int, int>> cand;
    for (std::size_t fi : front) {
      if (!gates[fi].twoQ()) continue;
      for (int vq : gates[fi].vqs) {
        int p = v2p(vq);
        for (int q : g.neighbours(p)) {
          int lo = std::min(p, q), hi = std::max(p, q);
          cand.insert({lo, hi});
        }
      }
    }
    if (cand.empty()) {
      // No candidate moves available — break to avoid infinite loop.
      break;
    }

    int bestA = -1, bestB = -1;
    double bestScore = 1e9;
    for (auto [a, b] : cand) {
      double s = scoreSwap(a, b, front, extended);
      if (s < bestScore - 1e-9 ||
          (std::abs(s - bestScore) < 1e-9 &&
           (bestA < 0 || a < bestA || (a == bestA && b < bestB)))) {
        bestScore = s;
        bestA = a;
        bestB = b;
      }
    }
    insertSwap(bestA, bestB);
  }

  // Final passthrough: any remaining single-qubit / measure / reset /
  // barrier ops not yet scheduled (e.g. when there's no front 2q at
  // start). Iterate to fixed point on isFront.
  bool progressed = true;
  while (progressed) {
    progressed = false;
    for (std::size_t gi = 0; gi < gates.size(); ++gi) {
      if (done[gi]) continue;
      if (!isFront(gi)) continue;
      const GateInfo& gi_ = gates[gi];
      if (gi_.twoQ() && !g.connected(v2p(gi_.vqs[0]), v2p(gi_.vqs[1]))) {
        continue;
      }
      commitGate(gi);
      progressed = true;
    }
  }

  R.finalLayout = L;
  return R;
}

}  // namespace spinor::passes

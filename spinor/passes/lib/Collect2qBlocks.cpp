// spinor/passes/lib/Collect2qBlocks.cpp
//
// DAG-walking block collector. For the Spinor IR, op order ==
// program order (we don't track explicit DAG edges), so a single
// linear scan suffices: maintain one "open" 2Q block per
// (qa, qb) pair; close it whenever a wider op or a fence touches
// either qubit.

#include "spinor/passes/Collect2qBlocks.h"

#include <map>

namespace spinor::passes {

using namespace spinor::dialect;

namespace {

// Recover virtual-qubit positions per Value the same way other
// passes do (allocations in order define logical qubits).
struct VirtIndex {
  std::vector<int> ofValue;  // -1 if not a qubit value
};

VirtIndex buildVirtIndex(const Module& m) {
  VirtIndex vi;
  vi.ofValue.assign(m.numValues(), -1);
  int next = 0;
  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit) {
      vi.ofValue[op.results.front().v] = next++;
      continue;
    }
    int nq = qubitArity(op.kind);
    int qResults = nq;
    if (op.kind == OpKind::Measure) qResults = 0;
    for (int k = 0; k < qResults && k < (int)op.results.size(); ++k) {
      vi.ofValue[op.results[k].v] = vi.ofValue[op.operands[k].v];
    }
  }
  return vi;
}

bool isFence(OpKind k) {
  return k == OpKind::Measure || k == OpKind::Reset ||
         k == OpKind::Barrier;
}

}  // namespace

std::vector<TwoQBlock> Collect2qBlocks::run(const Module& m) const {
  std::vector<TwoQBlock> out;
  VirtIndex vi = buildVirtIndex(m);

  // One open block per qubit position. When a third qubit or a
  // fence touches either qubit in a block, that block is
  // finalized and emitted.
  std::map<int, int> openIdx;  // qubit -> index into `out` (or -1)

  auto flushFor = [&](int q) {
    auto it = openIdx.find(q);
    if (it == openIdx.end()) return;
    int idx = it->second;
    if (idx < 0) return;
    // The other qubit of the same block also points here; clear
    // both entries.
    const TwoQBlock& blk = out[idx];
    openIdx[blk.qa] = -1;
    openIdx[blk.qb] = -1;
  };

  for (uint32_t i = 0; i < m.numOps(); ++i) {
    const Op& op = m.op(OpId{i});
    if (op.kind == OpKind::AllocQubit ||
        op.kind == OpKind::AllocBit) continue;

    int nq = qubitArity(op.kind);
    if (isFence(op.kind)) {
      // Close any open block touching this op's qubits.
      for (const auto& v : op.operands) {
        int q = vi.ofValue[v.v];
        if (q >= 0) flushFor(q);
      }
      continue;
    }
    if (nq == 1) {
      int q = vi.ofValue[op.operands.front().v];
      // 1Q op extends the block on `q` if open and the block's
      // *other* qubit hasn't been "closed". Otherwise, no-op.
      auto it = openIdx.find(q);
      if (it != openIdx.end() && it->second >= 0) {
        out[it->second].ops.push_back(OpId{i});
      }
      continue;
    }
    if (nq == 2 && op.operands.size() >= 2) {
      int qa = vi.ofValue[op.operands[0].v];
      int qb = vi.ofValue[op.operands[1].v];
      if (qa > qb) std::swap(qa, qb);
      // If both qubits are already in the same open block,
      // append. Otherwise, flush any conflicting blocks and
      // open a fresh one.
      auto ita = openIdx.find(qa);
      auto itb = openIdx.find(qb);
      bool sameBlock =
          (ita != openIdx.end() && itb != openIdx.end() &&
           ita->second >= 0 && ita->second == itb->second);
      if (!sameBlock) {
        if (ita != openIdx.end()) flushFor(qa);
        if (itb != openIdx.end()) flushFor(qb);
        TwoQBlock blk;
        blk.qa = qa;
        blk.qb = qb;
        int idx = (int)out.size();
        out.push_back(std::move(blk));
        openIdx[qa] = idx;
        openIdx[qb] = idx;
      }
      out[openIdx[qa]].ops.push_back(OpId{i});
      continue;
    }
    // 3+Q (e.g. Barrier on 3 qubits handled by fence path above).
    // Anything else conservatively closes blocks on its qubits.
    for (const auto& v : op.operands) {
      int q = vi.ofValue[v.v];
      if (q >= 0) flushFor(q);
    }
  }
  return out;
}

}  // namespace spinor::passes

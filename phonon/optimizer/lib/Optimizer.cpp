// phonon/optimizer/lib/Optimizer.cpp
//
// Built optimizer passes (M5).
//
// Each pass operates on a phonon::dialect::Module. The
// implementation strategy:
//
//   - Walk the op vector once, marking ops as dead in a
//     parallel `dead[i]` vector.
//   - For some passes, also accumulate replacement ops + value
//     remaps.
//   - At end-of-pass, rebuild the module by copying live ops
//     in their original order.
//
// Module rebuild is safe because the in-tree backend's value
// table is monotonic: we can copy ops + their results into a
// fresh module while remapping operand IDs through a vmap.

#include "phonon/optimizer/Optimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace phonon::optimizer {

namespace pd = phonon::dialect;

namespace {

constexpr double kTwoPi = 6.283185307179586476925286766559;

// Carry over only those attributes the kind requires.
std::vector<pd::Attribute> filterAttrs(const pd::Op& op) {
  return op.attributes;
}

// Rebuild the module by copying live ops. `dead[i]` is true if
// op `i` should be dropped.
void compact(pd::Module& m, const std::vector<bool>& dead,
             const std::unordered_map<std::uint32_t, double>* angleOverrides = nullptr) {
  pd::Module out;
  out.targetAttr = m.targetAttr;
  out.name = m.name;
  std::unordered_map<std::uint32_t, pd::ValueId> vmap;

  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (dead[i]) continue;
    pd::OpId pid{i};
    const pd::Op& op = m.op(pid);
    pd::Op nop;
    nop.kind = op.kind;
    for (pd::ValueId v : op.operands) {
      auto it = vmap.find(v.v);
      nop.operands.push_back(it != vmap.end() ? it->second : v);
    }
    nop.attributes = filterAttrs(op);
    if (angleOverrides) {
      auto a = angleOverrides->find(i);
      if (a != angleOverrides->end()) {
        for (auto& attr : nop.attributes) {
          if (attr.name == "angle") attr.value = a->second;
        }
      }
    }
    nop.loc = op.loc;
    pd::OpId nid = out.addOp(std::move(nop));
    pd::Op& live = out.opMut(nid);
    for (pd::ValueId r : op.results) {
      pd::Type t = m.typeOf(r);
      pd::ValueId nv = out.addValue(t, nid);
      vmap[r.v] = nv;
      live.results.push_back(nv);
      std::string n = m.nameOf(r);
      if (!n.empty() && n[0] == '%') n = n.substr(1);
      if (!n.empty() && n.find('?') == std::string::npos) {
        out.setName(nv, n);
      }
    }
  }

  m = std::move(out);
}

// True if op uses qubit operands.
bool consumesQubits(pd::OpKind k) {
  if (!pd::isSpinorKind(k)) return false;
  return k != pd::OpKind::AllocQubit && k != pd::OpKind::AllocBit;
}

// Inverse-pair check for self-inverse 1q gates.
bool isSelfInverse(pd::OpKind a, pd::OpKind b) {
  if (a != b) return false;
  switch (a) {
    case pd::OpKind::H:
    case pd::OpKind::X:
    case pd::OpKind::Y:
    case pd::OpKind::Z:
    case pd::OpKind::Cx:
    case pd::OpKind::Cz:
    case pd::OpKind::Swap:
      return true;
    default: return false;
  }
}
bool isInversePair(pd::OpKind a, pd::OpKind b) {
  if (isSelfInverse(a, b)) return true;
  if (a == pd::OpKind::S   && b == pd::OpKind::Sdg)  return true;
  if (a == pd::OpKind::Sdg && b == pd::OpKind::S)    return true;
  if (a == pd::OpKind::T   && b == pd::OpKind::Tdg)  return true;
  if (a == pd::OpKind::Tdg && b == pd::OpKind::T)    return true;
  if (a == pd::OpKind::Sx  && b == pd::OpKind::Sxdg) return true;
  if (a == pd::OpKind::Sxdg && b == pd::OpKind::Sx)  return true;
  return false;
}

double opAngle(const pd::Op& op) {
  for (const auto& a : op.attributes) {
    if (a.name == "angle") return std::get<double>(a.value);
  }
  return 0.0;
}

}  // namespace

// --- Cancellation --------------------------------------------------------

Stats cancelInversePairs(pd::Module& m) {
  Stats s;
  std::vector<bool> dead(m.numOps(), false);
  // Per-qubit "last live op" tracker. Keyed by the producer
  // ValueId of the qubit operand.
  std::unordered_map<std::uint32_t, std::uint32_t> lastOpIdx;
  // For multi-qubit gates we must track per-qubit; the trick is
  // that each gate produces a fresh ValueId for each output,
  // so we walk: for each operand, look up its producer (which is
  // also the previous op on this qubit). Need a way to relate
  // "the qubit slot" — easier: for each op, record a signature
  // (kind + sorted operand ValueIds). Two consecutive ops with
  // the same signature on the same qubit operands cancel.

  // Strategy: walk in order. Maintain `prevOnQubit[v] = opIdx`
  // where v is a ValueId of a qubit currently "live" (latest
  // producer for that physical slot). When we see an op:
  //   - if all its qubit operands have the same prev op AND the
  //     prev op's kind forms an inverse pair AND the operand sets
  //     match → mark both dead, drop their result bindings
  //     (vmap will remap the next user back to the operand).

  std::unordered_map<std::uint32_t, std::uint32_t> prevOpForValue;

  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    pd::OpId pid{i};
    const pd::Op& op = m.op(pid);
    if (!consumesQubits(op.kind)) {
      // Update prevOpForValue for results.
      for (pd::ValueId r : op.results) {
        if (m.typeOf(r).kind == pd::TypeKind::Qubit) {
          prevOpForValue[r.v] = i;
        }
      }
      continue;
    }

    // Special: barrier / reset / measure are fences; we don't try
    // to cancel them but they DO update the slot.
    if (op.kind == pd::OpKind::Barrier ||
        op.kind == pd::OpKind::Reset ||
        op.kind == pd::OpKind::Measure) {
      for (pd::ValueId r : op.results) {
        if (m.typeOf(r).kind == pd::TypeKind::Qubit) {
          prevOpForValue[r.v] = i;
        }
      }
      continue;
    }

    // Find the prev op for each qubit operand.
    std::vector<std::uint32_t> prevs;
    bool good = true;
    for (pd::ValueId opd : op.operands) {
      if (m.typeOf(opd).kind != pd::TypeKind::Qubit) continue;
      auto it = prevOpForValue.find(opd.v);
      if (it == prevOpForValue.end()) { good = false; break; }
      prevs.push_back(it->second);
    }
    if (good && !prevs.empty() &&
        std::all_of(prevs.begin(), prevs.end(),
                    [&](std::uint32_t p){ return p == prevs[0]; })) {
      const pd::Op& prev = m.op(pd::OpId{prevs[0]});
      // Check: each qubit-operand of `op` is precisely the
      // result-of-prev at the matching position (i.e. prev's op
      // chained directly into op with no other op in between).
      bool chains = !dead[prevs[0]] &&
                    isInversePair(prev.kind, op.kind) &&
                    op.operands.size() == prev.results.size();
      if (chains) {
        for (std::size_t k = 0; k < op.operands.size(); ++k) {
          if (op.operands[k] != prev.results[k]) { chains = false; break; }
        }
      }
      // Also ensure attribute lists match (so e.g. Rzz with same
      // angle would chain only with itself).
      if (chains && op.attributes.size() != prev.attributes.size()) {
        chains = false;
      }
      if (chains) {
        // Cancel both.
        dead[prevs[0]] = true;
        dead[i] = true;
        s.gatesRemoved += 2;
        for (std::size_t k = 0; k < op.results.size() &&
                                  k < prev.operands.size(); ++k) {
          // Map current op's results back to prev's operands.
          // We'll fix prev.results' downstream in a transitive
          // remap pass below.
        }
        continue;
      }
    }
    // Default: record this op as the producer of its results.
    for (pd::ValueId r : op.results) {
      if (m.typeOf(r).kind == pd::TypeKind::Qubit) {
        prevOpForValue[r.v] = i;
      }
    }
  }

  // For dead ops, we also need to remap any subsequent operand
  // references through to the cancelled op's operands. Build a
  // value-id remap: for each dead op, its results map to the
  // corresponding operand of the same index.
  std::unordered_map<std::uint32_t, std::uint32_t> resultRemap;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (!dead[i]) continue;
    const pd::Op& op = m.op(pd::OpId{i});
    for (std::size_t k = 0; k < op.results.size() &&
                              k < op.operands.size(); ++k) {
      resultRemap[op.results[k].v] = op.operands[k].v;
    }
  }
  // Apply remap transitively (in case both ops in a pair are
  // dead — the second's results need to map all the way back).
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto& [k, v] : resultRemap) {
      auto it = resultRemap.find(v);
      if (it != resultRemap.end() && it->second != v) {
        v = it->second;
        changed = true;
      }
    }
  }
  // Rewrite operands of every live op.
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (dead[i]) continue;
    pd::Op& op = m.opMut(pd::OpId{i});
    for (pd::ValueId& opd : op.operands) {
      auto it = resultRemap.find(opd.v);
      if (it != resultRemap.end()) opd = pd::ValueId{it->second};
    }
  }

  compact(m, dead);
  return s;
}

// --- Rotation merging ----------------------------------------------------

Stats mergeRotations(pd::Module& m) {
  Stats s;
  std::vector<bool> dead(m.numOps(), false);
  std::unordered_map<std::uint32_t, double> mergedAngles;
  std::unordered_map<std::uint32_t, std::uint32_t> prevOpForValue;
  // Remap: when we drop an op, route its result back to its operand.
  std::unordered_map<std::uint32_t, std::uint32_t> resultRemap;

  auto isRot = [](pd::OpKind k) {
    return k == pd::OpKind::Rx || k == pd::OpKind::Ry ||
           k == pd::OpKind::Rz;
  };

  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    pd::OpId pid{i};
    const pd::Op& op = m.op(pid);
    if (!consumesQubits(op.kind)) {
      for (pd::ValueId r : op.results) {
        if (m.typeOf(r).kind == pd::TypeKind::Qubit) prevOpForValue[r.v] = i;
      }
      continue;
    }
    if (op.kind == pd::OpKind::Barrier || op.kind == pd::OpKind::Reset ||
        op.kind == pd::OpKind::Measure || op.operands.empty()) {
      for (pd::ValueId r : op.results) {
        if (m.typeOf(r).kind == pd::TypeKind::Qubit) prevOpForValue[r.v] = i;
      }
      continue;
    }
    if (isRot(op.kind) && op.operands.size() == 1) {
      auto it = prevOpForValue.find(op.operands[0].v);
      if (it != prevOpForValue.end() && !dead[it->second]) {
        const pd::Op& prev = m.op(pd::OpId{it->second});
        if (prev.kind == op.kind) {
          double pA = mergedAngles.count(it->second) ? mergedAngles[it->second]
                                                       : opAngle(prev);
          double cA = opAngle(op);
          double sum = std::fmod(pA + cA, kTwoPi);
          if (sum > kTwoPi / 2)  sum -= kTwoPi;
          if (sum < -kTwoPi / 2) sum += kTwoPi;
          if (std::abs(sum) < 1e-12) {
            // Both drop.
            dead[it->second] = true;
            dead[i] = true;
            s.rotationsMerged += 2;
            // Remap chain: prev's result back to prev's operand;
            // op's result back to op's operand.
            for (std::size_t k = 0; k < prev.results.size() &&
                                      k < prev.operands.size(); ++k) {
              resultRemap[prev.results[k].v] = prev.operands[k].v;
            }
            for (std::size_t k = 0; k < op.results.size() &&
                                      k < op.operands.size(); ++k) {
              resultRemap[op.results[k].v] = op.operands[k].v;
            }
            continue;
          }
          // Merge into prev; drop current.
          mergedAngles[it->second] = sum;
          dead[i] = true;
          ++s.rotationsMerged;
          // Reroute current's result to prev's result.
          for (std::size_t k = 0; k < op.results.size() &&
                                    k < op.operands.size(); ++k) {
            // op's operand is prev's result; bypass current.
            resultRemap[op.results[k].v] = op.operands[k].v;
          }
          // Update prevOpForValue for the (now) merged result —
          // it stays "owned" by prev.
          for (pd::ValueId r : op.results) {
            if (m.typeOf(r).kind == pd::TypeKind::Qubit) {
              prevOpForValue[r.v] = it->second;
            }
          }
          continue;
        }
      }
    }
    for (pd::ValueId r : op.results) {
      if (m.typeOf(r).kind == pd::TypeKind::Qubit) prevOpForValue[r.v] = i;
    }
  }

  // Transitive close.
  bool changed = true;
  while (changed) {
    changed = false;
    for (auto& [k, v] : resultRemap) {
      auto it = resultRemap.find(v);
      if (it != resultRemap.end() && it->second != v) { v = it->second; changed = true; }
    }
  }
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    if (dead[i]) continue;
    pd::Op& op = m.opMut(pd::OpId{i});
    for (pd::ValueId& opd : op.operands) {
      auto it = resultRemap.find(opd.v);
      if (it != resultRemap.end()) opd = pd::ValueId{it->second};
    }
  }

  compact(m, dead, &mergedAngles);
  return s;
}

// --- Commutation reorder (lightweight) ------------------------------------

Stats commuteAndReduce(pd::Module& m) {
  Stats s;
  // Phase B M5 reorder is intentionally minimal: it does not
  // physically swap ops in the module, but it re-runs cancellation
  // and merging because some patterns expose only after an outer
  // pass. A more advanced commutation table can be added later
  // (M7 swap-policy work).
  Stats c1 = cancelInversePairs(m);
  Stats c2 = mergeRotations(m);
  s.gatesRemoved      = c1.gatesRemoved;
  s.rotationsMerged   = c2.rotationsMerged;
  return s;
}

// --- Scheduling ----------------------------------------------------------

Stats schedule(pd::Module& m) {
  Stats s;
  s.depthBefore = 0;
  // Compute depth: per-qubit ready time advances on every op
  // touching it.
  std::unordered_map<std::uint32_t, std::size_t> ready;
  std::size_t depth = 0;
  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    const pd::Op& op = m.op(pd::OpId{i});
    if (!consumesQubits(op.kind) || op.operands.empty()) continue;
    std::size_t t = 0;
    for (pd::ValueId opd : op.operands) {
      if (m.typeOf(opd).kind != pd::TypeKind::Qubit) continue;
      auto it = ready.find(opd.v);
      if (it != ready.end()) t = std::max(t, it->second);
    }
    ++t;
    for (pd::ValueId r : op.results) {
      if (m.typeOf(r).kind == pd::TypeKind::Qubit) ready[r.v] = t;
    }
    depth = std::max(depth, t);
  }
  s.depthBefore = s.depthAfter = depth;
  // For Phase B we report depth without reordering ops; a true
  // ASAP reorder requires a per-op stable sort that respects op
  // result-uses, which we hand to the M9 emitter for the chip's
  // own scheduler. (Phase D will replace this.)
  return s;
}

// --- Pipeline ------------------------------------------------------------

Stats runBuiltPipeline(pd::Module& m) {
  Stats total;
  Stats a = cancelInversePairs(m);
  Stats b = mergeRotations(m);
  Stats c = commuteAndReduce(m);
  Stats d = schedule(m);
  total.gatesRemoved      = a.gatesRemoved + c.gatesRemoved;
  total.rotationsMerged   = b.rotationsMerged + c.rotationsMerged;
  total.reorderingsApplied = 0;
  total.depthBefore = d.depthBefore;
  total.depthAfter  = d.depthAfter;
  return total;
}

}  // namespace phonon::optimizer

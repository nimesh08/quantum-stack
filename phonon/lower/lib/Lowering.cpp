// phonon/lower/lib/Lowering.cpp

#include "phonon/lower/Lowering.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace phonon::lower {

namespace pd = phonon::dialect;
namespace sd = spinor::dialect;

namespace {

struct FuncRange {
  std::uint32_t bodyStart = 0;   // index after the def op
  std::uint32_t bodyEnd = 0;     // index of end_def (exclusive)
  std::uint32_t defId = 0;
  std::vector<pd::ValueId> paramValues;  // results on the def op
  std::vector<pd::Type> paramTypes;
};

struct Lowerer {
  const pd::Module& src;
  sd::Module out;
  sd::Builder b;
  sd::Diagnostics diag;
  const spinor::verify::TargetInfo* target = nullptr;

  // Map from Phonon ValueId -> Spinor ValueId.
  std::unordered_map<std::uint32_t, sd::ValueId> vmap;
  // Compile-time integer values for classical scalars + for-vars.
  std::unordered_map<std::uint32_t, double> ctMap;

  // Function index.
  std::unordered_map<std::string, FuncRange> funcs;
  // For each call we are inlining, push the function name; used to
  // detect recursion.
  std::vector<std::string> callStack;

  Lowerer(const pd::Module& m,
          const spinor::verify::TargetInfo* t)
      : src(m), b(out), target(t) {
    out.targetAttr = m.targetAttr;
    out.name = m.name;
  }

  sd::ValueId mapValue(pd::ValueId pv) {
    auto it = vmap.find(pv.v);
    if (it == vmap.end()) {
      diag.error("internal: unmapped Phonon value " +
                 std::to_string(pv.v));
      return sd::kInvalidValue;
    }
    return it->second;
  }

  // -- Index pass: find all function definitions ----------------------
  void indexFunctions() {
    for (std::uint32_t i = 0; i < src.numOps(); ++i) {
      pd::OpId id{i};
      const pd::Op& op = src.op(id);
      if (op.kind == pd::OpKind::Def) {
        std::string name;
        for (const auto& a : op.attributes) {
          if (a.name == "name") name = std::get<std::string>(a.value);
        }
        FuncRange fr;
        fr.defId = i;
        fr.bodyStart = i + 1;
        // find matching end_def
        std::uint32_t depth = 1;
        std::uint32_t j = i + 1;
        while (j < src.numOps() && depth > 0) {
          const pd::Op& q = src.op(pd::OpId{j});
          if (q.kind == pd::OpKind::Def) ++depth;
          else if (q.kind == pd::OpKind::EndDef) --depth;
          if (depth == 0) break;
          ++j;
        }
        fr.bodyEnd = j;
        fr.paramValues = op.results;
        for (pd::ValueId v : op.results) fr.paramTypes.push_back(src.typeOf(v));
        funcs[name] = std::move(fr);
        i = j;  // skip past end_def
      }
    }
  }

  // -- Match marker: for `If`, find end_if (handling nested) ----------
  std::uint32_t matchMarker(std::uint32_t startIdx,
                            pd::OpKind beginKind,
                            pd::OpKind endKind) {
    std::uint32_t depth = 1;
    std::uint32_t j = startIdx + 1;
    while (j < src.numOps() && depth > 0) {
      pd::OpKind k = src.op(pd::OpId{j}).kind;
      if (k == beginKind) ++depth;
      else if (k == endKind) --depth;
      if (depth == 0) break;
      ++j;
    }
    return j;
  }

  // -- Emit a single spinor.* op as a copy with mapped operands -------
  void emitSpinorOp(pd::OpId pid) {
    const pd::Op& op = src.op(pid);
    sd::OpKind sk = pd::toSpinorKind(op.kind);
    sd::Op sop;
    sop.kind = sk;
    for (pd::ValueId v : op.operands) sop.operands.push_back(mapValue(v));
    // Copy attributes: only "angle" / "theta" / "phi" carry to spinor.*
    for (const auto& a : op.attributes) {
      if (a.name == "angle" || a.name == "theta" || a.name == "phi") {
        sop.attributes.push_back(sd::Attribute{a.name, a.value});
      }
    }
    sop.loc = sd::Location{op.loc.file, op.loc.line, op.loc.column};
    sd::OpId sid = out.addOp(std::move(sop));
    sd::Op& live = out.opMut(sid);
    // Allocate result types matching the Spinor signature, in order.
    int qResults = 0;
    bool producesBit = false;
    if (sk == sd::OpKind::AllocQubit || sk == sd::OpKind::Reset) {
      qResults = 1;
    } else if (sk == sd::OpKind::AllocBit || sk == sd::OpKind::Measure) {
      producesBit = true;
    } else if (sk == sd::OpKind::Cx || sk == sd::OpKind::Cz ||
               sk == sd::OpKind::Swap || sk == sd::OpKind::Ecr ||
               sk == sd::OpKind::Ms || sk == sd::OpKind::Rzz) {
      qResults = 2;
    } else if (sk == sd::OpKind::Barrier) {
      qResults = 0;
    } else {
      // single-qubit gates
      qResults = 1;
    }
    std::vector<sd::ValueId> outResults;
    for (int k = 0; k < qResults; ++k) {
      sd::ValueId v = out.addValue(sd::qubitType(), sid);
      outResults.push_back(v);
      live.results.push_back(v);
    }
    if (producesBit) {
      sd::ValueId v = out.addValue(sd::bitType(), sid);
      outResults.push_back(v);
      live.results.push_back(v);
    }
    // Map Phonon results to the freshly emitted Spinor results.
    for (std::size_t k = 0; k < op.results.size() && k < outResults.size(); ++k) {
      vmap[op.results[k].v] = outResults[k];
    }
    // Carry value names through (improves output readability).
    for (std::size_t k = 0; k < op.results.size() && k < outResults.size(); ++k) {
      std::string n = src.nameOf(op.results[k]);
      if (!n.empty() && n[0] == '%') n = n.substr(1);
      if (!n.empty() && n.find('?') == std::string::npos) {
        out.setName(outResults[k], n);
      }
    }
  }

  // -- Emit a range of ops, recursively unrolling control flow --------
  void emitRange(std::uint32_t lo, std::uint32_t hi) {
    std::uint32_t i = lo;
    while (i < hi) {
      pd::OpId pid{i};
      const pd::Op& op = src.op(pid);

      if (pd::isSpinorKind(op.kind)) {
        emitSpinorOp(pid);
        ++i; continue;
      }

      switch (op.kind) {
        case pd::OpKind::ConstInt: {
          double v = 0.0;
          for (const auto& a : op.attributes) if (a.name == "value") v = std::get<double>(a.value);
          for (pd::ValueId r : op.results) ctMap[r.v] = v;
          ++i; break;
        }
        case pd::OpKind::ConstAngle: {
          double v = 0.0;
          for (const auto& a : op.attributes) if (a.name == "value") v = std::get<double>(a.value);
          for (pd::ValueId r : op.results) ctMap[r.v] = v;
          ++i; break;
        }
        case pd::OpKind::BinOp: {
          // Evaluate symbolically if both operands have ct values.
          auto a = ctMap.find(op.operands[0].v);
          auto b_ = ctMap.find(op.operands[1].v);
          if (a != ctMap.end() && b_ != ctMap.end()) {
            std::string opn;
            for (const auto& at : op.attributes) if (at.name == "op") opn = std::get<std::string>(at.value);
            double r = 0.0;
            if      (opn == "+") r = a->second + b_->second;
            else if (opn == "-") r = a->second - b_->second;
            else if (opn == "*") r = a->second * b_->second;
            else if (opn == "/") r = a->second / b_->second;
            for (pd::ValueId rv : op.results) ctMap[rv.v] = r;
          }
          ++i; break;
        }
        case pd::OpKind::Cmp: {
          // No-op for lowering: the comparison's result feeds into
          // phonon.if which we flatten below. The spinor IR has no
          // notion of compare-bit; runtime feedforward is handled by
          // M9's emitter.
          ++i; break;
        }
        case pd::OpKind::Assign: {
          // Re-bind classical name; if rhs has ct value, propagate.
          if (op.operands.size() == 1) {
            auto it = ctMap.find(op.operands[0].v);
            if (it != ctMap.end()) ctMap[op.operands[0].v] = it->second;
          }
          ++i; break;
        }

        case pd::OpKind::Def: {
          // Skip the entire def block; it has been indexed in pass 1.
          std::string name;
          for (const auto& a : op.attributes) if (a.name == "name") name = std::get<std::string>(a.value);
          auto it = funcs.find(name);
          std::uint32_t skipTo = (it != funcs.end()) ? it->second.bodyEnd + 1 : i + 1;
          i = skipTo;
          break;
        }
        case pd::OpKind::EndDef:
          ++i; break;

        case pd::OpKind::Call: {
          std::string name;
          for (const auto& a : op.attributes) if (a.name == "name") name = std::get<std::string>(a.value);
          auto it = funcs.find(name);
          if (it == funcs.end()) {
            diag.error("call to unknown function: " + name);
            ++i; break;
          }
          // Recursion check.
          for (const auto& s : callStack) {
            if (s == name) {
              diag.error("recursive call to '" + name + "' is not supported");
              ++i; break;
            }
          }
          // Bind parameter values via vmap (qubit args) and ctMap
          // (classical args).
          const FuncRange& fr = it->second;
          // Save vmap entries for params so we can restore after inlining.
          std::vector<std::pair<std::uint32_t, std::optional<sd::ValueId>>> savedV;
          for (std::size_t k = 0; k < fr.paramValues.size() &&
                                   k < op.operands.size(); ++k) {
            std::uint32_t pv = fr.paramValues[k].v;
            auto sv = vmap.find(pv);
            savedV.push_back({pv, sv != vmap.end() ? std::optional<sd::ValueId>{sv->second} : std::nullopt});
            sd::ValueId sval = mapValue(op.operands[k]);
            vmap[pv] = sval;
          }
          // Inline the body. Track "return values" produced by a
          // phonon.return inside.
          std::vector<sd::ValueId> returnValues;
          callStack.push_back(name);
          {
            // Manual emit so we can intercept phonon.return.
            std::uint32_t j = fr.bodyStart;
            bool sawReturn = false;
            while (j < fr.bodyEnd) {
              pd::OpId jid{j};
              const pd::Op& jop = src.op(jid);
              if (jop.kind == pd::OpKind::Return) {
                sawReturn = true;
                for (pd::ValueId v : jop.operands) {
                  auto vmit = vmap.find(v.v);
                  if (vmit != vmap.end()) returnValues.push_back(vmit->second);
                }
                ++j; continue;
              }
              emitRange(j, j + 1);
              ++j;
            }
            if (!sawReturn) {
              // Auto-return: the latest vmap binding of each qubit
              // parameter is the function's result.
              for (std::size_t k = 0; k < fr.paramValues.size(); ++k) {
                if (fr.paramTypes[k].kind == pd::TypeKind::Qubit) {
                  auto vmit = vmap.find(fr.paramValues[k].v);
                  if (vmit != vmap.end()) returnValues.push_back(vmit->second);
                }
              }
            }
          }
          callStack.pop_back();
          // Bind call results to the returned values, in order
          // (qubit results only).
          std::size_t rk = 0;
          for (pd::ValueId rv : op.results) {
            if (rk < returnValues.size()) {
              vmap[rv.v] = returnValues[rk++];
            }
          }
          // Restore param vmap entries.
          for (const auto& p : savedV) {
            if (p.second) vmap[p.first] = *p.second;
            else vmap.erase(p.first);
          }
          ++i; break;
        }

        case pd::OpKind::For: {
          // Operands: lo (int value), hi (int value).
          double lo = 0.0, hi = 0.0;
          if (op.operands.size() >= 2) {
            auto a = ctMap.find(op.operands[0].v);
            auto b_ = ctMap.find(op.operands[1].v);
            if (a == ctMap.end() || b_ == ctMap.end()) {
              diag.error("for-loop bounds must be compile-time integers");
              ++i; break;
            }
            lo = a->second; hi = b_->second;
          }
          std::string var;
          for (const auto& a : op.attributes) if (a.name == "var") var = std::get<std::string>(a.value);
          std::uint32_t bodyStart = i + 1;
          std::uint32_t bodyEnd   = matchMarker(i, pd::OpKind::For,
                                                 pd::OpKind::EndFor);
          int loInt = static_cast<int>(lo);
          int hiInt = static_cast<int>(hi);
          for (int v = loInt; v < hiInt; ++v) {
            // Find the loop-var ValueId by name lookup in src? We
            // bound it during parsing into ctMap of the lo bound.
            // Re-bind by searching: if there is a const_int op in the
            // body that produced var bindings, no — var was held in
            // parser-side ctConst, not as a module value. The body
            // ops reference compile-time constants we already stored
            // in ctMap (the lo value). So our lo-binding already
            // serves as the iteration value for v == loInt. For
            // higher iterations we need to update the corresponding
            // ctMap entry.
            //
            // Pragmatic approach: walk the body once per iteration,
            // but rebind the var-tagged const_int. The body's
            // index references go through emitSpinorOp's mapValue,
            // which depends on per-op ctMap entries that are set at
            // body-emit time. So we just emit the body N times.
            //
            // For correctness of indexed qubit refs that depend on
            // the loop var: the parser folded those at parse time
            // using the `lo` value, so all iterations index the
            // SAME slot. To get true unrolling with different slots,
            // the parser would have to re-emit the body per
            // iteration. That is a known limitation of the simple
            // unroller; we mark a TODO and document in D9.
            (void)v;
            emitRange(bodyStart, bodyEnd);
          }
          i = bodyEnd + 1;  // skip end_for
          break;
        }
        case pd::OpKind::EndFor:
          ++i; break;

        case pd::OpKind::If: {
          // Phase B M4: emit then-body unconditionally; M9 will
          // wrap in chip-specific feedforward syntax.
          std::uint32_t bodyStart = i + 1;
          std::uint32_t bodyEnd   = matchMarker(i, pd::OpKind::If,
                                                 pd::OpKind::EndIf);
          // Find then_count / else_count attrs to split.
          std::uint32_t thenCount = bodyEnd - bodyStart;
          std::uint32_t elseCount = 0;
          for (const auto& a : op.attributes) {
            if (a.name == "then_count") thenCount = static_cast<std::uint32_t>(std::get<double>(a.value));
            if (a.name == "else_count") elseCount = static_cast<std::uint32_t>(std::get<double>(a.value));
          }
          (void)elseCount;
          emitRange(bodyStart, bodyStart + thenCount);
          i = bodyEnd + 1;
          break;
        }
        case pd::OpKind::EndIf:
          ++i; break;

        case pd::OpKind::While:
        case pd::OpKind::EndWhile: {
          diag.error("phonon.while is not supported by lowering "
                     "(use a bounded for-loop)");
          ++i; break;
        }

        case pd::OpKind::Return:
          // Should be handled inside Call inlining; if seen at the
          // top level, ignore.
          ++i; break;

        default:
          ++i; break;
      }
    }
  }
};

}  // namespace

Result lower(const pd::Module& m,
             const spinor::verify::TargetInfo* target) {
  Lowerer lo(m, target);
  lo.indexFunctions();
  lo.emitRange(0, m.numOps());
  Result r;
  r.diag = std::move(lo.diag);
  if (!r.diag.hasErrors()) r.module = std::move(lo.out);
  return r;
}

}  // namespace phonon::lower

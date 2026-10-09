// phonon/lower/lib/Lowering.cpp

#include "phonon/lower/Lowering.h"
#include "spinor/dialect/Circuit.h"

#include <cmath>
#include <algorithm>
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
  std::vector<sd::ValueId>* returnSink = nullptr;
  bool returning = false;
  std::size_t expandedIterations = 0;
  std::size_t nextAnonymousBit = 0;

  Lowerer(const pd::Module& m,
          const spinor::verify::TargetInfo* t)
      : src(m), b(out), target(t) {
    out.targetAttr = m.targetAttr;
    out.name = m.name;
    // Reserve source register slots and explicit destinations before assigning
    // anonymous library measurements. A later named readout must not overwrite
    // a bit which an earlier feed-forward predicate still references.
    std::size_t declaredBits = 0;
    for (const auto& op : m.ops()) {
      if (op.kind == pd::OpKind::AllocBit) ++declaredBits;
      if (op.kind != pd::OpKind::Measure) continue;
      for (const auto& a : op.attributes) if (a.name == "clbit") {
        const auto* value = std::get_if<double>(&a.value);
        if (!value || !std::isfinite(*value) || *value < 0 ||
            std::floor(*value) != *value || *value >= 1000000) {
          diag.error("measurement destination must be an integer in [0, 1000000)", op.loc);
          continue;
        }
        nextAnonymousBit = std::max(nextAnonymousBit, static_cast<std::size_t>(*value) + 1);
      }
    }
    nextAnonymousBit = std::max(nextAnonymousBit, declaredBits);
    out.numClbits = nextAnonymousBit;
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
    for (pd::ValueId v : op.operands) {
      const auto type = src.typeOf(v).kind;
      if (type == pd::TypeKind::Qubit || type == pd::TypeKind::Bit)
        sop.operands.push_back(mapValue(v));
    }
    // Copy attributes: only "angle" / "theta" / "phi" carry to spinor.*
    for (const auto& a : op.attributes) {
      if (a.name == "angle" || a.name == "theta" || a.name == "phi" || a.name == "clbit") {
        sop.attributes.push_back(sd::Attribute{a.name, a.value});
      } else if (a.name == "angle_operand" || a.name == "theta_operand" || a.name == "phi_operand") {
        const auto* index = std::get_if<double>(&a.value);
        if (!index || !std::isfinite(*index) || *index < 0 ||
            std::floor(*index) != *index || *index >= op.operands.size()) {
          diag.error("invalid symbolic gate parameter operand", op.loc); return;
        }
        const auto found = ctMap.find(op.operands[static_cast<std::size_t>(*index)].v);
        if (found == ctMap.end() || !std::isfinite(found->second)) {
          diag.error("unbound gate parameter: bind a finite angle before compiling", op.loc); return;
        }
        sop.attributes.push_back({a.name.substr(0, a.name.size() - 8), found->second});
      }
    }
    if (sk == sd::OpKind::Measure &&
        std::none_of(sop.attributes.begin(), sop.attributes.end(),
                     [](const auto& a) { return a.name == "clbit"; })) {
      sop.attributes.push_back({"clbit", static_cast<double>(nextAnonymousBit++)});
    }
    sop.loc = sd::Location{op.loc.file, op.loc.line, op.loc.column};
    sd::OpId sid = out.addOp(std::move(sop));
    sd::Op& live = out.opMut(sid);
    for (const auto& attr : live.attributes) if (attr.name == "clbit") {
      out.numClbits = std::max(out.numClbits, static_cast<std::size_t>(std::get<double>(attr.value)) + 1);
    }
    // Allocate result types matching the Spinor signature, in order.
    int qResults = 0;
    bool producesBit = false;
    if (sk == sd::OpKind::AllocQubit || sk == sd::OpKind::Reset) {
      qResults = 1;
    } else if (sk == sd::OpKind::AllocBit || sk == sd::OpKind::Measure) {
      producesBit = true;
    } else if (sk == sd::OpKind::Cx || sk == sd::OpKind::Cz ||
               sk == sd::OpKind::Swap || sk == sd::OpKind::Ecr ||
               sk == sd::OpKind::Ms || sk == sd::OpKind::Rzz || sk == sd::OpKind::Rxx) {
      qResults = 2;
    } else if (sk == sd::OpKind::Barrier || sk == sd::OpKind::GlobalPhase) {
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
    // Repeated function/loop bodies must consume the latest SSA value for
    // their slot, rather than reuse the value from the first invocation.
    for (std::size_t k = 0; k < op.operands.size() && k < outResults.size(); ++k) {
      if (src.typeOf(op.operands[k]).kind != pd::TypeKind::Qubit ||
          out.typeOf(outResults[k]).kind != sd::TypeKind::Qubit) continue;
      auto previous = live.operands[k];
      for (auto& [_, value] : vmap) if (value == previous) value = outResults[k];
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
    while (i < hi && !returning && !diag.hasErrors()) {
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
            else if (opn == "/" && b_->second != 0) r = a->second / b_->second;
            else { diag.error("unsupported or invalid compile-time arithmetic"); break; }
            if (!std::isfinite(r)) { diag.error("compile-time arithmetic must remain finite", op.loc); return; }
            for (pd::ValueId rv : op.results) ctMap[rv.v] = r;
          }
          ++i; break;
        }
        case pd::OpKind::Cmp: {
          auto lhs = ctMap.find(op.operands.at(0).v);
          auto rhs = ctMap.find(op.operands.at(1).v);
          if (lhs != ctMap.end() && rhs != ctMap.end()) {
            std::string predicate;
            for (const auto& attr : op.attributes)
              if (attr.name == "op") predicate = std::get<std::string>(attr.value);
            const double a = lhs->second, b = rhs->second;
            bool result;
            if (predicate == "==") result = a == b;
            else if (predicate == "!=") result = a != b;
            else if (predicate == "<") result = a < b;
            else if (predicate == ">") result = a > b;
            else if (predicate == "<=") result = a <= b;
            else if (predicate == ">=") result = a >= b;
            else { diag.error("unsupported comparison predicate"); break; }
            for (auto value : op.results) ctMap[value.v] = result ? 1 : 0;
          }
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
          if (std::find(callStack.begin(), callStack.end(), name) != callStack.end()) {
            diag.error("recursive call to '" + name + "' is not supported");
            return;
          }
          // Bind parameter values via vmap (qubit args) and ctMap
          // (classical args).
          const FuncRange& fr = it->second;
          if (fr.paramValues.size() != op.operands.size()) {
            diag.error("argument count mismatch for function '" + name + "'");
            return;
          }
          // Save vmap entries for params so we can restore after inlining.
          std::vector<std::pair<std::uint32_t, std::optional<sd::ValueId>>> savedV;
          auto savedCt = ctMap;
          for (std::size_t k = 0; k < fr.paramValues.size() &&
                                   k < op.operands.size(); ++k) {
            std::uint32_t pv = fr.paramValues[k].v;
            auto sv = vmap.find(pv);
            savedV.push_back({pv, sv != vmap.end() ? std::optional<sd::ValueId>{sv->second} : std::nullopt});
            if (fr.paramTypes[k].kind == pd::TypeKind::Int ||
                fr.paramTypes[k].kind == pd::TypeKind::Angle) {
              auto value = ctMap.find(op.operands[k].v);
              if (value == ctMap.end()) { diag.error("unbound classical function argument"); return; }
              if (!std::isfinite(value->second) ||
                  (fr.paramTypes[k].kind == pd::TypeKind::Int && std::floor(value->second) != value->second)) {
                diag.error("function argument must match its finite numeric parameter type", op.loc); return;
              }
              ctMap[pv] = value->second;
            } else {
              vmap[pv] = mapValue(op.operands[k]);
            }
          }
          // Inline the body. Track "return values" produced by a
          // phonon.return inside.
          std::vector<sd::ValueId> returnValues;
          callStack.push_back(name);
          auto oldSink = returnSink;
          returnSink = &returnValues;
          {
            emitRange(fr.bodyStart, fr.bodyEnd);
            if (!returning) {
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
          returnSink = oldSink;
          returning = false;
          callStack.pop_back();
          ctMap = std::move(savedCt);
          // Bind call results to the returned values, in order
          // (qubit results only).
          std::size_t rk = 0;
          for (pd::ValueId rv : op.results) {
            if (rk < returnValues.size()) {
              vmap[rv.v] = returnValues[rk++];
            }
          }
          if (rk != op.results.size()) diag.error("function returned the wrong number of values: " + name);
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
          if (!std::isfinite(lo) || !std::isfinite(hi) ||
              std::floor(lo) != lo || std::floor(hi) != hi || hi - lo > 100000 ||
              std::abs(lo) > 9007199254740991.0 || std::abs(hi) > 9007199254740991.0) {
            diag.error("for-loop requires finite integer bounds and at most 100000 iterations");
            return;
          }
          auto loInt = static_cast<std::int64_t>(lo);
          auto hiInt = static_cast<std::int64_t>(hi);
          for (auto v = loInt; v < hiInt && !returning; ++v) {
            // Source-level induction indices were resolved before SSA construction.
            // A programmatically built For repeats its fixed body while the
            // value mapping threads each slot's latest value between iterations.
            (void)v;
            if (++expandedIterations > 100000) { diag.error("static loop expansion exceeds 100000 iterations"); return; }
            emitRange(bodyStart, bodyEnd);
          }
          i = bodyEnd + 1;  // skip end_for
          break;
        }
        case pd::OpKind::EndFor:
          ++i; break;

        case pd::OpKind::If: {
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
          auto condition = op.operands.empty() ? ctMap.end() : ctMap.find(op.operands.front().v);
          if (condition == ctMap.end()) {
            auto predicate=op.operands.front();
            const auto& cmp=src.op(src.producerOf(predicate));
            pd::ValueId bit=predicate;double constant=1;std::string comparison="==";bool reverse=false;
            if(cmp.kind==pd::OpKind::Cmp){
              for(const auto& attr:cmp.attributes)if(attr.name=="op")comparison=std::get<std::string>(attr.value);
              auto left=ctMap.find(cmp.operands[0].v),right=ctMap.find(cmp.operands[1].v);
              if(right!=ctMap.end()){bit=cmp.operands[0];constant=right->second;}
              else if(left!=ctMap.end()){bit=cmp.operands[1];constant=left->second;reverse=true;}
              else {diag.error("runtime comparison requires one measured bit and one compile-time value",op.loc);return;}
            }
            auto mapped=vmap.find(bit.v);
            if(mapped==vmap.end()||out.typeOf(mapped->second)!=sd::bitType()){
              diag.error("runtime predicate is not a measured classical bit",op.loc);return;
            }
            auto evaluate=[&](double value){
              double lhs=reverse?constant:value,rhs=reverse?value:constant;
              if(comparison=="==")return lhs==rhs;if(comparison=="!=")return lhs!=rhs;
              if(comparison=="<")return lhs<rhs;if(comparison==">")return lhs>rhs;
              if(comparison=="<=")return lhs<=rhs;if(comparison==">=")return lhs>=rhs;
              throw std::runtime_error("unsupported classical predicate");
            };
            bool zero=evaluate(0),one=evaluate(1);
            if(zero==one){if(one)emitRange(bodyStart,bodyStart+thenCount);else emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
            else {
              b.beginIf(sd::classicalIndex(out,mapped->second),one,op.loc);
              emitRange(bodyStart,bodyStart+thenCount);
              if(returning){diag.error("conditional return requires explicit control-flow return lowering",op.loc);return;}
              if(elseCount){b.elseBranch(op.loc);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
              if(returning){diag.error("conditional return requires explicit control-flow return lowering",op.loc);return;}
              b.endIf(op.loc);
            }
          } else if (condition->second != 0) emitRange(bodyStart, bodyStart + thenCount);
          else emitRange(bodyStart + thenCount, bodyStart + thenCount + elseCount);
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
          if (!returnSink) {
            diag.error("return outside an inlined function is unsupported");
            return;
          }
          for (auto value : op.operands) returnSink->push_back(mapValue(value));
          returning = true;
          return;

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

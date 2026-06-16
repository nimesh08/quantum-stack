// photon/lang/cpp/lib/Lower.cpp
#include "photon/lang/Lower.h"
#include "photon/lang/Library.h"
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace photon::lang {
namespace pd = phonon::dialect;
namespace {

struct Lowerer {
  const Module& src;
  pd::Module out;
  pd::Builder b;
  Diagnostics diag;
  bool fatal = false;

  // Slot tables, scoped per-function. A QReg of size N maps to a
  // vector of N current SSA qubit ValueIds.
  std::unordered_map<std::string, std::vector<pd::ValueId>> qslots;
  std::unordered_map<std::string, std::vector<pd::ValueId>> bslots;
  std::unordered_map<std::string, pd::ValueId> classicals;
  std::unordered_map<std::string, std::int64_t> intConsts;
  bool in_def_ = false;  // true while inside a phonon.def body.

  Lowerer(const Module& m) : src(m), b(out) { out.targetAttr = m.target; }

  pd::Location loc(const Location& l) const {
    return {l.file, l.line, l.column};
  }
  void err(std::string msg, const Location& l) {
    diag.error(std::move(msg), loc(l));
    fatal = true;
  }

  // Compile-time integer evaluator for index expressions.
  std::optional<std::int64_t> foldInt(const ExprPtr& e) const {
    if (!e) return std::nullopt;
    switch (e->kind) {
      case ExprKind::IntLit: return e->int_value;
      case ExprKind::RealLit: return static_cast<std::int64_t>(e->real_value);
      case ExprKind::Ident: {
        auto it = intConsts.find(e->text);
        if (it != intConsts.end()) return it->second;
        return std::nullopt;
      }
      case ExprKind::UnaryMinus: {
        auto v = foldInt(e->children[0]);
        if (!v) return std::nullopt;
        return -*v;
      }
      case ExprKind::BinOp: {
        auto a = foldInt(e->children[0]);
        auto bv = foldInt(e->children[1]);
        if (!a || !bv) return std::nullopt;
        if (e->text == "+") return *a + *bv;
        if (e->text == "-") return *a - *bv;
        if (e->text == "*") return *a * *bv;
        if (e->text == "/" && *bv != 0) return *a / *bv;
        return std::nullopt;
      }
      default: return std::nullopt;
    }
  }

  // Compile-time real evaluator for angle expressions (handles pi).
  std::optional<double> foldReal(const ExprPtr& e) const {
    if (!e) return std::nullopt;
    switch (e->kind) {
      case ExprKind::IntLit:  return static_cast<double>(e->int_value);
      case ExprKind::RealLit: return e->real_value;
      case ExprKind::Pi:      return M_PI;
      case ExprKind::UnaryMinus: {
        auto v = foldReal(e->children[0]);
        if (!v) return std::nullopt;
        return -*v;
      }
      case ExprKind::BinOp: {
        auto a = foldReal(e->children[0]);
        auto bv = foldReal(e->children[1]);
        if (!a || !bv) return std::nullopt;
        if (e->text == "+") return *a + *bv;
        if (e->text == "-") return *a - *bv;
        if (e->text == "*") return *a * *bv;
        if (e->text == "/" && *bv != 0.0) return *a / *bv;
        return std::nullopt;
      }
      case ExprKind::Ident: {
        auto it = intConsts.find(e->text);
        if (it != intConsts.end()) return static_cast<double>(it->second);
        return std::nullopt;
      }
      default: return std::nullopt;
    }
  }

  pd::ValueId emitConstInt(std::int64_t v, pd::Location l) {
    return b.constInt(v, std::move(l));
  }

  // Apply a 1q gate by name to the slot value at qslots[name][idx].
  void apply1q(const std::string& reg, std::int64_t idx,
               const std::string& gate, const std::vector<ExprPtr>& args,
               const Location& aloc) {
    auto it = qslots.find(reg);
    if (it == qslots.end() || idx < 0 ||
        idx >= static_cast<std::int64_t>(it->second.size())) {
      err("unknown qubit slot " + reg + "[" + std::to_string(idx) + "]", aloc);
      return;
    }
    pd::ValueId q = it->second[idx];
    pd::Location L = loc(aloc);
    pd::ValueId r;
    if      (gate == "h" || gate == "hadamard") r = b.h(q, L);
    else if (gate == "x")    r = b.x(q, L);
    else if (gate == "y")    r = b.y(q, L);
    else if (gate == "z")    r = b.z(q, L);
    else if (gate == "s" || gate == "phase") r = b.s(q, L);
    else if (gate == "sdg")  r = b.sdg(q, L);
    else if (gate == "t")    r = b.t(q, L);
    else if (gate == "tdg")  r = b.tdg(q, L);
    else if (gate == "sx")   r = b.sx(q, L);
    else if (gate == "sxdg") r = b.sxdg(q, L);
    else if (gate == "rx" || gate == "ry" || gate == "rz" ||
             gate == "gpi" || gate == "gpi2") {
      if (args.empty()) {
        err(gate + " expects an angle as first argument", aloc); return;
      }
      auto angle = foldReal(args[0]);
      if (!angle) {
        err("rotation angle must fold at compile time", aloc); return;
      }
      if (gate == "rx") r = b.rx(*angle, q, L);
      else if (gate == "ry") r = b.ry(*angle, q, L);
      else if (gate == "rz") r = b.rz(*angle, q, L);
      else if (gate == "gpi")  r = b.gpi(*angle, q, L);
      else if (gate == "gpi2") r = b.gpi2(*angle, q, L);
    } else if (gate == "u1q") {
      if (args.size() < 2) {
        err("u1q expects (theta, phi, idx)", aloc); return;
      }
      auto th = foldReal(args[0]);
      auto ph = foldReal(args[1]);
      if (!th || !ph) {
        err("u1q angles must fold at compile time", aloc); return;
      }
      r = b.u1q(*th, *ph, q, L);
    } else {
      err("unsupported 1q gate '" + gate + "'", aloc);
      return;
    }
    it->second[idx] = r;
  }

  // Apply a 2q gate to slot pair (idxA, idxB). Both slots updated.
  void apply2q(const std::string& reg, std::int64_t a, std::int64_t b_,
               const std::string& gate, const std::vector<ExprPtr>& args,
               const Location& aloc) {
    auto it = qslots.find(reg);
    if (it == qslots.end()) {
      err("unknown qreg '" + reg + "'", aloc); return;
    }
    auto& slots = it->second;
    if (a < 0 || b_ < 0 ||
        a >= static_cast<std::int64_t>(slots.size()) ||
        b_ >= static_cast<std::int64_t>(slots.size())) {
      err("qubit index out of range", aloc); return;
    }
    pd::Location L = loc(aloc);
    std::pair<pd::ValueId, pd::ValueId> r;
    pd::ValueId qa = slots[a], qb = slots[b_];
    if      (gate == "cx" || gate == "cnot") r = b.cx(qa, qb, L);
    else if (gate == "cz")   r = b.cz(qa, qb, L);
    else if (gate == "swap") r = b.swap(qa, qb, L);
    else if (gate == "ecr")  r = b.ecr(qa, qb, L);
    else if (gate == "ms")   r = b.ms(qa, qb, L);
    else if (gate == "rzz") {
      if (args.empty()) { err("rzz expects angle", aloc); return; }
      auto ang = foldReal(args[0]);
      if (!ang) { err("rzz angle must fold", aloc); return; }
      r = b.rzz(*ang, qa, qb, L);
    } else {
      err("unsupported 2q gate '" + gate + "'", aloc); return;
    }
    slots[a] = r.first;
    slots[b_] = r.second;
  }

  void lowerStmt(const Stmt& s);
  void lowerBlock(const std::vector<StmtPtr>& body) {
    for (const auto& s : body) if (s) lowerStmt(*s);
  }
  void lowerFunction(const Function& f);
};

void Lowerer::lowerStmt(const Stmt& s) {
  if (fatal) return;
  pd::Location L = loc(s.loc);
  switch (s.kind) {
    case StmtKind::VarDecl: {
      if (s.decl_type.kind == TypeKind::QReg) {
        std::uint32_t n = s.decl_type.qreg_size;
        if (n == 0) { err("QReg requires a size", s.loc); return; }
        std::vector<pd::ValueId> qs;
        qs.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) qs.push_back(b.allocQubit(L));
        qslots[s.name] = std::move(qs);
        // Allocate parallel classical bit register so q.measure_int /
        // q.measure can write into it. Sized to match the QReg.
        std::vector<pd::ValueId> cs;
        cs.reserve(n);
        std::string bname = "__c_" + s.name;
        for (std::uint32_t i = 0; i < n; ++i) cs.push_back(b.allocBit(L));
        bslots[bname] = std::move(cs);
      } else if (s.decl_type.kind == TypeKind::Int) {
        if (s.init) {
          auto v = foldInt(s.init);
          if (v) {
            intConsts[s.name] = *v;
            classicals[s.name] = b.constInt(*v, L);
          } else {
            err("int initializer must fold to a literal", s.loc);
          }
        }
      } else if (s.decl_type.kind == TypeKind::Angle) {
        if (s.init) {
          auto v = foldReal(s.init);
          if (v) classicals[s.name] = b.constAngle(*v, L);
          else err("angle initializer must fold to a literal", s.loc);
        }
      }
      break;
    }
    case StmtKind::GateCall: {
      const std::string& reg = s.receiver;
      const std::string& gate = s.method;
      // Determine index args. 1q gates take one index; 2q gates take
      // two; rotations take (angle, idx) or (theta, phi, idx) for u1q.
      // Identify qubit-index args by skipping leading angle args.
      std::size_t skip = 0;
      if (gate == "rx" || gate == "ry" || gate == "rz" ||
          gate == "gpi" || gate == "gpi2") skip = 1;
      else if (gate == "u1q") skip = 2;
      else if (gate == "rzz") skip = 1;  // 2q with angle.

      // Number of qubit indices follows the gate's arity:
      auto needed = [&](const std::string& g) -> int {
        if (g == "cx" || g == "cnot" || g == "cz" || g == "swap" ||
            g == "ecr" || g == "ms" || g == "rzz") return 2;
        return 1;
      };
      int qa = needed(gate);
      if (s.args.size() < skip + static_cast<std::size_t>(qa)) {
        err("gate '" + gate + "' missing qubit index", s.loc); return;
      }
      auto idx0 = foldInt(s.args[skip]);
      if (!idx0) { err("qubit index must fold to a literal int", s.loc); return; }
      if (qa == 1) {
        std::vector<ExprPtr> rotArgs(s.args.begin(), s.args.begin() + skip);
        apply1q(reg, *idx0, gate, rotArgs, s.loc);
      } else {
        auto idx1 = foldInt(s.args[skip + 1]);
        if (!idx1) { err("qubit index must fold to a literal int", s.loc); return; }
        std::vector<ExprPtr> rotArgs(s.args.begin(), s.args.begin() + skip);
        apply2q(reg, *idx0, *idx1, gate, rotArgs, s.loc);
      }
      break;
    }
    case StmtKind::MeasureAll:
    case StmtKind::MeasureInt: {
      auto qit = qslots.find(s.receiver);
      if (qit == qslots.end()) {
        err("measure on unknown qreg '" + s.receiver + "'", s.loc); return;
      }
      std::string bname = "__c_" + s.receiver;
      auto bit = bslots.find(bname);
      auto& bits = bit->second;
      for (std::size_t i = 0; i < qit->second.size(); ++i) {
        bits[i] = b.measure(qit->second[i], L);
      }
      // Note: M2 will use this measurement to compute return values
      // for `q.measure_int()`. M1 leaves the measure ops in the IR
      // for the type checker to validate.
      break;
    }
    case StmtKind::ReturnStmt: {
      // Special-case: `return q.measure_int()` — emit per-slot measures,
      // pack them into an int, return that. This is the canonical
      // Photon return path (see Deep-Dive Part 1 §3 worked example).
      auto isMeasureIntCall = [](const Expr& e) -> std::optional<std::string> {
        if (e.kind != ExprKind::Call) return std::nullopt;
        // text is "<recv>.<method>".
        auto dot = e.text.find('.');
        if (dot == std::string::npos) return std::nullopt;
        if (e.text.substr(dot + 1) != "measure_int" &&
            e.text.substr(dot + 1) != "measure") return std::nullopt;
        return e.text.substr(0, dot);
      };
      if (s.init) {
        if (auto recv = isMeasureIntCall(*s.init)) {
          auto qit = qslots.find(*recv);
          if (qit == qslots.end()) {
            err("measure on unknown qreg '" + *recv + "'", s.loc); return;
          }
          std::vector<pd::ValueId> bits;
          for (auto qv : qit->second) bits.push_back(b.measure(qv, L));
          // Skip phonon.return at top-level (flatten mode); it's only
          // valid inside a phonon.def body.
          if (in_def_) {
            pd::ValueId rv = bits.empty() ? b.constInt(0, L) : bits.front();
            b.returnOp(std::span<const pd::ValueId>(&rv, 1), L);
          }
          return;
        }
        auto v = foldInt(s.init);
        if (v) {
          if (in_def_) {
            pd::ValueId r = b.constInt(*v, L);
            b.returnOp(std::span<const pd::ValueId>(&r, 1), L);
          }
        } else if (in_def_) {
          b.returnOp({}, L);
        }
      } else if (in_def_) {
        b.returnOp({}, L);
      }
      break;
    }
    case StmtKind::ForLoop: {
      auto lo = foldInt(s.for_lo);
      auto hi = foldInt(s.for_hi);
      if (!lo || !hi) {
        err("for-loop bounds must fold to literal ints", s.loc); return;
      }
      pd::ValueId loV = b.constInt(*lo, L);
      pd::ValueId hiV = b.constInt(*hi, L);
      auto begin = b.beginFor(s.for_var, loV, hiV, L);
      // Bind loop variable to the lo bound for in-body indexing.
      // (Phonon's M4 unroller does the same; this matches D10.)
      auto save = intConsts.find(s.for_var) != intConsts.end()
                      ? std::make_optional(intConsts[s.for_var])
                      : std::nullopt;
      intConsts[s.for_var] = *lo;
      lowerBlock(s.body);
      if (save) intConsts[s.for_var] = *save;
      else intConsts.erase(s.for_var);
      b.endFor(begin, L);
      break;
    }
    case StmtKind::IfStmt: {
      // For M1 we only support `c[i] == 1` style predicates (matches
      // Phonon's teleportation corpus). We compile to phonon.if with a
      // bit-typed predicate.
      if (!s.predicate) { err("if missing predicate", s.loc); return; }
      // Best-effort: if predicate is a comparison `c == 1`, resolve
      // c's slot. Otherwise leave a placeholder constInt(0).
      pd::ValueId pred;
      const Expr& p = *s.predicate;
      if (p.kind == ExprKind::CmpEq && p.children.size() == 2 &&
          p.children[0]->kind == ExprKind::Ident) {
        const auto& cname = p.children[0]->text;
        auto cit = classicals.find(cname);
        if (cit != classicals.end()) {
          auto rhs = foldInt(p.children[1]);
          pd::ValueId rhsV = b.constInt(rhs.value_or(0), L);
          pred = b.cmp("==", cit->second, rhsV, L);
        } else {
          pred = b.constInt(0, L);
        }
      } else {
        pred = b.constInt(0, L);
      }
      auto begin = b.beginIf(pred, L);
      lowerBlock(s.then_body);
      if (!s.else_body.empty()) {
        b.elseIf(begin);
        lowerBlock(s.else_body);
      }
      b.endIf(begin, L);
      break;
    }
    case StmtKind::Assign: {
      if (s.init) {
        auto v = foldInt(s.init);
        if (v) intConsts[s.name] = *v;
      }
      break;
    }
    case StmtKind::LibCall: {
      // Try the library expander first. If the routine matches, the
      // expander emits the right Phonon ops on `s.receiver`'s slots.
      ExpandCtx ctx;
      ctx.qslots = &qslots;
      ctx.builder = &b;
      ctx.loc = L;
      ctx.foldInt = [this](const ExprPtr& e) { return foldInt(e); };
      ctx.foldReal = [this](const ExprPtr& e) { return foldReal(e); };
      ctx.diag = &diag;
      if (expandLibrary(s.method, s.receiver, s.args, s.loc, ctx)) break;
      // Otherwise: leave a phonon.call placeholder for the platform.
      b.call(std::string("lib.") + s.method, {}, {}, L);
      break;
    }
    case StmtKind::ExprStmt:
      // No-op for M1: side-effecting plain expressions are uncommon.
      break;
  }
}

void Lowerer::lowerFunction(const Function& f) {
  pd::Location L = loc(f.loc);
  bool flatten = f.is_kernel && f.params.empty();
  if (flatten) {
    qslots.clear();
    bslots.clear();
    classicals.clear();
    intConsts.clear();
    in_def_ = false;
    lowerBlock(f.body);
    return;
  }
  in_def_ = true;
  // Translate Photon parameters into Phonon Builder::Param. QReg
  // parameters become a phonon `qubit` parameter (size lost; the
  // caller's QReg slot table threads the values).
  std::vector<pd::Builder::Param> params;
  for (const auto& p : f.params) {
    pd::Builder::Param pp;
    pp.name = p.name;
    switch (p.type.kind) {
      case TypeKind::Int:    pp.type = pd::intType();   break;
      case TypeKind::Angle:  pp.type = pd::angleType(); break;
      case TypeKind::Bit:    pp.type = pd::bitType();   break;
      case TypeKind::QReg:   pp.type = pd::qubitType(); break;
      case TypeKind::Oracle: pp.type = pd::funcType();  break;
      default:               pp.type = pd::funcType();  break;
    }
    params.push_back(std::move(pp));
  }
  auto def = b.beginDef(f.name, params, L);
  // Bind parameter values into the local tables.
  qslots.clear();
  bslots.clear();
  classicals.clear();
  intConsts.clear();
  for (std::size_t i = 0; i < f.params.size(); ++i) {
    pd::ValueId v = b.paramValue(def, i);
    if (f.params[i].type.kind == TypeKind::QReg) {
      qslots[f.params[i].name] = {v};
    } else {
      classicals[f.params[i].name] = v;
    }
  }
  lowerBlock(f.body);
  b.endDef(def, L);
  in_def_ = false;
}

}  // namespace (anonymous)

LowerResult lowerToPhonon(const Module& m) {
  Lowerer L(m);
  for (const auto& f : m.functions) {
    L.lowerFunction(f);
    if (L.fatal) break;
  }
  LowerResult r;
  r.diag = std::move(L.diag);
  bool ok = true;
  for (const auto& d : r.diag.items()) {
    if (d.severity == DiagSeverity::Error) { ok = false; break; }
  }
  if (ok) r.module = std::move(L.out);
  return r;
}

}  // namespace photon::lang

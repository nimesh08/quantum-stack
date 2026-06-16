// phonon/parser/cpp/lib/Parser.cpp
//
// Hand-written recursive-descent Phonon parser.
//
// Strict superset of the Spinor grammar — every legal `.spn` file
// is a legal Phonon program. Adds: int/angle declarations, classical
// expressions, if/else, for, while, def/call/return, assign.
//
// The parser threads quantum-register slot bindings through the
// statement stream Spinor-style (each gate write updates the slot's
// current ValueId), so the produced Phonon module is already in SSA
// shape and ready for the linear type checker (M3).

#include "phonon/parser/Parser.h"
#include "Lexer.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace phonon::parser {

namespace pd = phonon::dialect;

namespace {

struct Parser {
  std::vector<Token> toks;
  std::size_t pos = 0;
  std::string filename;

  pd::Module mod;
  pd::Builder b;
  pd::Diagnostics diag;
  bool fatal = false;

  // Quantum-register table: name -> vector of current ValueIds (one
  // per slot). A scalar `qubit q[1]` produces a 1-element vector.
  std::unordered_map<std::string, std::vector<pd::ValueId>> qreg;
  std::unordered_map<std::string, std::vector<pd::ValueId>> creg;
  std::unordered_map<std::string, pd::ValueId> classicals;  // int/angle
  // Classical scalars whose compile-time value is known (used for
  // for-loop bound resolution and qubit register sizes).
  std::unordered_map<std::string, double> ctConst;

  // Function templates, parser-side. body_start is index into the
  // token stream (the '{' after the param list). Used by call sites
  // to inline; the dialect-level `phonon.def`/`phonon.call` ops are
  // emitted regardless so M4 has structure to walk.
  struct FuncDecl {
    std::string name;
    std::vector<pd::Builder::Param> params;
    std::size_t body_start = 0;  // index of `{`
    std::size_t body_end = 0;    // index of `}`
  };
  std::unordered_map<std::string, FuncDecl> funcs;

  Parser(std::vector<Token> ts, std::string fn)
      : toks(std::move(ts)), filename(std::move(fn)), b(mod) {}

  // --- Token helpers -----------------------------------------------------

  const Token& peek(std::size_t k = 0) const {
    return toks[std::min(pos + k, toks.size() - 1)];
  }
  const Token& cur() const { return peek(0); }
  Token consume() {
    Token t = toks[pos];
    if (pos + 1 < toks.size()) ++pos;
    return t;
  }
  bool accept(Tok k) {
    if (cur().kind == k) { consume(); return true; }
    return false;
  }
  bool expect(Tok k, const char* what) {
    if (cur().kind == k) { consume(); return true; }
    err(std::string("expected ") + what + ", got '" + cur().text + "'");
    return false;
  }
  void skipNewlines() {
    while (cur().kind == Tok::Newline) consume();
  }
  void err(std::string msg) {
    pd::Location loc{filename, cur().line, cur().column};
    diag.error(std::move(msg), loc);
  }

  // --- Compile-time integer evaluator ------------------------------------
  // Returns nullopt if the expression cannot be folded.
  std::optional<double> foldExpr();
  std::optional<double> foldTerm();
  std::optional<double> foldFactor();

  // --- Parse-time expression to ValueId ----------------------------------
  pd::ValueId parseExpr();
  pd::ValueId parseTerm();
  pd::ValueId parseFactor();

  // --- Top-level ---------------------------------------------------------
  void parseProgram();

  // header
  void parseHeader();

  // statements
  void parseStmt();
  void parseDeclQubit();
  void parseDeclBit();
  void parseDeclClassical(bool isAngle);
  void parseGateStmt();
  void parseMeasureAssign(const std::string& lhsName,
                          std::optional<int> lhsIdx);
  void parseResetStmt();
  void parseBarrierStmt();
  void parseIfStmt();
  void parseForStmt();
  void parseWhileStmt();
  void parseDefStmt();
  void parseCallStmt(const std::string& name);
  void parseAssignStmt(const std::string& name);
  void parseReturnStmt();
  void parseBlock();  // expects '{', parses stmts, expects '}'

  // helpers
  std::optional<std::pair<std::string, int>> parseQubitRef();
  pd::ValueId getQubitSlot(const std::string& name, int idx,
                           const Token& at);
  void setQubitSlot(const std::string& name, int idx, pd::ValueId v);
  pd::ValueId getBitSlot(const std::string& name, int idx,
                         const Token& at);
  void setBitSlot(const std::string& name, int idx, pd::ValueId v);
};

// --- Compile-time fold ----------------------------------------------------

std::optional<double> Parser::foldFactor() {
  // Snapshot pos so we can rewind if we cannot fold.
  std::size_t save = pos;
  if (cur().kind == Tok::Minus) {
    consume();
    auto v = foldFactor();
    if (!v) { pos = save; return std::nullopt; }
    return -*v;
  }
  if (cur().kind == Tok::Pi) {
    consume();
    return M_PI;
  }
  if (cur().kind == Tok::Integer) {
    return std::stod(consume().text);
  }
  if (cur().kind == Tok::Real) {
    return std::stod(consume().text);
  }
  if (cur().kind == Tok::LParen) {
    consume();
    auto v = foldExpr();
    if (!v || !accept(Tok::RParen)) { pos = save; return std::nullopt; }
    return v;
  }
  if (cur().kind == Tok::Identifier) {
    auto it = ctConst.find(cur().text);
    if (it != ctConst.end()) {
      consume();
      return it->second;
    }
  }
  pos = save;
  return std::nullopt;
}

std::optional<double> Parser::foldTerm() {
  auto a = foldFactor();
  if (!a) return std::nullopt;
  while (cur().kind == Tok::Star || cur().kind == Tok::Slash) {
    Tok op = cur().kind; consume();
    auto b_ = foldFactor();
    if (!b_) return std::nullopt;
    if (op == Tok::Star) a = *a * *b_; else a = *a / *b_;
  }
  return a;
}

std::optional<double> Parser::foldExpr() {
  auto a = foldTerm();
  if (!a) return std::nullopt;
  while (cur().kind == Tok::Plus || cur().kind == Tok::Minus) {
    Tok op = cur().kind; consume();
    auto b_ = foldTerm();
    if (!b_) return std::nullopt;
    if (op == Tok::Plus) a = *a + *b_; else a = *a - *b_;
  }
  return a;
}

// --- Parse-time expression -----------------------------------------------
//
// Returns a ValueId of !phonon.int, !phonon.angle, or !spinor.bit.
// A literal int produces phonon.const_int; identifiers are looked up
// in `classicals` or quantum-register slots (the latter is only valid
// in a `cmp` predicate context, but the parser is permissive — the
// type checker will reject misuse).
pd::ValueId Parser::parseFactor() {
  if (cur().kind == Tok::Minus) {
    consume();
    pd::ValueId v = parseFactor();
    pd::ValueId zero = b.constInt(0);
    return b.binOp("-", zero, v);
  }
  if (cur().kind == Tok::Pi) {
    consume();
    return b.constAngle(M_PI);
  }
  if (cur().kind == Tok::Integer) {
    return b.constInt(static_cast<int64_t>(std::stoll(consume().text)));
  }
  if (cur().kind == Tok::Real) {
    return b.constAngle(std::stod(consume().text));
  }
  if (cur().kind == Tok::LParen) {
    consume();
    pd::ValueId v = parseExpr();
    expect(Tok::RParen, "')'");
    return v;
  }
  if (cur().kind == Tok::Identifier) {
    std::string name = consume().text;
    // Indexed reference: name[expr]
    if (cur().kind == Tok::LBracket) {
      consume();
      auto idxOpt = foldExpr();
      expect(Tok::RBracket, "']'");
      if (!idxOpt) {
        err("array index must be a compile-time integer");
        return b.constInt(0);
      }
      int idx = static_cast<int>(*idxOpt);
      auto qit = qreg.find(name);
      if (qit != qreg.end()) {
        return getQubitSlot(name, idx, cur());
      }
      auto cit = creg.find(name);
      if (cit != creg.end()) {
        return getBitSlot(name, idx, cur());
      }
      err("unknown indexed identifier: " + name);
      return b.constInt(0);
    }
    auto it = classicals.find(name);
    if (it != classicals.end()) return it->second;
    // Try quantum register single-slot (size 1).
    auto qit = qreg.find(name);
    if (qit != qreg.end() && qit->second.size() == 1) {
      return qit->second[0];
    }
    err("unknown identifier: " + name);
    return b.constInt(0);
  }
  err("expected expression");
  return b.constInt(0);
}

pd::ValueId Parser::parseTerm() {
  pd::ValueId a = parseFactor();
  while (cur().kind == Tok::Star || cur().kind == Tok::Slash) {
    std::string op = cur().kind == Tok::Star ? "*" : "/";
    consume();
    pd::ValueId b_ = parseFactor();
    a = b.binOp(op, a, b_);
  }
  return a;
}

pd::ValueId Parser::parseExpr() {
  pd::ValueId a = parseTerm();
  while (cur().kind == Tok::Plus || cur().kind == Tok::Minus) {
    std::string op = cur().kind == Tok::Plus ? "+" : "-";
    consume();
    pd::ValueId b_ = parseTerm();
    a = b.binOp(op, a, b_);
  }
  return a;
}

// --- Slot accessors ------------------------------------------------------

pd::ValueId Parser::getQubitSlot(const std::string& name, int idx,
                                 const Token& at) {
  auto it = qreg.find(name);
  if (it == qreg.end()) {
    diag.error("unknown qubit register: " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  if (idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    diag.error("qubit index out of range for " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  return it->second[idx];
}
void Parser::setQubitSlot(const std::string& name, int idx, pd::ValueId v) {
  qreg[name][idx] = v;
}

pd::ValueId Parser::getBitSlot(const std::string& name, int idx,
                               const Token& at) {
  auto it = creg.find(name);
  if (it == creg.end()) {
    diag.error("unknown bit register: " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  if (idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    diag.error("bit index out of range for " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  return it->second[idx];
}
void Parser::setBitSlot(const std::string& name, int idx, pd::ValueId v) {
  creg[name][idx] = v;
}

// --- Header --------------------------------------------------------------

void Parser::parseHeader() {
  skipNewlines();
  if (!expect(Tok::Target, "'target'")) { fatal = true; return; }
  if (cur().kind == Tok::Generic) {
    mod.targetAttr = "generic";
    consume();
  } else if (cur().kind == Tok::Identifier) {
    mod.targetAttr = consume().text;
  } else {
    err("expected 'generic' or device id after 'target'");
    fatal = true; return;
  }
  // Newline is optional after header (eof terminates fine too).
  if (cur().kind == Tok::Newline) consume();
}

// --- Declarations --------------------------------------------------------

void Parser::parseDeclQubit() {
  consume();  // 'qubit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt) { err("qubit register size must be a compile-time integer"); return; }
  int n = static_cast<int>(*sizeOpt);
  std::vector<pd::ValueId> slots;
  slots.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    pd::ValueId v = b.allocQubit();
    mod.setName(v, name + std::to_string(i));
    slots.push_back(v);
  }
  qreg[name] = std::move(slots);
}

void Parser::parseDeclBit() {
  consume();  // 'bit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt) { err("bit register size must be a compile-time integer"); return; }
  int n = static_cast<int>(*sizeOpt);
  std::vector<pd::ValueId> slots;
  slots.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    pd::ValueId v = b.allocBit();
    mod.setName(v, name + std::to_string(i));
    slots.push_back(v);
  }
  creg[name] = std::move(slots);
}

void Parser::parseDeclClassical(bool isAngle) {
  consume();  // 'int' or 'angle'
  if (cur().kind != Tok::Identifier) { err("expected name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::Equals, "'='")) return;
  // Try to compile-time fold first (so we can use it as a `for` bound).
  std::size_t save = pos;
  auto folded = foldExpr();
  if (folded && (cur().kind == Tok::Newline || cur().kind == Tok::Eof)) {
    ctConst[name] = *folded;
    classicals[name] = isAngle ? b.constAngle(*folded)
                                : b.constInt(static_cast<int64_t>(*folded));
  } else {
    pos = save;
    pd::ValueId v = parseExpr();
    classicals[name] = v;
  }
}

// --- Qubit reference helper ----------------------------------------------

std::optional<std::pair<std::string, int>> Parser::parseQubitRef() {
  if (cur().kind != Tok::Identifier) {
    err("expected qubit reference");
    return std::nullopt;
  }
  std::string name = consume().text;
  int idx = -1;
  if (cur().kind == Tok::LBracket) {
    consume();
    auto v = foldExpr();
    if (!expect(Tok::RBracket, "']'")) return std::nullopt;
    if (!v) { err("qubit index must be a compile-time integer"); return std::nullopt; }
    idx = static_cast<int>(*v);
  }
  return std::make_pair(std::move(name), idx);
}

// --- Gate / measure / reset / barrier ------------------------------------

void Parser::parseGateStmt() {
  Token gateTok = consume();  // GateName
  std::string g = gateTok.text;
  // Optional angle list: "(angle expr [, angle expr])"
  std::vector<pd::ValueId> angleVals;     // for parser-level expressions
  std::vector<double>      angleConsts;   // resolved compile-time
  if (cur().kind == Tok::LParen) {
    consume();
    while (cur().kind != Tok::RParen) {
      std::size_t save = pos;
      auto folded = foldExpr();
      if (folded && (cur().kind == Tok::Comma || cur().kind == Tok::RParen)) {
        angleConsts.push_back(*folded);
      } else {
        pos = save;
        pd::ValueId v = parseExpr();
        angleVals.push_back(v);
        angleConsts.push_back(0.0);  // placeholder
      }
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RParen, "')'");
  }
  // Operands: 1 or 2 qubit references, comma-separated.
  std::vector<std::pair<std::string, int>> ops;
  while (true) {
    auto qref = parseQubitRef();
    if (!qref) return;
    ops.push_back(*qref);
    if (!accept(Tok::Comma)) break;
  }

  auto applyGate = [&](const std::string& reg, int idx) -> pd::ValueId {
    pd::ValueId v = getQubitSlot(reg, idx, gateTok);
    pd::ValueId r;
    if (g == "h")        r = b.h(v);
    else if (g == "x")   r = b.x(v);
    else if (g == "y")   r = b.y(v);
    else if (g == "z")   r = b.z(v);
    else if (g == "s")   r = b.s(v);
    else if (g == "sdg") r = b.sdg(v);
    else if (g == "t")   r = b.t(v);
    else if (g == "tdg") r = b.tdg(v);
    else if (g == "sx")  r = b.sx(v);
    else if (g == "sxdg") r = b.sxdg(v);
    else if (g == "rx")  r = b.rx(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "ry")  r = b.ry(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "rz")  r = b.rz(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "gpi") r = b.gpi(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "gpi2") r = b.gpi2(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "u1q") r = b.u1q(angleConsts.size()>0?angleConsts[0]:0.0,
                                   angleConsts.size()>1?angleConsts[1]:0.0, v);
    else { err("unknown 1q gate: " + g); r = v; }
    return r;
  };

  if (ops.size() == 1) {
    auto& [reg, idx] = ops[0];
    if (idx == -1) {
      // whole register
      auto& slots = qreg[reg];
      for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i] = applyGate(reg, static_cast<int>(i));
      }
    } else {
      pd::ValueId nv = applyGate(reg, idx);
      setQubitSlot(reg, idx, nv);
    }
  } else if (ops.size() == 2) {
    auto& [ra, ia] = ops[0];
    auto& [rb, ib] = ops[1];
    // If a register has size 1 and no index was given, default to 0.
    if (ia == -1 && qreg.count(ra) && qreg[ra].size() == 1) ia = 0;
    if (ib == -1 && qreg.count(rb) && qreg[rb].size() == 1) ib = 0;
    if (ia == -1 || ib == -1) {
      err("two-qubit gate requires explicit indices on register operands");
      return;
    }
    pd::ValueId va = getQubitSlot(ra, ia, gateTok);
    pd::ValueId vb = getQubitSlot(rb, ib, gateTok);
    std::pair<pd::ValueId, pd::ValueId> r{va, vb};
    if      (g == "cx")   r = b.cx(va, vb);
    else if (g == "cz")   r = b.cz(va, vb);
    else if (g == "swap") r = b.swap(va, vb);
    else if (g == "ecr")  r = b.ecr(va, vb);
    else if (g == "ms")   r = b.ms(va, vb);
    else if (g == "rzz")  r = b.rzz(angleConsts.empty()?0.0:angleConsts[0], va, vb);
    else { err("unknown 2q gate: " + g); }
    setQubitSlot(ra, ia, r.first);
    setQubitSlot(rb, ib, r.second);
  } else {
    err("gate with " + std::to_string(ops.size()) + " operands");
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseMeasureAssign(const std::string& lhsName,
                                std::optional<int> lhsIdx) {
  // Already consumed: lhsName [`[` lhsIdx `]`] '='
  expect(Tok::Measure, "'measure'");
  auto qref = parseQubitRef();
  if (!qref) return;
  auto& [reg, idx] = *qref;
  if (idx == -1) {
    // c = measure q  → for each slot pair
    auto qit = qreg.find(reg);
    auto cit = creg.find(lhsName);
    if (qit == qreg.end() || cit == creg.end()) {
      err("unknown register in 'measure' statement");
      return;
    }
    if (qit->second.size() != cit->second.size()) {
      err("register size mismatch in 'measure' assignment");
      return;
    }
    for (std::size_t i = 0; i < qit->second.size(); ++i) {
      pd::ValueId vbit = b.measure(qit->second[i]);
      cit->second[i] = vbit;
    }
  } else {
    pd::ValueId vq = getQubitSlot(reg, idx, cur());
    pd::ValueId vbit = b.measure(vq);
    if (lhsIdx) {
      setBitSlot(lhsName, *lhsIdx, vbit);
    } else {
      // single-bit register? only valid if creg[lhsName].size() == 1
      auto cit = creg.find(lhsName);
      if (cit == creg.end() || cit->second.size() != 1) {
        err("expected indexed lhs in 'measure' assignment");
        return;
      }
      cit->second[0] = vbit;
    }
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseResetStmt() {
  consume();  // 'reset'
  auto qref = parseQubitRef();
  if (!qref) return;
  auto& [reg, idx] = *qref;
  if (idx == -1) {
    auto& slots = qreg[reg];
    for (std::size_t i = 0; i < slots.size(); ++i) {
      slots[i] = b.reset(slots[i]);
    }
  } else {
    pd::ValueId nv = b.reset(getQubitSlot(reg, idx, cur()));
    setQubitSlot(reg, idx, nv);
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseBarrierStmt() {
  consume();  // 'barrier'
  std::vector<pd::ValueId> qs;
  while (true) {
    auto qref = parseQubitRef();
    if (!qref) return;
    auto& [reg, idx] = *qref;
    if (idx == -1) {
      for (auto v : qreg[reg]) qs.push_back(v);
    } else {
      qs.push_back(getQubitSlot(reg, idx, cur()));
    }
    if (!accept(Tok::Comma)) break;
  }
  b.barrier(std::span<const pd::ValueId>(qs.data(), qs.size()));
  if (cur().kind == Tok::Newline) consume();
}

// --- Control flow --------------------------------------------------------

void Parser::parseIfStmt() {
  consume();
  expect(Tok::LParen, "'('");
  pd::ValueId lhs = parseExpr();
  std::string cmpOp = "==";
  if (cur().kind == Tok::EqEq) { cmpOp = "=="; consume(); }
  else if (cur().kind == Tok::NotEq) { cmpOp = "!="; consume(); }
  else if (cur().kind == Tok::Lt) { cmpOp = "<"; consume(); }
  else if (cur().kind == Tok::Gt) { cmpOp = ">"; consume(); }
  else if (cur().kind == Tok::Le) { cmpOp = "<="; consume(); }
  else if (cur().kind == Tok::Ge) { cmpOp = ">="; consume(); }
  pd::ValueId rhs = parseExpr();
  expect(Tok::RParen, "')'");
  pd::ValueId pred = b.cmp(cmpOp, lhs, rhs);
  pd::OpId ifId = b.beginIf(pred);
  parseBlock();
  if (cur().kind == Tok::Else) {
    consume();
    b.elseIf(ifId);
    parseBlock();
  }
  b.endIf(ifId);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseForStmt() {
  consume();
  if (cur().kind != Tok::Identifier) { err("expected loop variable"); return; }
  std::string var = consume().text;
  if (!expect(Tok::In, "'in'")) return;
  auto loOpt = foldExpr();
  if (!expect(Tok::DotDot, "'..'")) return;
  auto hiOpt = foldExpr();
  if (!loOpt || !hiOpt) {
    err("for-loop bounds must be compile-time integers"); return;
  }
  pd::ValueId loV = b.constInt(static_cast<int64_t>(*loOpt));
  pd::ValueId hiV = b.constInt(static_cast<int64_t>(*hiOpt));
  pd::OpId fid = b.beginFor(var, loV, hiV);
  ctConst[var] = *loOpt;
  classicals[var] = loV;
  parseBlock();
  ctConst.erase(var);
  classicals.erase(var);
  b.endFor(fid);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseWhileStmt() {
  consume();
  expect(Tok::LParen, "'('");
  pd::ValueId lhs = parseExpr();
  std::string cmpOp = "==";
  if (cur().kind == Tok::EqEq) { cmpOp = "=="; consume(); }
  else if (cur().kind == Tok::NotEq) { cmpOp = "!="; consume(); }
  else if (cur().kind == Tok::Lt) { cmpOp = "<"; consume(); }
  else if (cur().kind == Tok::Gt) { cmpOp = ">"; consume(); }
  else if (cur().kind == Tok::Le) { cmpOp = "<="; consume(); }
  else if (cur().kind == Tok::Ge) { cmpOp = ">="; consume(); }
  pd::ValueId rhs = parseExpr();
  expect(Tok::RParen, "')'");
  pd::ValueId pred = b.cmp(cmpOp, lhs, rhs);
  pd::OpId wid = b.beginWhile(pred);
  parseBlock();
  b.endWhile(wid);
  if (cur().kind == Tok::Newline) consume();
}

// --- Function definition / call ------------------------------------------

void Parser::parseDefStmt() {
  consume();
  if (cur().kind != Tok::Identifier) { err("expected function name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LParen, "'('")) return;
  std::vector<pd::Builder::Param> params;
  while (cur().kind != Tok::RParen) {
    pd::Type ty = pd::qubitType();
    if      (cur().kind == Tok::Qubit) { ty = pd::qubitType(); consume(); }
    else if (cur().kind == Tok::Bit)   { ty = pd::bitType();   consume(); }
    else if (cur().kind == Tok::Int)   { ty = pd::intType();   consume(); }
    else if (cur().kind == Tok::Angle) { ty = pd::angleType(); consume(); }
    else { err("expected parameter type (qubit/bit/int/angle)"); return; }
    if (cur().kind != Tok::Identifier) { err("expected parameter name"); return; }
    std::string pname = consume().text;
    params.push_back({ty, pname});
    if (!accept(Tok::Comma)) break;
  }
  expect(Tok::RParen, "')'");
  pd::OpId defId = b.beginDef(name, std::span<const pd::Builder::Param>(params.data(), params.size()));
  for (std::size_t i = 0; i < params.size(); ++i) {
    pd::ValueId pv = b.paramValue(defId, i);
    if (params[i].type.kind == pd::TypeKind::Qubit) qreg[params[i].name] = {pv};
    else if (params[i].type.kind == pd::TypeKind::Bit) creg[params[i].name] = {pv};
    else classicals[params[i].name] = pv;
  }
  parseBlock();
  for (const auto& p : params) {
    qreg.erase(p.name); creg.erase(p.name); classicals.erase(p.name);
  }
  b.endDef(defId);
  FuncDecl fd; fd.name = name; fd.params = std::move(params);
  funcs[fd.name] = std::move(fd);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseCallStmt(const std::string& name) {
  expect(Tok::LParen, "'('");
  std::vector<pd::ValueId> args;
  std::vector<std::pair<std::string, int>> qrefs;
  while (cur().kind != Tok::RParen) {
    if (cur().kind == Tok::Identifier && qreg.count(cur().text)) {
      auto qref = parseQubitRef();
      if (!qref) return;
      auto& [reg, idx] = *qref;
      int realIdx = idx == -1 ? 0 : idx;
      args.push_back(getQubitSlot(reg, realIdx, cur()));
      qrefs.push_back({reg, realIdx});
      if (!accept(Tok::Comma)) break;
      continue;
    }
    pd::ValueId v = parseExpr();
    args.push_back(v);
    qrefs.push_back({"", -1});
    if (!accept(Tok::Comma)) break;
  }
  expect(Tok::RParen, "')'");
  auto fit = funcs.find(name);
  std::vector<pd::Type> rts;
  if (fit != funcs.end()) {
    for (const auto& p : fit->second.params) {
      if (p.type.kind == pd::TypeKind::Qubit) rts.push_back(pd::qubitType());
    }
  }
  std::vector<pd::ValueId> ret = b.call(name,
      std::span<const pd::ValueId>(args.data(), args.size()),
      std::span<const pd::Type>(rts.data(), rts.size()));
  std::size_t k = 0;
  for (std::size_t i = 0; i < qrefs.size() && k < ret.size(); ++i) {
    if (!qrefs[i].first.empty()) {
      setQubitSlot(qrefs[i].first, qrefs[i].second, ret[k]);
      ++k;
    }
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseAssignStmt(const std::string& name) {
  expect(Tok::Equals, "'='");
  pd::ValueId v = parseExpr();
  classicals[name] = v;
  b.assign(name, v);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseReturnStmt() {
  consume();
  std::vector<pd::ValueId> vs;
  while (cur().kind != Tok::Newline && cur().kind != Tok::Eof &&
         cur().kind != Tok::RBrace) {
    if (cur().kind == Tok::Identifier && qreg.count(cur().text)) {
      auto qref = parseQubitRef();
      if (!qref) return;
      auto& [reg, idx] = *qref;
      vs.push_back(getQubitSlot(reg, idx == -1 ? 0 : idx, cur()));
    } else {
      vs.push_back(parseExpr());
    }
    if (!accept(Tok::Comma)) break;
  }
  b.returnOp(std::span<const pd::ValueId>(vs.data(), vs.size()));
  if (cur().kind == Tok::Newline) consume();
}

// --- Statement dispatch + block ------------------------------------------

void Parser::parseStmt() {
  skipNewlines();
  switch (cur().kind) {
    case Tok::Qubit:    parseDeclQubit(); return;
    case Tok::Bit:      parseDeclBit();   return;
    case Tok::Int:      parseDeclClassical(false); return;
    case Tok::Angle:    parseDeclClassical(true);  return;
    case Tok::GateName: parseGateStmt(); return;
    case Tok::Reset:    parseResetStmt(); return;
    case Tok::Barrier:  parseBarrierStmt(); return;
    case Tok::If:       parseIfStmt(); return;
    case Tok::For:      parseForStmt(); return;
    case Tok::While:    parseWhileStmt(); return;
    case Tok::Def:      parseDefStmt(); return;
    case Tok::Return:   parseReturnStmt(); return;
    case Tok::Identifier: {
      Token nameTok = consume();
      std::string name = nameTok.text;
      if (cur().kind == Tok::LBracket) {
        consume();
        auto idxOpt = foldExpr();
        if (!expect(Tok::RBracket, "']'")) return;
        if (!idxOpt) { err("index must be compile-time integer"); return; }
        if (cur().kind == Tok::Equals) {
          consume();
          if (cur().kind == Tok::Measure) {
            consume();
            auto qref = parseQubitRef();
            if (!qref) return;
            auto& [reg, idx2] = *qref;
            pd::ValueId vq = getQubitSlot(reg, idx2 == -1 ? 0 : idx2, cur());
            pd::ValueId vbit = b.measure(vq);
            setBitSlot(name, static_cast<int>(*idxOpt), vbit);
            if (cur().kind == Tok::Newline) consume();
            return;
          }
          err("only 'measure' allowed on rhs of indexed assignment");
          return;
        }
        err("expected '=' after indexed lhs");
        return;
      }
      if (cur().kind == Tok::LParen) {
        parseCallStmt(name); return;
      }
      if (cur().kind == Tok::Equals) {
        consume();
        if (cur().kind == Tok::Measure) {
          consume();
          auto qref = parseQubitRef();
          if (!qref) return;
          auto& [reg, idx] = *qref;
          if (idx == -1) {
            auto qit = qreg.find(reg);
            auto cit = creg.find(name);
            if (qit == qreg.end() || cit == creg.end()) {
              err("unknown register in 'measure' statement"); return;
            }
            if (qit->second.size() != cit->second.size()) {
              err("register size mismatch in 'measure' assignment"); return;
            }
            for (std::size_t i = 0; i < qit->second.size(); ++i) {
              cit->second[i] = b.measure(qit->second[i]);
            }
          } else {
            pd::ValueId vq = getQubitSlot(reg, idx, cur());
            pd::ValueId vbit = b.measure(vq);
            auto cit = creg.find(name);
            if (cit == creg.end() || cit->second.size() != 1) {
              err("expected single-bit register on lhs"); return;
            }
            cit->second[0] = vbit;
          }
          if (cur().kind == Tok::Newline) consume();
          return;
        }
        pd::ValueId v = parseExpr();
        classicals[name] = v;
        b.assign(name, v);
        if (cur().kind == Tok::Newline) consume();
        return;
      }
      err("unexpected statement starting with identifier '" + name + "'");
      while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) consume();
      return;
    }
    case Tok::Newline: consume(); return;
    case Tok::RBrace:  return;
    case Tok::Eof:     return;
    default:
      err("unexpected token '" + cur().text + "'");
      while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) consume();
      return;
  }
}

void Parser::parseBlock() {
  if (!expect(Tok::LBrace, "'{'")) return;
  skipNewlines();
  while (cur().kind != Tok::RBrace && cur().kind != Tok::Eof) {
    if (fatal) return;
    parseStmt();
    skipNewlines();
  }
  expect(Tok::RBrace, "'}'");
}

void Parser::parseProgram() {
  parseHeader();
  while (cur().kind != Tok::Eof && !fatal) {
    parseStmt();
    skipNewlines();
  }
}

}  // namespace

ParseResult parse(std::string_view text, std::string_view filename) {
  Lexer lex(text);
  auto toks = lex.tokenize();
  Parser p(std::move(toks), std::string(filename));
  p.parseProgram();
  ParseResult r;
  r.diag = std::move(p.diag);
  if (!r.diag.hasErrors()) r.module = std::move(p.mod);
  return r;
}

}  // namespace phonon::parser

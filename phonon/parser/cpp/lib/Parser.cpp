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
#include <algorithm>
#include <numbers>
#include <cstdint>
#include <limits>
#include <memory>
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

  // Captures and scalar register parameters refer to the same mutable SSA
  // slot. Copying a scope keeps those references, while rebinding a local
  // name creates a fresh register and cannot replace the caller's binding.
  struct Slot {
    std::shared_ptr<pd::ValueId> value;
    explicit Slot(pd::ValueId v) : value(std::make_shared<pd::ValueId>(v)) {}
    operator pd::ValueId() const { return *value; }
    Slot& operator=(pd::ValueId v) { *value = v; return *this; }
  };
  struct Register {
    std::vector<Slot> slots;
    Register() = default;
    Register(std::initializer_list<pd::ValueId> values) {
      for (auto value : values) slots.emplace_back(value);
    }
    Register(std::vector<pd::ValueId> values) {
      for (auto value : values) slots.emplace_back(value);
    }
    std::size_t size() const { return slots.size(); }
    Slot& operator[](std::size_t i) { return slots[i]; }
    pd::ValueId operator[](std::size_t i) const { return slots[i]; }
    auto begin() const { return slots.begin(); }
    auto end() const { return slots.end(); }
    Register clone() const {
      Register copy;
      for (const auto& slot : slots) copy.slots.emplace_back(pd::ValueId(slot));
      return copy;
    }
    static Register alias(const Register& source, std::size_t i) {
      Register result;
      result.slots.push_back(source.slots[i]);
      return result;
    }
  };
  using Registers = std::unordered_map<std::string, Register>;
  Registers qreg;
  Registers creg;
  std::unordered_map<std::string, pd::ValueId> classicals;  // int/angle
  // Classical scalars whose compile-time value is known (used for
  // for-loop bound resolution and qubit register sizes).
  std::unordered_map<std::string, double> ctConst;
  struct ScalarBinding {
    pd::ValueId value;
    std::optional<double> constant;
  };
  using ScalarBindings = std::unordered_map<std::string, std::shared_ptr<ScalarBinding>>;
  ScalarBindings scalarBindings;
  void recordScalar(const std::string& name, bool declaration = false) {
    if (!classicals.count(name)) { scalarBindings.erase(name); return; }
    if (declaration || !scalarBindings.count(name))
      scalarBindings[name] = std::make_shared<ScalarBinding>();
    auto& binding = *scalarBindings.at(name);
    binding.value = classicals.at(name);
    binding.constant = ctConst.count(name) ? std::optional<double>{ctConst.at(name)} : std::nullopt;
  }
  std::unordered_map<std::string, std::vector<std::size_t>> bitTargets;
  std::size_t nextBit = 0;
  std::size_t expandedIterations = 0;
  std::size_t expandedCalls = 0;
  std::size_t runtimeDepth = 0;

  // Numeric parameters must be bound before register indices and static
  // loops are resolved. Retain these bodies as lexical source templates;
  // qubit-only helpers without captures keep structured Def/Call IR.
  struct FuncDecl {
    std::string name;
    std::vector<pd::Builder::Param> params;
    std::size_t body_start = 0;  // index of `{`
    std::size_t body_end = 0;    // index of `}`
    bool specialize = false;
    Registers capturedQreg;
    Registers capturedCreg;
    ScalarBindings capturedScalars;
    std::unordered_map<std::string, std::vector<std::size_t>> capturedBitTargets;
  };
  std::unordered_map<std::string, FuncDecl> funcs;
  struct InlineFrame {
    std::string name;
    std::size_t runtimeDepth;
    bool returned = false;
    std::vector<pd::ValueId> values;
  };
  std::vector<InlineFrame> inlineFrames;
  bool inlineReturned() const { return !inlineFrames.empty() && inlineFrames.back().returned; }

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
  void skipBlock();

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
    return std::numbers::pi;
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
    if (op == Tok::Slash && *b_ == 0) { err("division by zero"); return std::nullopt; }
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
    return b.constAngle(std::numbers::pi);
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
      if (!idxOpt || !std::isfinite(*idxOpt) || std::floor(*idxOpt) != *idxOpt ||
          *idxOpt < 0 || *idxOpt > std::numeric_limits<int>::max()) {
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
    auto cit = creg.find(name);
    if (cit != creg.end() && cit->second.size() == 1) return cit->second[0];
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
  auto it = qreg.find(name);
  if (it != qreg.end() && idx >= 0 && static_cast<std::size_t>(idx) < it->second.size())
    it->second[idx] = v;
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
  auto it = creg.find(name);
  if (it == creg.end() || idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    err("bit index out of range for " + name);
    return;
  }
  it->second[idx] = v;
  auto target = bitTargets.find(name);
  if (target != bitTargets.end()) {
    mod.opMut(mod.producerOf(v)).attributes.push_back(
        {"clbit", static_cast<double>(target->second[idx])});
  }
}

// --- Header --------------------------------------------------------------

void Parser::parseHeader() {
  skipNewlines();
  if (!expect(Tok::Target, "'target'")) { fatal = true; return; }
  if (cur().kind == Tok::Generic) {
    mod.targetAttr = "generic";
    consume();
  } else if (cur().kind == Tok::Identifier || cur().kind == Tok::String) {
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
  if (runtimeDepth) {
    err("runtime branches cannot allocate or redeclare quantum registers; declare the register before the branch");
    fatal = true; return;
  }
  consume();  // 'qubit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt || !std::isfinite(*sizeOpt) || std::floor(*sizeOpt) != *sizeOpt ||
      *sizeOpt <= 0 || *sizeOpt > 1000000) {
    err("qubit register size must be a positive compile-time integer up to 1000000"); return;
  }
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
  if (runtimeDepth) {
    err("runtime branches cannot allocate or redeclare classical registers; declare the register before the branch");
    fatal = true; return;
  }
  consume();  // 'bit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt || !std::isfinite(*sizeOpt) || std::floor(*sizeOpt) != *sizeOpt ||
      *sizeOpt <= 0 || *sizeOpt > 1000000) {
    err("bit register size must be a positive compile-time integer up to 1000000"); return;
  }
  int n = static_cast<int>(*sizeOpt);
  std::vector<pd::ValueId> slots;
  slots.reserve(static_cast<std::size_t>(n));
  std::vector<std::size_t> targets;
  for (int i = 0; i < n; ++i) {
    pd::ValueId v = b.allocBit();
    targets.push_back(nextBit++);
    mod.opMut(mod.producerOf(v)).attributes.push_back({"clbit", static_cast<double>(targets.back())});
    mod.setName(v, name + std::to_string(i));
    slots.push_back(v);
  }
  creg[name] = std::move(slots);
  bitTargets[name] = std::move(targets);
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
    if (!std::isfinite(*folded) || (!isAngle && std::floor(*folded) != *folded)) {
      err("classical initializer must match its finite numeric type"); return;
    }
    ctConst[name] = *folded;
    classicals[name] = isAngle ? b.constAngle(*folded)
                                : b.constInt(static_cast<int64_t>(*folded));
  } else {
    ctConst.erase(name);
    pos = save;
    pd::ValueId v = parseExpr();
    if (mod.typeOf(v).kind == pd::TypeKind::Bit) {
      err("copying measured data into a numeric scalar is unsupported; use a separate measurement destination instead of a saved scalar alias");
      return;
    }
    classicals[name] = v;
  }
  recordScalar(name, true);
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
    if (!v || !std::isfinite(*v) || std::floor(*v) != *v ||
        *v < 0 || *v > std::numeric_limits<int>::max()) {
      err("qubit index must be a non-negative compile-time integer"); return std::nullopt;
    }
    idx = static_cast<int>(*v);
  }
  return std::make_pair(std::move(name), idx);
}

// --- Gate / measure / reset / barrier ------------------------------------

void Parser::parseGateStmt() {
  Token gateTok = consume();  // GateName
  std::string g = gateTok.text;
  // Optional angle list: "(angle expr [, angle expr])"
  std::vector<std::optional<pd::ValueId>> angleVals;
  std::vector<double>      angleConsts;   // resolved compile-time
  if (cur().kind == Tok::LParen) {
    consume();
    while (cur().kind != Tok::RParen) {
      std::size_t save = pos;
      auto folded = foldExpr();
      if (folded && (cur().kind == Tok::Comma || cur().kind == Tok::RParen)) {
        angleConsts.push_back(*folded);
        angleVals.push_back(std::nullopt);
      } else {
        pos = save;
        pd::ValueId v = parseExpr();
        angleVals.push_back(v);
        angleConsts.push_back(0.0);
        const auto type = mod.typeOf(v).kind;
        if (type != pd::TypeKind::Int && type != pd::TypeKind::Angle)
          err("gate parameter must be a compile-time numeric expression");
      }
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RParen, "')'");
  }
  const std::size_t expectedAngles = g == "u1q" ? 2 :
      (g == "rx" || g == "ry" || g == "rz" || g == "gpi" ||
       g == "gpi2" || g == "rzz" || g == "rxx" || g == "gphase") ? 1 : 0;
  if (angleConsts.size() != expectedAngles) {
    err("gate '" + g + "' has missing, extra, or unbound angle parameters");
    return;
  }
  for (double angle : angleConsts) if (!std::isfinite(angle)) {
    err("gate angle must be finite"); return;
  }
  auto bindAngles = [&](pd::OpId id) {
    auto& operation = mod.opMut(id);
    for (std::size_t i = 0; i < angleVals.size(); ++i) if (angleVals[i]) {
      const std::string name = g == "u1q" ? (i == 0 ? "theta" : "phi") : "angle";
      std::erase_if(operation.attributes, [&](const auto& a) { return a.name == name; });
      operation.attributes.push_back({name + "_operand", static_cast<double>(operation.operands.size())});
      operation.operands.push_back(*angleVals[i]);
    }
  };
  if (g == "gphase") {
    if (cur().kind != Tok::Newline && cur().kind != Tok::Eof && cur().kind != Tok::RBrace) {
      err("gphase takes no qubit operands"); return;
    }
    b.globalPhase(angleConsts.at(0));
    bindAngles(pd::OpId{static_cast<std::uint32_t>(mod.numOps() - 1)});
    return;
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
    if (!diag.hasErrors()) bindAngles(mod.producerOf(r));
    return r;
  };

  if (ops.size() == 1) {
    auto& [reg, idx] = ops[0];
    if (idx == -1) {
      // whole register
      if (!qreg.count(reg)) { err("unknown qubit register: " + reg); return; }
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
    if (ra == rb && ia == ib) { err("two-qubit gate requires distinct qubits"); return; }
    std::pair<pd::ValueId, pd::ValueId> r{va, vb};
    if      (g == "cx")   r = b.cx(va, vb);
    else if (g == "cz")   r = b.cz(va, vb);
    else if (g == "swap") r = b.swap(va, vb);
    else if (g == "ecr")  r = b.ecr(va, vb);
    else if (g == "ms")   r = b.ms(va, vb);
    else if (g == "rzz")  r = b.rzz(angleConsts[0], va, vb);
    else if (g == "rxx")  r = b.rxx(angleConsts[0], va, vb);
    else { err("unknown 2q gate: " + g); }
    if (!diag.hasErrors()) bindAngles(mod.producerOf(r.first));
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
      setBitSlot(lhsName, static_cast<int>(i), vbit);
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
      setBitSlot(lhsName, 0, vbit);
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
    if (!qreg.count(reg)) { err("reset on unknown qubit register: " + reg); return; }
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
      if (!qreg.count(reg)) { err("barrier on unknown qubit register: " + reg); return; }
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
  // Resolve static conditions before constructing SSA so the untaken branch
  // cannot change qubit bindings or leak a value into the following code.
  const auto conditionStart = pos;
  auto foldedLeft = foldExpr();
  const auto comparison = cur().kind;
  if (foldedLeft && (comparison == Tok::EqEq || comparison == Tok::NotEq ||
      comparison == Tok::Lt || comparison == Tok::Gt ||
      comparison == Tok::Le || comparison == Tok::Ge)) {
    consume();
    auto foldedRight = foldExpr();
    if (foldedRight && accept(Tok::RParen)) {
      bool takeThen = comparison == Tok::EqEq ? *foldedLeft == *foldedRight :
          comparison == Tok::NotEq ? *foldedLeft != *foldedRight :
          comparison == Tok::Lt ? *foldedLeft < *foldedRight :
          comparison == Tok::Gt ? *foldedLeft > *foldedRight :
          comparison == Tok::Le ? *foldedLeft <= *foldedRight : *foldedLeft >= *foldedRight;
      if (takeThen) parseBlock(); else skipBlock();
      skipNewlines();
      if (accept(Tok::Else)) {
        if (takeThen) skipBlock(); else parseBlock();
      }
      return;
    }
  }
  pos = conditionStart;
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
  auto staticBefore=classicals;auto qBefore=qreg;auto cBefore=creg;
  ++runtimeDepth;
  parseBlock();
  skipNewlines();
  if (cur().kind == Tok::Else) {
    consume();
    b.elseIf(ifId);
    parseBlock();
  }
  --runtimeDepth;
  if(classicals!=staticBefore||qreg.size()!=qBefore.size()||creg.size()!=cBefore.size())
    err("runtime branches cannot declare registers or change compile-time scalar bindings");
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
  if (!loOpt || !hiOpt || !std::isfinite(*loOpt) || !std::isfinite(*hiOpt) ||
      std::floor(*loOpt) != *loOpt || std::floor(*hiOpt) != *hiOpt ||
      std::abs(*loOpt) > 9007199254740991.0 || std::abs(*hiOpt) > 9007199254740991.0) {
    err("for-loop bounds must be compile-time integers"); return;
  }
  skipNewlines();
  const auto bodyStart = pos;
  skipBlock();
  const auto afterBody = pos;
  if (diag.hasErrors()) return;
  const auto oldConst = ctConst.find(var) == ctConst.end() ? std::optional<double>{} : ctConst[var];
  const auto oldValue = classicals.find(var) == classicals.end() ? std::optional<pd::ValueId>{} : classicals[var];
  for (auto value = static_cast<std::int64_t>(*loOpt);
       value < static_cast<std::int64_t>(*hiOpt) && !diag.hasErrors() && !inlineReturned(); ++value) {
    if (++expandedIterations > 100000) { err("static loop expansion exceeds 100000 iterations"); break; }
    ctConst[var] = static_cast<double>(value);
    classicals[var] = b.constInt(value);
    recordScalar(var);
    pos = bodyStart;
    parseBlock();
  }
  pos = afterBody;
  if (oldConst) ctConst[var] = *oldConst; else ctConst.erase(var);
  if (oldValue) classicals[var] = *oldValue; else classicals.erase(var);
  recordScalar(var);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::skipBlock() {
  skipNewlines();
  if (!expect(Tok::LBrace, "'{'") ) return;
  std::size_t depth = 1;
  while (depth && cur().kind != Tok::Eof) {
    auto token = consume();
    if (token.kind == Tok::LBrace) ++depth;
    else if (token.kind == Tok::RBrace) --depth;
  }
  if (depth) err("unterminated block");
}

void Parser::parseWhileStmt() {
  consume();
  if (!expect(Tok::LParen, "'('")) return;
  const auto conditionStart = pos;
  auto condition = [&]() -> std::optional<bool> {
    pos = conditionStart;
    auto left = foldExpr();
    const auto comparison = consume().kind;
    auto right = foldExpr();
    if (!left || !right || !std::isfinite(*left) || !std::isfinite(*right) ||
        !accept(Tok::RParen)) {
      err("while requires a finite compile-time condition; measured-bit while is unsupported");
      return std::nullopt;
    }
    switch (comparison) {
      case Tok::EqEq: return *left == *right;
      case Tok::NotEq: return *left != *right;
      case Tok::Lt: return *left < *right;
      case Tok::Gt: return *left > *right;
      case Tok::Le: return *left <= *right;
      case Tok::Ge: return *left >= *right;
      default: err("while requires a comparison predicate"); return std::nullopt;
    }
  };
  auto take = condition();
  if (!take) return;
  skipNewlines();
  const auto bodyStart = pos;
  skipBlock();
  const auto afterBody = pos;
  while (*take && !diag.hasErrors() && !inlineReturned()) {
    if (++expandedIterations > 100000) {
      err("static loop expansion exceeds 100000 iterations; while termination was not established"); break;
    }
    pos = bodyStart;
    parseBlock();
    if (diag.hasErrors() || inlineReturned()) break;
    take = condition();
    if (!take) break;
  }
  pos = afterBody;
  if (cur().kind == Tok::Newline) consume();
}

// --- Function definition / call ------------------------------------------

void Parser::parseDefStmt() {
  if (!inlineFrames.empty()) {
    err("nested function definitions are unsupported; define helpers at module scope");
    fatal = true;
    return;
  }
  consume();
  if (cur().kind != Tok::Identifier) { err("expected function name"); return; }
  std::string name = consume().text;
  if (funcs.count(name)) { err("function cannot be redefined: " + name); return; }
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
    if (std::any_of(params.begin(), params.end(), [&](const auto& p) { return p.name == pname; })) {
      err("duplicate function parameter: " + pname); return;
    }
    params.push_back({ty, pname});
    if (!accept(Tok::Comma)) break;
  }
  if (!expect(Tok::RParen, "')'")) return;
  skipNewlines();
  FuncDecl fd;
  fd.name = name;
  fd.params = params;
  fd.body_start = pos;
  fd.specialize = std::any_of(params.begin(), params.end(), [](const auto& p) {
    return p.type.kind != pd::TypeKind::Qubit;
  });
  skipBlock();
  fd.body_end = pos;
  pos = fd.body_start;
  // A wrapper around a template and a function using lexical variables
  // also need call-time expansion. Pure qubit helpers retain Def/Call IR.
  for (auto token = fd.body_start; token < fd.body_end; ++token) {
    if (toks[token].kind != Tok::Identifier) continue;
    const auto& symbol = toks[token].text;
    const bool parameter = std::any_of(params.begin(), params.end(), [&](const auto& p) { return p.name == symbol; });
    if (!parameter && (qreg.count(symbol) || creg.count(symbol) || scalarBindings.count(symbol)))
      fd.specialize = true;
    const auto helper = funcs.find(symbol);
    if (helper != funcs.end() && helper->second.specialize && toks[token + 1].kind == Tok::LParen)
      fd.specialize = true;
  }
  if (fd.specialize) {
    fd.capturedQreg = qreg;
    fd.capturedCreg = creg;
    fd.capturedScalars = scalarBindings;
    fd.capturedBitTargets = bitTargets;
    pos = fd.body_end;
    funcs.emplace(name, std::move(fd));
    if (cur().kind == Tok::Newline) consume();
    return;
  }
  const auto savedQreg = qreg;
  const auto savedCreg = creg;
  const auto savedClassicals = classicals;
  const auto savedConstants = ctConst;
  const auto savedScalars = scalarBindings;
  const auto savedBitTargets = bitTargets;
  // Merely defining a structured function must not update captured slots.
  for (auto& [_, reg] : qreg) reg = reg.clone();
  for (auto& [_, reg] : creg) reg = reg.clone();
  for (auto& [_, binding] : scalarBindings) binding = std::make_shared<ScalarBinding>(*binding);
  pd::OpId defId = b.beginDef(name, std::span<const pd::Builder::Param>(params.data(), params.size()));
  for (std::size_t i = 0; i < params.size(); ++i) {
    pd::ValueId pv = b.paramValue(defId, i);
    qreg.erase(params[i].name); creg.erase(params[i].name);
    classicals.erase(params[i].name); ctConst.erase(params[i].name);
    bitTargets.erase(params[i].name);
    scalarBindings.erase(params[i].name);
    if (params[i].type.kind == pd::TypeKind::Qubit) qreg[params[i].name] = {pv};
    else if (params[i].type.kind == pd::TypeKind::Bit) creg[params[i].name] = {pv};
    else classicals[params[i].name] = pv;
  }
  parseBlock();
  qreg = savedQreg; creg = savedCreg; classicals = savedClassicals;
  ctConst = savedConstants; bitTargets = savedBitTargets;
  scalarBindings = savedScalars;
  b.endDef(defId);
  fd.body_end = pos;
  funcs[fd.name] = std::move(fd);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseCallStmt(const std::string& name) {
  if (!expect(Tok::LParen, "'('")) return;
  std::vector<pd::ValueId> args;
  std::vector<std::pair<std::string, int>> refs;
  std::vector<std::optional<double>> constants;
  while (cur().kind != Tok::RParen && cur().kind != Tok::Eof) {
    if (cur().kind == Tok::Identifier &&
        (qreg.count(cur().text) || creg.count(cur().text))) {
      auto ref = parseQubitRef();
      if (!ref) return;
      auto& [reg, idx] = *ref;
      const bool quantum = qreg.count(reg) != 0;
      const auto& slots = quantum ? qreg.at(reg) : creg.at(reg);
      if (idx == -1 && slots.size() != 1) {
        err("scalar function argument requires one explicit register index"); return;
      }
      const int realIdx = idx == -1 ? 0 : idx;
      args.push_back(quantum ? getQubitSlot(reg, realIdx, cur()) : getBitSlot(reg, realIdx, cur()));
      refs.push_back({reg, realIdx});
      constants.push_back(std::nullopt);
    } else {
      const auto expressionStart = pos;
      auto value = foldExpr();
      pos = expressionStart;
      args.push_back(parseExpr());
      refs.push_back({"", -1});
      constants.push_back(value);
    }
    if (!accept(Tok::Comma)) break;
  }
  if (!expect(Tok::RParen, "')'") || diag.hasErrors()) return;
  const auto fit = funcs.find(name);
  if (fit != funcs.end() && fit->second.specialize) {
    // Copy metadata because a body parse must not retain unordered_map iterators.
    const auto function = fit->second;
    if (args.size() != function.params.size()) {
      err("argument count mismatch for function '" + name + "'"); return;
    }
    if (std::any_of(inlineFrames.begin(), inlineFrames.end(), [&](const auto& frame) {
          return frame.name == name;
        })) {
      err("recursive call to '" + name + "' is not supported"); return;
    }
    if (inlineFrames.size() >= 128 || ++expandedCalls > 100000) {
      err("function specialization exceeds 128 nested calls or 100000 expanded calls"); return;
    }
    std::vector<pd::ValueId> qubitArgs;
    for (std::size_t i = 0; i < args.size(); ++i) {
      const auto type = function.params[i].type.kind;
      const auto actual = mod.typeOf(args[i]).kind;
      if (type == pd::TypeKind::Int || type == pd::TypeKind::Angle) {
        if ((actual != pd::TypeKind::Int && actual != pd::TypeKind::Angle) ||
            !constants[i] || !std::isfinite(*constants[i]) ||
            (type == pd::TypeKind::Int && (std::floor(*constants[i]) != *constants[i] ||
                                         std::abs(*constants[i]) > 9007199254740991.0))) {
          err("function scalar argument must be bound to a finite value matching its parameter type"); return;
        }
      } else if (type != actual || refs[i].first.empty()) {
        err("register argument type mismatch for function '" + name + "'"); return;
      }
      if (type == pd::TypeKind::Qubit) {
        if (std::find(qubitArgs.begin(), qubitArgs.end(), args[i]) != qubitArgs.end()) {
          err("function qubit arguments must refer to distinct slots"); return;
        }
        qubitArgs.push_back(args[i]);
      }
    }
    const auto afterCall = pos;
    const auto savedQreg = qreg;
    const auto savedCreg = creg;
    const auto savedClassicals = classicals;
    const auto savedConstants = ctConst;
    const auto savedScalars = scalarBindings;
    const auto savedBitTargets = bitTargets;
    qreg = function.capturedQreg;
    creg = function.capturedCreg;
    classicals.clear(); ctConst.clear(); scalarBindings.clear();
    for (const auto& [symbol, binding] : function.capturedScalars) {
      classicals[symbol] = binding->value;
      if (binding->constant) ctConst[symbol] = *binding->constant;
      scalarBindings[symbol] = std::make_shared<ScalarBinding>(*binding);
    }
    bitTargets = function.capturedBitTargets;
    for (std::size_t i = 0; i < args.size(); ++i) {
      const auto& param = function.params[i];
      qreg.erase(param.name); creg.erase(param.name);
      classicals.erase(param.name); ctConst.erase(param.name); bitTargets.erase(param.name);
      scalarBindings.erase(param.name);
      if (param.type.kind == pd::TypeKind::Qubit) {
        qreg[param.name] = Register::alias(savedQreg.at(refs[i].first), refs[i].second);
      } else if (param.type.kind == pd::TypeKind::Bit) {
        creg[param.name] = Register::alias(savedCreg.at(refs[i].first), refs[i].second);
        const auto target = savedBitTargets.find(refs[i].first);
        if (target != savedBitTargets.end()) bitTargets[param.name] = {target->second.at(refs[i].second)};
      } else {
        ctConst[param.name] = *constants[i];
        classicals[param.name] = args[i];
        recordScalar(param.name, true);
      }
    }
    inlineFrames.push_back({name, runtimeDepth});
    pos = function.body_start;
    parseBlock();
    auto frame = std::move(inlineFrames.back());
    inlineFrames.pop_back();
    std::vector<pd::ValueId> results = std::move(frame.values);
    if (!frame.returned) {
      for (const auto& param : function.params)
        if (param.type.kind == pd::TypeKind::Qubit) results.push_back(qreg.at(param.name)[0]);
    }
    // A conditional helper cannot change the meaning of a caller's slot at
    // the branch join. Materialize a returned permutation on those wires.
    // Outside runtime control flow, the existing return-alias semantics stay
    // unchanged. Fresh-wire replacement needs an explicit branch value merge.
    std::vector<std::string> quantumParams;
    std::vector<pd::ValueId> current;
    for (const auto& param : function.params) if (param.type.kind == pd::TypeKind::Qubit) {
      quantumParams.push_back(param.name);
      current.push_back(qreg.at(param.name)[0]);
    }
    for (std::size_t i = 0; i < results.size(); ++i)
      if (std::find(results.begin(), results.begin() + i, results[i]) != results.begin() + i)
        err("function cannot return duplicate qubit aliases: " + name);
    if (runtimeDepth && results.size() == current.size() && !diag.hasErrors()) {
      std::vector<std::size_t> wanted, stateAt;
      for (auto value : results) {
        const auto found = std::find(current.begin(), current.end(), value);
        if (found == current.end()) {
          err("runtime function return must be a permutation of its input qubits; fresh-wire substitution needs branch value merging");
          break;
        }
        wanted.push_back(static_cast<std::size_t>(found - current.begin()));
      }
      for (std::size_t i = 0; i < current.size(); ++i) stateAt.push_back(i);
      if (!diag.hasErrors()) for (std::size_t i = 0; i < current.size(); ++i) {
        const auto found = std::find(stateAt.begin() + i, stateAt.end(), wanted[i]);
        const auto j = static_cast<std::size_t>(found - stateAt.begin());
        if (i == j) continue;
        auto swapped = b.swap(current[i], current[j]);
        current[i] = swapped.first; current[j] = swapped.second;
        setQubitSlot(quantumParams[i], 0, current[i]);
        setQubitSlot(quantumParams[j], 0, current[j]);
        std::swap(stateAt[i], stateAt[j]);
      }
      if (!diag.hasErrors()) results = std::move(current);
    }
    qreg = savedQreg; creg = savedCreg; classicals = savedClassicals;
    ctConst = savedConstants; bitTargets = savedBitTargets;
    scalarBindings = savedScalars;
    pos = afterCall;
    if (results.size() != qubitArgs.size() || std::any_of(results.begin(), results.end(), [&](auto value) {
          return mod.typeOf(value).kind != pd::TypeKind::Qubit;
        })) {
      err("function must return one qubit value per qubit parameter: " + name);
    } else {
      std::size_t result = 0;
      for (std::size_t i = 0; i < function.params.size(); ++i)
        if (function.params[i].type.kind == pd::TypeKind::Qubit)
          setQubitSlot(refs[i].first, refs[i].second, results[result++]);
    }
  } else {
    std::vector<pd::Type> resultTypes;
    if (fit != funcs.end())
      for (const auto& param : fit->second.params)
        if (param.type.kind == pd::TypeKind::Qubit) resultTypes.push_back(pd::qubitType());
    auto results = b.call(name, args, resultTypes);
    std::size_t result = 0;
    for (std::size_t i = 0; i < refs.size() && result < results.size(); ++i)
      if (!refs[i].first.empty() && qreg.count(refs[i].first))
        setQubitSlot(refs[i].first, refs[i].second, results[result++]);
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseAssignStmt(const std::string& name) {
  expect(Tok::Equals, "'='");
  const auto expressionStart = pos;
  auto folded = foldExpr();
  pos = expressionStart;
  pd::ValueId v = parseExpr();
  if (mod.typeOf(v).kind == pd::TypeKind::Bit) {
    err("copying measured data into a numeric scalar is unsupported; use a separate measurement destination instead of a saved scalar alias");
    return;
  }
  if (folded && std::isfinite(*folded)) ctConst[name] = *folded;
  else ctConst.erase(name);
  classicals[name] = v;
  recordScalar(name);
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
      if (idx == -1 && qreg.at(reg).size() != 1) {
        err("function return requires one explicit qubit index per value"); return;
      }
      vs.push_back(getQubitSlot(reg, idx == -1 ? 0 : idx, cur()));
    } else {
      vs.push_back(parseExpr());
    }
    if (!accept(Tok::Comma)) break;
  }
  if (!inlineFrames.empty()) {
    if (runtimeDepth != inlineFrames.back().runtimeDepth) {
      err("conditional return requires explicit control-flow return lowering");
    } else {
      inlineFrames.back().returned = true;
      inlineFrames.back().values = std::move(vs);
    }
  } else {
    b.returnOp(std::span<const pd::ValueId>(vs.data(), vs.size()));
  }
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
        if (!idxOpt || !std::isfinite(*idxOpt) || std::floor(*idxOpt) != *idxOpt ||
            *idxOpt < 0 || *idxOpt > std::numeric_limits<int>::max()) {
          err("index must be a non-negative compile-time integer"); return;
        }
        if (cur().kind == Tok::Equals) {
          consume();
          if (cur().kind == Tok::Measure) {
            consume();
            auto qref = parseQubitRef();
            if (!qref) return;
            auto& [reg, idx2] = *qref;
            if (idx2 == -1 && (!qreg.count(reg) || qreg.at(reg).size() != 1)) {
              err("measurement into an indexed bit requires one explicit qubit"); return;
            }
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
              setBitSlot(name, static_cast<int>(i), b.measure(qit->second[i]));
            }
          } else {
            pd::ValueId vq = getQubitSlot(reg, idx, cur());
            pd::ValueId vbit = b.measure(vq);
            auto cit = creg.find(name);
            if (cit == creg.end() || cit->second.size() != 1) {
              err("expected single-bit register on lhs"); return;
            }
            setBitSlot(name, 0, vbit);
          }
          if (cur().kind == Tok::Newline) consume();
          return;
        }
        --pos;  // parseAssignStmt owns the '=' and compile-time rebinding.
        parseAssignStmt(name);
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
  skipNewlines();
  if (!expect(Tok::LBrace, "'{'")) return;
  skipNewlines();
  while (cur().kind != Tok::RBrace && cur().kind != Tok::Eof) {
    if (fatal) return;
    if (inlineReturned()) {
      // A static return exits this function, including all surrounding
      // compile-time loops. Consume the rest without emitting operations.
      std::size_t depth = 0;
      while (cur().kind != Tok::Eof) {
        if (cur().kind == Tok::RBrace && depth == 0) break;
        const auto token = consume().kind;
        if (token == Tok::LBrace) ++depth;
        else if (token == Tok::RBrace) --depth;
      }
      break;
    }
    const auto previous = pos;
    parseStmt();
    if (pos == previous) { err("parser could not consume statement"); consume(); }
    skipNewlines();
  }
  expect(Tok::RBrace, "'}'");
}

void Parser::parseProgram() {
  parseHeader();
  while (cur().kind != Tok::Eof && !fatal) {
    const auto previous = pos;
    parseStmt();
    if (pos == previous) { err("unexpected token outside a block"); consume(); }
    skipNewlines();
  }
}

}  // namespace

ParseResult parse(std::string_view text, std::string_view filename) {
  Lexer lex(text);
  auto toks = lex.tokenize();
  Parser p(std::move(toks), std::string(filename));
  try {
    p.parseProgram();
  } catch (const std::exception& error) {
    p.err(std::string("invalid source: ") + error.what());
  }
  ParseResult r;
  r.diag = std::move(p.diag);
  if (!r.diag.hasErrors()) r.module = std::move(p.mod);
  return r;
}

}  // namespace phonon::parser

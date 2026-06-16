// spinor/parser/cpp/lib/Parser.cpp
//
// Hand-written recursive-descent parser. One function per
// grammar rule. Produces a dialect::Module directly.
// Spec: docs/build/phaseA/M7_cpp_parser.md.

#include "spinor/parser/Parser.h"

#include "Lexer.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spinor::parser {

namespace {

using namespace spinor::dialect;

struct ParseError {
  std::string message;
  Location loc;
};

class Driver {
 public:
  Driver(std::vector<Token> toks, std::string filename, Module& m,
         Diagnostics& diag)
      : toks_(std::move(toks)),
        file_(std::move(filename)),
        m_(m),
        diag_(diag),
        b_(m_) {}

  void parseProgram() {
    skipBlankLines();
    parseTargetDecl();
    while (peek().kind != Tok::Eof) {
      parseStatement();
    }
    if (m_.targetAttr.empty()) {
      ParseError e;
      e.message = "missing 'target' declaration";
      e.loc = locOf(toks_.front());
      throw e;
    }
  }

 private:
  std::vector<Token> toks_;
  std::size_t pos_ = 0;
  std::string file_;
  Module& m_;
  Diagnostics& diag_;
  Builder b_;

  // Per-register state.
  struct RegState {
    bool isQubit = true;
    int size = 0;
    std::vector<ValueId> liveQubits;
    std::vector<int> genQubits;
    std::vector<ValueId> latestBits;
    std::vector<int> genBits;
  };
  std::unordered_map<std::string, RegState> regs_;

  // ---- helpers ----

  const Token& peek(std::size_t k = 0) const { return toks_[pos_ + k]; }
  const Token& consume() { return toks_[pos_++]; }
  bool match(Tok k) { if (peek().kind == k) { ++pos_; return true; } return false; }
  void expect(Tok k, const char* what) {
    if (peek().kind != k) {
      ParseError e;
      e.message = std::string("expected ") + what +
                  ", got '" + peek().text + "'";
      e.loc = locOf(peek());
      throw e;
    }
    ++pos_;
  }
  Location locOf(const Token& t) const {
    return Location{file_, t.line, t.column};
  }
  void skipBlankLines() {
    while (peek().kind == Tok::Newline) ++pos_;
  }
  void expectNewlineOrEof() {
    if (peek().kind == Tok::Newline) {
      ++pos_;
      skipBlankLines();
    } else if (peek().kind != Tok::Eof) {
      ParseError e;
      e.message = "expected end of line, got '" + peek().text + "'";
      e.loc = locOf(peek());
      throw e;
    }
  }

  // ---- target_decl ----
  void parseTargetDecl() {
    expect(Tok::Target, "'target' on the first non-comment line");
    std::string targetId;
    if (peek().kind == Tok::Generic) {
      ++pos_;
      targetId = "generic";
    } else if (peek().kind == Tok::Identifier ||
               peek().kind == Tok::GateName) {
      // a chip id like ibm_heron_r2; GateName because some chip
      // ids may collide with gate mnemonics in principle.
      targetId = consume().text;
    } else {
      ParseError e;
      e.message = "expected 'generic' or a device id after 'target'";
      e.loc = locOf(peek());
      throw e;
    }
    m_.targetAttr = targetId;
    expectNewlineOrEof();
  }

  // ---- statement ----
  void parseStatement() {
    switch (peek().kind) {
      case Tok::Qubit:    parseQubitDecl();  return;
      case Tok::Bit:      parseBitDecl();    return;
      case Tok::Reset:    parseResetStmt();  return;
      case Tok::Barrier:  parseBarrierStmt();return;
      case Tok::Measure: {
        // Bare-form `measure q[i]` (rare; we accept too).
        // Standard form is `c[i] = measure q[i]` or `c = measure q`.
        ParseError e;
        e.message = "'measure' must be the right-hand side of an assignment";
        e.loc = locOf(peek());
        throw e;
      }
      case Tok::GateName:
        parseGateStmt(); return;
      case Tok::Identifier:
        // could be `c = measure q` or `c[i] = measure q[i]`.
        parseMeasureStmt(); return;
      case Tok::Newline:
        ++pos_; return;
      case Tok::Eof: return;
      default: {
        ParseError e;
        e.message = "unexpected '" + peek().text +
                    "' at start of statement";
        e.loc = locOf(peek());
        throw e;
      }
    }
  }

  // ---- declarations ----
  void parseQubitDecl() {
    Token kw = consume();  // 'qubit'
    if (peek().kind != Tok::Identifier) {
      ParseError e;
      e.message = "expected register name after 'qubit'";
      e.loc = locOf(peek());
      throw e;
    }
    std::string name = consume().text;
    expect(Tok::LBracket, "'['");
    if (peek().kind != Tok::Integer) {
      ParseError e;
      e.message = "expected integer size in qubit declaration";
      e.loc = locOf(peek());
      throw e;
    }
    int size = std::stoi(consume().text);
    expect(Tok::RBracket, "']'");
    if (regs_.count(name)) {
      ParseError e;
      e.message = "register '" + name + "' redeclared";
      e.loc = locOf(kw);
      throw e;
    }
    RegState st;
    st.isQubit = true;
    st.size = size;
    for (int i = 0; i < size; ++i) {
      ValueId v = b_.allocQubit(locOf(kw));
      m_.setName(v, name + std::to_string(i));
      st.liveQubits.push_back(v);
      st.genQubits.push_back(0);
    }
    regs_.emplace(name, std::move(st));
    expectNewlineOrEof();
  }

  void parseBitDecl() {
    Token kw = consume();  // 'bit'
    if (peek().kind != Tok::Identifier) {
      ParseError e;
      e.message = "expected register name after 'bit'";
      e.loc = locOf(peek());
      throw e;
    }
    std::string name = consume().text;
    expect(Tok::LBracket, "'['");
    if (peek().kind != Tok::Integer) {
      ParseError e;
      e.message = "expected integer size in bit declaration";
      e.loc = locOf(peek());
      throw e;
    }
    int size = std::stoi(consume().text);
    expect(Tok::RBracket, "']'");
    if (regs_.count(name)) {
      ParseError e;
      e.message = "register '" + name + "' redeclared";
      e.loc = locOf(kw);
      throw e;
    }
    RegState st;
    st.isQubit = false;
    st.size = size;
    for (int i = 0; i < size; ++i) {
      ValueId v = b_.allocBit(locOf(kw));
      m_.setName(v, name + std::to_string(i));
      st.latestBits.push_back(v);
      st.genBits.push_back(0);
    }
    regs_.emplace(name, std::move(st));
    expectNewlineOrEof();
  }

  // ---- gate / params / angle ----
  double parseAngle() {
    // Possibilities:
    //  Real or Integer (decimal)
    //  -Real / -Integer
    //  pi   |   -pi
    //  pi/N |   -pi/N
    //  Real * pi  | Integer * pi  | -Real*pi
    bool neg = false;
    if (peek().kind == Tok::Minus) { neg = true; ++pos_; }
    if (peek().kind == Tok::Pi) {
      ++pos_;
      if (match(Tok::Slash)) {
        if (peek().kind != Tok::Integer) {
          ParseError e;
          e.message = "expected integer after 'pi/'";
          e.loc = locOf(peek());
          throw e;
        }
        int N = std::stoi(consume().text);
        double v = M_PI / N;
        return neg ? -v : v;
      }
      return neg ? -M_PI : M_PI;
    }
    // Real or integer literal, possibly followed by '* pi'.
    if (peek().kind != Tok::Real && peek().kind != Tok::Integer) {
      ParseError e;
      e.message = "expected angle (number or 'pi' form), got '" +
                  peek().text + "'";
      e.loc = locOf(peek());
      throw e;
    }
    double n = std::stod(consume().text);
    if (neg) n = -n;
    if (match(Tok::Star)) {
      expect(Tok::Pi, "'pi' after '*'");
      return n * M_PI;
    }
    return n;
  }

  std::vector<double> parseParams() {
    expect(Tok::LParen, "'('");
    std::vector<double> out;
    out.push_back(parseAngle());
    while (match(Tok::Comma)) out.push_back(parseAngle());
    expect(Tok::RParen, "')'");
    return out;
  }

  // operand ::= IDENT '[' INT ']'
  struct ParsedOperand {
    std::string reg;
    int idx = 0;
    Token loc;
  };
  ParsedOperand parseOperand() {
    if (peek().kind != Tok::Identifier) {
      ParseError e;
      e.message = "expected register operand, got '" + peek().text + "'";
      e.loc = locOf(peek());
      throw e;
    }
    Token regTok = consume();
    expect(Tok::LBracket, "'['");
    if (peek().kind != Tok::Integer) {
      ParseError e;
      e.message = "expected index inside '[ ]'";
      e.loc = locOf(peek());
      throw e;
    }
    int idx = std::stoi(consume().text);
    expect(Tok::RBracket, "']'");
    return {regTok.text, idx, regTok};
  }

  ValueId resolveQubit(const ParsedOperand& op) {
    auto it = regs_.find(op.reg);
    if (it == regs_.end() || !it->second.isQubit) {
      ParseError e;
      e.message = "'" + op.reg + "' is not a declared qubit register";
      e.loc = locOf(op.loc);
      throw e;
    }
    if (op.idx < 0 || op.idx >= (int)it->second.liveQubits.size()) {
      ParseError e;
      e.message = "qubit index " + std::to_string(op.idx) +
                  " out of range for register '" + op.reg + "'";
      e.loc = locOf(op.loc);
      throw e;
    }
    return it->second.liveQubits[op.idx];
  }
  void updateLiveQubit(const ParsedOperand& op, ValueId v) {
    auto it = regs_.find(op.reg);
    if (it == regs_.end() || !it->second.isQubit) return;
    int g = ++it->second.genQubits[op.idx];
    it->second.liveQubits[op.idx] = v;
    m_.setName(v, op.reg + std::to_string(op.idx) + "_" +
                  std::to_string(g));
  }
  int updateLatestBit(const ParsedOperand& op, ValueId v) {
    auto it = regs_.find(op.reg);
    if (it == regs_.end() || it->second.isQubit) {
      ParseError e;
      e.message = "'" + op.reg + "' is not a declared bit register";
      e.loc = locOf(op.loc);
      throw e;
    }
    if (op.idx < 0 || op.idx >= (int)it->second.latestBits.size()) {
      ParseError e;
      e.message = "bit index " + std::to_string(op.idx) +
                  " out of range for register '" + op.reg + "'";
      e.loc = locOf(op.loc);
      throw e;
    }
    int g = ++it->second.genBits[op.idx];
    it->second.latestBits[op.idx] = v;
    m_.setName(v, op.reg + std::to_string(op.idx) + "_" +
                  std::to_string(g));
    return g;
  }

  void parseGateStmt() {
    Token nameTok = consume();  // GateName
    std::vector<double> params;
    if (peek().kind == Tok::LParen) params = parseParams();
    std::vector<ParsedOperand> ops;
    ops.push_back(parseOperand());
    while (match(Tok::Comma)) ops.push_back(parseOperand());
    expectNewlineOrEof();

    const std::string& mn = nameTok.text;
    Location L = locOf(nameTok);

    auto need = [&](std::size_t want) {
      if (ops.size() != want) {
        ParseError e;
        e.message = "gate '" + mn + "' expects " +
                    std::to_string(want) + " operand(s), got " +
                    std::to_string(ops.size());
        e.loc = L;
        throw e;
      }
    };
    auto needAngles = [&](std::size_t want) {
      if (params.size() != want) {
        ParseError e;
        e.message = "gate '" + mn + "' expects " +
                    std::to_string(want) + " angle(s), got " +
                    std::to_string(params.size());
        e.loc = L;
        throw e;
      }
    };

    auto sq1 = [&](auto fn) {
      need(1);
      ValueId v = resolveQubit(ops[0]);
      ValueId r = (b_.*fn)(v, L);
      updateLiveQubit(ops[0], r);
    };
    auto sq1a = [&](auto fn) {
      need(1); needAngles(1);
      ValueId v = resolveQubit(ops[0]);
      ValueId r = (b_.*fn)(params[0], v, L);
      updateLiveQubit(ops[0], r);
    };
    auto tq = [&](auto fn) {
      need(2);
      ValueId a = resolveQubit(ops[0]);
      ValueId c = resolveQubit(ops[1]);
      auto [na, nc] = (b_.*fn)(a, c, L);
      updateLiveQubit(ops[0], na);
      updateLiveQubit(ops[1], nc);
    };

    if (mn == "h")   { sq1(&Builder::h);   return; }
    if (mn == "x")   { sq1(&Builder::x);   return; }
    if (mn == "y")   { sq1(&Builder::y);   return; }
    if (mn == "z")   { sq1(&Builder::z);   return; }
    if (mn == "s")   { sq1(&Builder::s);   return; }
    if (mn == "sdg") { sq1(&Builder::sdg); return; }
    if (mn == "t")   { sq1(&Builder::t);   return; }
    if (mn == "tdg") { sq1(&Builder::tdg); return; }
    if (mn == "sx")  { sq1(&Builder::sx);  return; }
    if (mn == "sxdg"){ sq1(&Builder::sxdg);return; }
    if (mn == "rx")  { sq1a(&Builder::rx); return; }
    if (mn == "ry")  { sq1a(&Builder::ry); return; }
    if (mn == "rz")  { sq1a(&Builder::rz); return; }
    if (mn == "gpi") { sq1a(&Builder::gpi); return; }
    if (mn == "gpi2"){ sq1a(&Builder::gpi2); return; }
    if (mn == "u1q") {
      need(1); needAngles(2);
      ValueId v = resolveQubit(ops[0]);
      ValueId r = b_.u1q(params[0], params[1], v, L);
      updateLiveQubit(ops[0], r);
      return;
    }
    if (mn == "cx")   { tq(&Builder::cx);   return; }
    if (mn == "cz")   { tq(&Builder::cz);   return; }
    if (mn == "swap") { tq(&Builder::swap); return; }
    if (mn == "ecr")  { tq(&Builder::ecr);  return; }
    if (mn == "ms")   { tq(&Builder::ms);   return; }
    if (mn == "rzz") {
      need(2); needAngles(1);
      ValueId a = resolveQubit(ops[0]);
      ValueId c = resolveQubit(ops[1]);
      auto [na, nc] = b_.rzz(params[0], a, c, L);
      updateLiveQubit(ops[0], na);
      updateLiveQubit(ops[1], nc);
      return;
    }
    ParseError e;
    e.message = "unknown gate '" + mn + "'";
    e.loc = L;
    throw e;
  }

  void parseResetStmt() {
    Token kw = consume();  // 'reset'
    ParsedOperand op = parseOperand();
    expectNewlineOrEof();
    ValueId v = resolveQubit(op);
    ValueId r = b_.reset(v, locOf(kw));
    updateLiveQubit(op, r);
  }

  void parseBarrierStmt() {
    Token kw = consume();  // 'barrier'
    std::vector<ValueId> qs;
    if (peek().kind != Tok::Newline && peek().kind != Tok::Eof) {
      ParsedOperand op = parseOperand();
      qs.push_back(resolveQubit(op));
      while (match(Tok::Comma)) {
        op = parseOperand();
        qs.push_back(resolveQubit(op));
      }
    }
    expectNewlineOrEof();
    b_.barrier(qs, locOf(kw));
  }

  // measure_stmt ::=
  //   operand "=" "measure" operand NEWLINE
  // | IDENT "=" "measure" IDENT NEWLINE       (whole-register form)
  void parseMeasureStmt() {
    // Already at IDENT.
    Token first = consume();  // IDENT (bit register name or operand)
    // Two cases by the next token:
    //   '['  → indexed form: c[i] = measure q[j]
    //   '='  → whole-register form: c = measure q
    if (match(Tok::LBracket)) {
      if (peek().kind != Tok::Integer) {
        ParseError e;
        e.message = "expected index in measure target";
        e.loc = locOf(peek());
        throw e;
      }
      int idx = std::stoi(consume().text);
      expect(Tok::RBracket, "']'");
      ParsedOperand bitOp{first.text, idx, first};
      expect(Tok::Equals, "'='");
      expect(Tok::Measure, "'measure'");
      ParsedOperand qOp = parseOperand();
      expectNewlineOrEof();
      ValueId q = resolveQubit(qOp);
      ValueId bit = b_.measure(q, locOf(first));
      updateLatestBit(bitOp, bit);
      return;
    }
    // Whole-register form.
    expect(Tok::Equals, "'='");
    expect(Tok::Measure, "'measure'");
    if (peek().kind != Tok::Identifier) {
      ParseError e;
      e.message = "expected qubit register name after 'measure'";
      e.loc = locOf(peek());
      throw e;
    }
    Token qReg = consume();
    expectNewlineOrEof();
    auto bitIt = regs_.find(first.text);
    auto qIt = regs_.find(qReg.text);
    if (bitIt == regs_.end() || bitIt->second.isQubit) {
      ParseError e;
      e.message = "'" + first.text + "' is not a declared bit register";
      e.loc = locOf(first);
      throw e;
    }
    if (qIt == regs_.end() || !qIt->second.isQubit) {
      ParseError e;
      e.message = "'" + qReg.text + "' is not a declared qubit register";
      e.loc = locOf(qReg);
      throw e;
    }
    if (bitIt->second.size != qIt->second.size) {
      ParseError e;
      e.message = "register-wide measure requires matching sizes";
      e.loc = locOf(first);
      throw e;
    }
    int n = qIt->second.size;
    for (int i = 0; i < n; ++i) {
      ParsedOperand qOp{qReg.text, i, qReg};
      ParsedOperand bitOp{first.text, i, first};
      ValueId q = resolveQubit(qOp);
      ValueId bit = b_.measure(q, locOf(first));
      updateLatestBit(bitOp, bit);
    }
  }
};

}  // namespace

ParseResult parse(std::string_view text, std::string_view filename) {
  ParseResult r;
  Lexer lx(text);
  std::vector<Token> toks = lx.tokenize();
  r.module = Module();
  r.module->targetAttr = "";
  Driver drv(toks, std::string(filename), *r.module, r.diag);
  try {
    drv.parseProgram();
  } catch (const ParseError& e) {
    r.diag.error(e.message, e.loc);
    r.module.reset();
  } catch (const std::exception& e) {
    r.diag.error(std::string("internal parse error: ") + e.what());
    r.module.reset();
  }
  return r;
}

}  // namespace spinor::parser

// photon/lang/cpp/lib/Parser.cpp
//
// Hand-written recursive-descent parser for Photon (.pho).
//
// The grammar is documented in docs/build/phaseC/M1_lang.md §2.

#include "photon/lang/Parser.h"
#include "Lexer.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace photon::lang {

namespace {

struct ParserImpl {
  std::vector<Token> toks;
  std::size_t pos = 0;
  std::string filename;
  Diagnostics diag;
  bool fatal = false;

  ParserImpl(std::vector<Token> t, std::string f)
      : toks(std::move(t)), filename(std::move(f)) {}

  // ---- token helpers ----
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
    while (cur().kind == Tok::Newline || cur().kind == Tok::Semicolon)
      consume();
  }
  Location locHere() const {
    return {filename, cur().line, cur().column};
  }
  void err(std::string msg) {
    diag.error(std::move(msg), locHere());
    fatal = true;
  }

  // ---- types ----
  std::optional<Type> parseType() {
    if (accept(Tok::Int))   return intType();
    if (accept(Tok::Angle)) return angleType();
    if (accept(Tok::Bit))   return bitType();
    if (cur().kind == Tok::QReg) {
      consume();
      // Optional `(N)` for parameter declarations like `QReg q(2)`,
      // or just `QReg` as a parameter type without a fixed size.
      if (accept(Tok::LParen)) {
        if (cur().kind != Tok::Integer) {
          err("QReg size must be an integer literal");
          return std::nullopt;
        }
        std::int64_t n = std::stoll(consume().text);
        if (!expect(Tok::RParen, "')'")) return std::nullopt;
        return qregType(static_cast<std::uint32_t>(n));
      }
      return qregType(0);
    }
    if (cur().kind == Tok::Ident) {
      // User-defined type alias (e.g. Oracle in the spec example).
      // Treat unknown idents as Oracle for M1 (a forward-declared
      // callable) — this lets `kernel search(oracle: Oracle) -> int`
      // parse without M2's type table.
      std::string s = consume().text;
      (void)s;
      return oracleType();
    }
    return std::nullopt;
  }

  // ---- expressions (Pratt-ish: cmp < add < mul < unary < primary) ----
  ExprPtr parseExpr()    { return parseCmp(); }

  ExprPtr parseCmp() {
    auto lhs = parseAdd();
    while (true) {
      Tok k = cur().kind;
      ExprKind ek;
      switch (k) {
        case Tok::EqEq:  ek = ExprKind::CmpEq;  break;
        case Tok::NotEq: ek = ExprKind::CmpNeq; break;
        case Tok::Lt:    ek = ExprKind::CmpLt;  break;
        case Tok::Gt:    ek = ExprKind::CmpGt;  break;
        case Tok::Le:    ek = ExprKind::CmpLe;  break;
        case Tok::Ge:    ek = ExprKind::CmpGe;  break;
        default: return lhs;
      }
      Location loc = locHere();
      consume();
      auto rhs = parseAdd();
      auto e = std::make_shared<Expr>();
      e->kind = ek;
      e->children = {std::move(lhs), std::move(rhs)};
      e->loc = loc;
      lhs = std::move(e);
    }
  }

  ExprPtr parseAdd() {
    auto lhs = parseMul();
    while (cur().kind == Tok::Plus || cur().kind == Tok::Minus) {
      std::string op = cur().text;
      Location loc = locHere();
      consume();
      auto rhs = parseMul();
      lhs = mkBinOp(op, lhs, rhs, loc);
    }
    return lhs;
  }

  ExprPtr parseMul() {
    auto lhs = parseUnary();
    while (cur().kind == Tok::Star || cur().kind == Tok::Slash) {
      std::string op = cur().text;
      Location loc = locHere();
      consume();
      auto rhs = parseUnary();
      lhs = mkBinOp(op, lhs, rhs, loc);
    }
    return lhs;
  }

  ExprPtr parseUnary() {
    if (cur().kind == Tok::Minus) {
      Location loc = locHere();
      consume();
      auto rhs = parseUnary();
      auto e = std::make_shared<Expr>();
      e->kind = ExprKind::UnaryMinus;
      e->children = {std::move(rhs)};
      e->loc = loc;
      return e;
    }
    return parsePrimary();
  }

  ExprPtr parsePrimary() {
    Location loc = locHere();
    if (cur().kind == Tok::Integer) {
      std::int64_t v = std::stoll(consume().text);
      return mkInt(v, loc);
    }
    if (cur().kind == Tok::Real) {
      double v = std::stod(consume().text);
      return mkReal(v, loc);
    }
    if (cur().kind == Tok::Pi) {
      consume();
      auto e = std::make_shared<Expr>();
      e->kind = ExprKind::Pi;
      e->loc = loc;
      return e;
    }
    if (cur().kind == Tok::LParen) {
      consume();
      auto inner = parseExpr();
      expect(Tok::RParen, "')'");
      return inner;
    }
    if (cur().kind == Tok::Ident) {
      std::string name = consume().text;
      // Member expression: `q.method` or `q.method(args)`.
      if (cur().kind == Tok::Dot) {
        consume();
        if (cur().kind != Tok::Ident) {
          err("expected member name after '.'");
          return mkIdent(name, loc);
        }
        std::string member = consume().text;
        if (cur().kind == Tok::LParen) {
          consume();
          std::vector<ExprPtr> args;
          if (cur().kind != Tok::RParen) {
            args.push_back(parseExpr());
            while (accept(Tok::Comma)) args.push_back(parseExpr());
          }
          expect(Tok::RParen, "')'");
          auto e = std::make_shared<Expr>();
          e->kind = ExprKind::Call;
          e->text = name + "." + member;
          e->children = std::move(args);
          e->loc = loc;
          return e;
        }
        return mkMember(name, member, loc);
      }
      // Bare call: `f(args)` — used by lib expansions and kernel calls.
      if (cur().kind == Tok::LParen) {
        consume();
        std::vector<ExprPtr> args;
        if (cur().kind != Tok::RParen) {
          args.push_back(parseExpr());
          while (accept(Tok::Comma)) args.push_back(parseExpr());
        }
        expect(Tok::RParen, "')'");
        return mkCall(name, std::move(args), loc);
      }
      return mkIdent(name, loc);
    }
    err("unexpected token '" + cur().text + "' in expression");
    return mkInt(0, loc);
  }

  // ---- statements ----

  std::vector<StmtPtr> parseBlock() {
    std::vector<StmtPtr> body;
    expect(Tok::LBrace, "'{'");
    skipNewlines();
    while (!fatal && cur().kind != Tok::RBrace && cur().kind != Tok::Eof) {
      auto s = parseStmt();
      if (s) body.push_back(std::move(s));
      skipNewlines();
    }
    expect(Tok::RBrace, "'}'");
    return body;
  }

  StmtPtr parseStmt() {
    Location loc = locHere();
    if (cur().kind == Tok::Return) {
      consume();
      auto s = std::make_shared<Stmt>();
      s->kind = StmtKind::ReturnStmt;
      s->loc = loc;
      if (cur().kind != Tok::Newline && cur().kind != Tok::Semicolon &&
          cur().kind != Tok::RBrace) {
        s->init = parseExpr();
      }
      return s;
    }
    if (cur().kind == Tok::For) {
      consume();
      auto s = std::make_shared<Stmt>();
      s->kind = StmtKind::ForLoop;
      s->loc = loc;
      if (cur().kind != Tok::Ident) {
        err("expected loop variable name");
        return nullptr;
      }
      s->for_var = consume().text;
      expect(Tok::In, "'in'");
      s->for_lo = parseExpr();
      expect(Tok::DotDot, "'..'");
      s->for_hi = parseExpr();
      s->body = parseBlock();
      return s;
    }
    if (cur().kind == Tok::If) {
      consume();
      expect(Tok::LParen, "'('");
      auto s = std::make_shared<Stmt>();
      s->kind = StmtKind::IfStmt;
      s->loc = loc;
      s->predicate = parseExpr();
      expect(Tok::RParen, "')'");
      s->then_body = parseBlock();
      skipNewlines();
      if (accept(Tok::Else)) {
        skipNewlines();
        s->else_body = parseBlock();
      }
      return s;
    }
    // Type-led declaration: `int n = ...`, `angle theta = ...`,
    // `QReg q(N)`, `Bit c = ...`.
    if (cur().kind == Tok::Int || cur().kind == Tok::Angle ||
        cur().kind == Tok::Bit || cur().kind == Tok::QReg) {
      auto t = parseType();
      if (!t) return nullptr;
      if (cur().kind != Tok::Ident) {
        err("expected variable name after type");
        return nullptr;
      }
      auto s = std::make_shared<Stmt>();
      s->kind = StmtKind::VarDecl;
      s->loc = loc;
      s->decl_type = *t;
      s->name = consume().text;
      // QReg(N) — N already consumed by parseType. But the user
      // might also write `QReg q(2)` — second size after the name.
      if (s->decl_type.kind == TypeKind::QReg && accept(Tok::LParen)) {
        if (cur().kind != Tok::Integer) {
          err("QReg constructor expects integer size");
          return nullptr;
        }
        s->decl_type.qreg_size =
            static_cast<std::uint32_t>(std::stoll(consume().text));
        expect(Tok::RParen, "')'");
      }
      if (accept(Tok::Equals)) {
        s->init = parseExpr();
      }
      return s;
    }
    // Identifier-led: assignment, gate call, lib call, measure, expr stmt.
    if (cur().kind == Tok::Ident) {
      // peek ahead to disambiguate.
      std::string name = cur().text;
      if (peek(1).kind == Tok::Equals) {
        consume();  // ident
        consume();  // '='
        auto s = std::make_shared<Stmt>();
        s->kind = StmtKind::Assign;
        s->loc = loc;
        s->name = std::move(name);
        s->init = parseExpr();
        return s;
      }
      if (peek(1).kind == Tok::Dot) {
        consume();  // ident
        consume();  // '.'
        if (cur().kind != Tok::Ident) {
          err("expected method name after '.'");
          return nullptr;
        }
        std::string method = consume().text;
        // measure / measure_int — no args, no parens or empty parens.
        bool empty_parens = false;
        if (cur().kind == Tok::LParen && peek(1).kind == Tok::RParen) {
          consume(); consume();
          empty_parens = true;
        }
        if (method == "measure" || method == "measure_int") {
          auto s = std::make_shared<Stmt>();
          s->kind = (method == "measure_int") ? StmtKind::MeasureInt
                                              : StmtKind::MeasureAll;
          s->loc = loc;
          s->receiver = std::move(name);
          s->method = std::move(method);
          (void)empty_parens;  // either form is fine.
          return s;
        }
        // Gate or lib call: open paren, args, close paren.
        std::vector<ExprPtr> args;
        if (!empty_parens) {
          if (!expect(Tok::LParen, "'('")) return nullptr;
          if (cur().kind != Tok::RParen) {
            args.push_back(parseExpr());
            while (accept(Tok::Comma)) args.push_back(parseExpr());
          }
          expect(Tok::RParen, "')'");
        }
        auto s = std::make_shared<Stmt>();
        s->kind = isPhotonGateMethod(method) ? StmtKind::GateCall
                                             : StmtKind::LibCall;
        s->loc = loc;
        s->receiver = std::move(name);
        s->method = std::move(method);
        s->args = std::move(args);
        return s;
      }
      // bare expression statement (e.g. a call to a free function).
      auto e = parseExpr();
      auto s = std::make_shared<Stmt>();
      s->kind = StmtKind::ExprStmt;
      s->loc = loc;
      s->init = e;
      return s;
    }
    err("unexpected token '" + cur().text + "' starting statement");
    consume();
    return nullptr;
  }

  // ---- top-level (def / kernel) ----

  std::optional<Function> parseFunction(bool is_kernel) {
    Location loc = locHere();
    consume();  // 'def' or 'kernel'
    if (cur().kind != Tok::Ident) {
      err("expected function name");
      return std::nullopt;
    }
    Function f;
    f.is_kernel = is_kernel;
    f.name = consume().text;
    f.loc = loc;
    expect(Tok::LParen, "'('");
    if (cur().kind != Tok::RParen) {
      do {
        Param p;
        p.loc = locHere();
        // Two equivalent forms accepted:
        //   `qubit a` / `int n` / `angle theta` / `QReg q` / `Oracle o`
        //   `name: Type` (Photon spec example: `oracle: Oracle`)
        // Form 1: type then name.
        if (cur().kind == Tok::Int || cur().kind == Tok::Angle ||
            cur().kind == Tok::Bit || cur().kind == Tok::QReg) {
          auto t = parseType();
          if (!t) return std::nullopt;
          p.type = *t;
          if (cur().kind != Tok::Ident) {
            err("expected parameter name");
            return std::nullopt;
          }
          p.name = consume().text;
        } else if (cur().kind == Tok::Ident && peek(1).kind == Tok::Colon) {
          p.name = consume().text;
          consume();  // ':'
          auto t = parseType();
          if (!t) return std::nullopt;
          p.type = *t;
        } else if (cur().kind == Tok::Ident) {
          // Treat as `name Type` for backward compatibility with
          // Phonon-style parameters.
          std::string first = consume().text;
          if (cur().kind == Tok::Ident) {
            // Was: `qubit a` (Phonon legacy) — first is type kw not
            // recognised; treat as oracle.
            p.type = oracleType();
            p.name = consume().text;
            (void)first;
          } else {
            // Bare ident — treat as oracle parameter.
            p.type = oracleType();
            p.name = first;
          }
        } else {
          err("expected parameter declaration");
          return std::nullopt;
        }
        f.params.push_back(std::move(p));
      } while (accept(Tok::Comma));
    }
    expect(Tok::RParen, "')'");
    if (accept(Tok::Arrow)) {
      auto rt = parseType();
      if (!rt) return std::nullopt;
      f.return_type = *rt;
    }
    f.body = parseBlock();
    return f;
  }

  Module parseProgram() {
    Module m;
    skipNewlines();
    if (accept(Tok::Target)) {
      if (cur().kind == Tok::Ident) m.target = consume().text;
      else err("expected target id");
    }
    skipNewlines();
    while (!fatal && cur().kind != Tok::Eof) {
      if (cur().kind == Tok::Def || cur().kind == Tok::Kernel) {
        bool is_kernel = (cur().kind == Tok::Kernel);
        auto f = parseFunction(is_kernel);
        if (f) m.functions.push_back(std::move(*f));
      } else {
        err("expected 'def' or 'kernel' at top level");
        consume();
      }
      skipNewlines();
    }
    return m;
  }
};

}  // namespace

ParseResult parse(std::string_view text, std::string_view filename) {
  Lexer lx(text);
  auto toks = lx.tokenize();
  ParserImpl p(std::move(toks), std::string(filename));
  Module m = p.parseProgram();
  ParseResult r;
  r.diag = std::move(p.diag);
  bool ok = true;
  for (const auto& d : r.diag.items()) {
    if (d.severity == DiagSeverity::Error) { ok = false; break; }
  }
  if (ok) r.module = std::move(m);
  return r;
}

}  // namespace photon::lang

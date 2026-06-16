// photon/frontends/cpp/lib/Ingest.cpp
//
// Self-hosted ingester for the small C++ subset Photon kernels use.
// This is intentionally NOT a full C++ parser: it only recognises
// constructs the engine can lower (QReg, gate methods, for, if, return).
// Anything outside the subset produces a precise diagnostic.

#include "photon/cppfront/Ingest.h"

#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace photon::cppfront {
namespace pl = photon::lang;
namespace {

enum class Tok {
  Ident, Integer, Real,
  KwInt, KwDouble, KwVoid, KwAuto,
  KwFor, KwIf, KwElse, KwReturn,
  KwQReg, KwBit, KwOracle,
  LBrack, RBrack, LParen, RParen, LBrace, RBrace,
  Comma, Semi, Colon, Dot, AmpAmp, Arrow,
  AttrOpen, AttrClose,   // [[ and ]]
  Eq, EqEq, NotEq, Lt, Gt, Le, Ge,
  Plus, Minus, Star, Slash,
  PlusPlus, MinusMinus,
  ColonColon,
  Eof,
  Unknown,
};

struct Token {
  Tok kind = Tok::Eof;
  std::string text;
  std::uint32_t line = 0, col = 0;
};

struct Lex {
  std::string_view src;
  std::size_t pos = 0;
  std::uint32_t line = 1, col = 1;

  char peek(std::size_t k = 0) const {
    return (pos + k) < src.size() ? src[pos + k] : '\0';
  }
  char get() {
    if (pos >= src.size()) return '\0';
    char c = src[pos++];
    if (c == '\n') { ++line; col = 1; } else { ++col; }
    return c;
  }

  void skipWS() {
    while (pos < src.size()) {
      char c = peek();
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { get(); continue; }
      if (c == '/' && peek(1) == '/') {
        while (pos < src.size() && peek() != '\n') ++pos;
        continue;
      }
      if (c == '/' && peek(1) == '*') {
        get(); get();
        while (pos < src.size() && !(peek() == '*' && peek(1) == '/')) get();
        if (pos < src.size()) { get(); get(); }
        continue;
      }
      // Skip preprocessor directives (no expansion).
      if (c == '#' && col == 1) {
        while (pos < src.size() && peek() != '\n') ++pos;
        continue;
      }
      break;
    }
  }

  Token readIdent() {
    std::uint32_t l = line, c = col;
    std::string s;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')
      s.push_back(get());
    Tok t = Tok::Ident;
    if (s == "int")    t = Tok::KwInt;
    else if (s == "double" || s == "float") t = Tok::KwDouble;
    else if (s == "void") t = Tok::KwVoid;
    else if (s == "auto") t = Tok::KwAuto;
    else if (s == "for")  t = Tok::KwFor;
    else if (s == "if")   t = Tok::KwIf;
    else if (s == "else") t = Tok::KwElse;
    else if (s == "return") t = Tok::KwReturn;
    else if (s == "QReg") t = Tok::KwQReg;
    else if (s == "Bit" || s == "bit")  t = Tok::KwBit;
    else if (s == "Oracle") t = Tok::KwOracle;
    return {t, std::move(s), l, c};
  }

  Token readNumber() {
    std::uint32_t l = line, c = col;
    std::string s;
    bool isReal = false;
    while (std::isdigit(static_cast<unsigned char>(peek()))) s.push_back(get());
    if (peek() == '.' && peek(1) != '.') {
      isReal = true; s.push_back(get());
      while (std::isdigit(static_cast<unsigned char>(peek())))
        s.push_back(get());
    }
    if (peek() == 'e' || peek() == 'E') {
      isReal = true; s.push_back(get());
      if (peek() == '+' || peek() == '-') s.push_back(get());
      while (std::isdigit(static_cast<unsigned char>(peek())))
        s.push_back(get());
    }
    return {isReal ? Tok::Real : Tok::Integer, std::move(s), l, c};
  }

  Token next() {
    skipWS();
    if (pos >= src.size()) return {Tok::Eof, "", line, col};
    char c = peek();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
      return readIdent();
    if (std::isdigit(static_cast<unsigned char>(c))) return readNumber();
    std::uint32_t l = line, col_ = col;
    auto pop = [&](Tok k, std::string t) {
      get();
      return Token{k, std::move(t), l, col_};
    };
    auto pop2 = [&](Tok k, std::string t) {
      get(); get();
      return Token{k, std::move(t), l, col_};
    };
    if (c == '[' && peek(1) == '[') return pop2(Tok::AttrOpen, "[[");
    if (c == ']' && peek(1) == ']') return pop2(Tok::AttrClose, "]]");
    if (c == '+' && peek(1) == '+') return pop2(Tok::PlusPlus, "++");
    if (c == '-' && peek(1) == '-') return pop2(Tok::MinusMinus, "--");
    if (c == '-' && peek(1) == '>') return pop2(Tok::Arrow, "->");
    if (c == ':' && peek(1) == ':') return pop2(Tok::ColonColon, "::");
    if (c == '=' && peek(1) == '=') return pop2(Tok::EqEq, "==");
    if (c == '!' && peek(1) == '=') return pop2(Tok::NotEq, "!=");
    if (c == '<' && peek(1) == '=') return pop2(Tok::Le, "<=");
    if (c == '>' && peek(1) == '=') return pop2(Tok::Ge, ">=");
    switch (c) {
      case '[': return pop(Tok::LBrack, "[");
      case ']': return pop(Tok::RBrack, "]");
      case '(': return pop(Tok::LParen, "(");
      case ')': return pop(Tok::RParen, ")");
      case '{': return pop(Tok::LBrace, "{");
      case '}': return pop(Tok::RBrace, "}");
      case ',': return pop(Tok::Comma, ",");
      case ';': return pop(Tok::Semi, ";");
      case ':': return pop(Tok::Colon, ":");
      case '.': return pop(Tok::Dot, ".");
      case '+': return pop(Tok::Plus, "+");
      case '-': return pop(Tok::Minus, "-");
      case '*': return pop(Tok::Star, "*");
      case '/': return pop(Tok::Slash, "/");
      case '<': return pop(Tok::Lt, "<");
      case '>': return pop(Tok::Gt, ">");
      case '=': return pop(Tok::Eq, "=");
      case '&':
        if (peek(1) == '&') return pop2(Tok::AmpAmp, "&&");
        return pop(Tok::Unknown, "&");
      default:
        std::string s; s.push_back(c); get();
        return Token{Tok::Unknown, std::move(s), l, col_};
    }
  }
};

std::vector<Token> tokenize(std::string_view src) {
  Lex lx{src};
  std::vector<Token> out;
  while (true) {
    Token t = lx.next();
    bool eof = (t.kind == Tok::Eof);
    out.push_back(std::move(t));
    if (eof) break;
  }
  return out;
}

struct Parser {
  std::vector<Token> toks;
  std::size_t pos = 0;
  std::string filename;
  pl::Diagnostics diag;
  bool fatal = false;

  Parser(std::vector<Token> t, std::string f)
      : toks(std::move(t)), filename(std::move(f)) {}

  const Token& cur() const { return toks[std::min(pos, toks.size() - 1)]; }
  const Token& peek(std::size_t k = 1) const {
    return toks[std::min(pos + k, toks.size() - 1)];
  }
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
  pl::Location loc() const { return {filename, cur().line, cur().col}; }
  void err(std::string m) {
    diag.error(std::move(m), loc());
    fatal = true;
  }

  // Skip a `[[ ... ]]` attribute. Returns true when the attribute
  // contains the photon::kernel marker.
  bool skipAttribute() {
    if (cur().kind != Tok::AttrOpen) return false;
    consume();
    bool sawPhoton = false;
    while (cur().kind != Tok::AttrClose && cur().kind != Tok::Eof) {
      if (cur().text == "photon" && peek().kind == Tok::ColonColon &&
          peek(2).text == "kernel") {
        sawPhoton = true;
      }
      // Tolerate the GCC-style `__attribute__((annotate("photon-kernel")))`
      // form: `annotate("photon-kernel")` shows up here as
      // identifier `annotate` followed by parens with a string we can't
      // tokenize cleanly. We accept any attribute body and rely on the
      // marker check.
      consume();
    }
    expect(Tok::AttrClose, "']]'");
    return sawPhoton;
  }

  // Match the optional `[[photon::kernel]]` attribute (return true if
  // present) and the simpler GCC-style `__attribute__((...))` (skipped,
  // returns false).
  bool matchKernelAttr() {
    if (cur().kind == Tok::AttrOpen) return skipAttribute();
    if (cur().kind == Tok::Ident && cur().text == "__attribute__") {
      consume();
      if (cur().kind == Tok::LParen) {
        // Skip nested parens.
        int depth = 0;
        while (cur().kind != Tok::Eof) {
          if (cur().kind == Tok::LParen) ++depth;
          else if (cur().kind == Tok::RParen) { --depth; if (depth == 0) {
            consume(); break;
          } }
          consume();
        }
      }
    }
    return false;
  }

  // Parse a type specifier: int / double / void / QReg / Oracle / Bit.
  std::optional<pl::Type> parseType() {
    switch (cur().kind) {
      case Tok::KwInt:    consume(); return pl::intType();
      case Tok::KwDouble: consume(); return pl::angleType();
      case Tok::KwVoid:   consume(); return pl::voidType();
      case Tok::KwBit:    consume(); return pl::bitType();
      case Tok::KwQReg:   consume(); return pl::qregType(0);
      case Tok::KwOracle: consume(); return pl::oracleType();
      case Tok::KwAuto:   consume(); return pl::voidType();  // hint only
      default: return std::nullopt;
    }
  }

  // Parse a numeric / identifier expression. The C++ ingester has no
  // operator table; only constants and bare identifiers are needed
  // for arguments to gate methods (qubit indices, angles).
  pl::ExprPtr parseExpr();
  pl::ExprPtr parseUnary() {
    pl::Location l = loc();
    if (cur().kind == Tok::Minus) {
      consume();
      auto e = std::make_shared<pl::Expr>();
      e->kind = pl::ExprKind::UnaryMinus;
      e->loc = l;
      e->children = {parseUnary()};
      return e;
    }
    return parsePrimary();
  }
  pl::ExprPtr parsePrimary() {
    pl::Location l = loc();
    Token t = cur();
    if (t.kind == Tok::Integer) {
      consume();
      return pl::mkInt(std::stoll(t.text), l);
    }
    if (t.kind == Tok::Real) {
      consume();
      return pl::mkReal(std::stod(t.text), l);
    }
    if (t.kind == Tok::LParen) {
      consume();
      auto e = parseExpr();
      expect(Tok::RParen, "')'");
      return e;
    }
    if (t.kind == Tok::Ident) {
      consume();
      return pl::mkIdent(t.text, l);
    }
    err("unexpected token '" + t.text + "' in expression");
    return pl::mkInt(0, l);
  }

  // Parse a function: `<type> name(<params>) [body]`.
  std::optional<pl::Function> parseFunction(bool is_kernel) {
    pl::Location L = loc();
    auto rt = parseType();
    if (!rt) {
      err("expected return type");
      return std::nullopt;
    }
    if (cur().kind != Tok::Ident) {
      err("expected function name");
      return std::nullopt;
    }
    pl::Function f;
    f.name = consume().text;
    f.is_kernel = is_kernel;
    f.return_type = *rt;
    f.loc = L;
    expect(Tok::LParen, "'('");
    if (cur().kind != Tok::RParen) {
      do {
        pl::Param p;
        p.loc = loc();
        auto pt = parseType();
        if (!pt) { err("expected parameter type"); return std::nullopt; }
        p.type = *pt;
        if (cur().kind != Tok::Ident) {
          err("expected parameter name");
          return std::nullopt;
        }
        p.name = consume().text;
        f.params.push_back(std::move(p));
      } while (accept(Tok::Comma));
    }
    expect(Tok::RParen, "')'");
    if (accept(Tok::Semi)) {
      // Forward declaration; ignore.
      return std::nullopt;
    }
    expect(Tok::LBrace, "'{'");
    parseBlock(f.body);
    return f;
  }

  void parseBlock(std::vector<pl::StmtPtr>& body) {
    while (!fatal && cur().kind != Tok::RBrace && cur().kind != Tok::Eof) {
      auto s = parseStmt();
      if (s) body.push_back(std::move(s));
    }
    expect(Tok::RBrace, "'}'");
  }

  // Skip any function-body-level construct outside the supported subset
  // by recursing into braces. This is the "be tolerant" mode used when
  // we see a non-kernel function.
  void skipBalanced(Tok open, Tok close) {
    int depth = 1;
    while (cur().kind != Tok::Eof && depth > 0) {
      if (cur().kind == open) ++depth;
      else if (cur().kind == close) --depth;
      consume();
    }
  }

  // Statement.
  pl::StmtPtr parseStmt();
};

pl::ExprPtr Parser::parseExpr() {
  // Just unary -> primary; binary ops in argument positions are folded
  // by the lowerer's foldInt/foldReal helpers if needed.
  auto lhs = parseUnary();
  // Allow simple `lhs <op> rhs` for arguments like `pi/4`.
  while (cur().kind == Tok::Plus || cur().kind == Tok::Minus ||
         cur().kind == Tok::Star || cur().kind == Tok::Slash) {
    Token op = consume();
    auto rhs = parseUnary();
    auto e = std::make_shared<pl::Expr>();
    e->kind = pl::ExprKind::BinOp;
    e->text = op.text;
    e->children = {std::move(lhs), std::move(rhs)};
    e->loc = {filename, op.line, op.col};
    lhs = std::move(e);
  }
  return lhs;
}

pl::StmtPtr Parser::parseStmt() {
  pl::Location L = loc();
  if (cur().kind == Tok::KwReturn) {
    consume();
    auto s = std::make_shared<pl::Stmt>();
    s->kind = pl::StmtKind::ReturnStmt;
    s->loc = L;
    if (cur().kind != Tok::Semi) {
      // Special: `return q.measure_int();` becomes a measure-int return.
      if (cur().kind == Tok::Ident && peek().kind == Tok::Dot &&
          peek(2).kind == Tok::Ident && peek(3).kind == Tok::LParen) {
        std::string recv = consume().text;
        consume();  // '.'
        std::string method = consume().text;
        consume();  // '('
        expect(Tok::RParen, "')'");
        auto call = std::make_shared<pl::Expr>();
        call->kind = pl::ExprKind::Call;
        call->text = recv + "." + method;
        call->loc = L;
        s->init = call;
      } else {
        s->init = parseExpr();
      }
    }
    expect(Tok::Semi, "';'");
    return s;
  }
  if (cur().kind == Tok::KwFor) {
    consume();
    expect(Tok::LParen, "'('");
    // For now support `for (int i = lo; i < hi; ++i) { ... }`.
    if (cur().kind != Tok::KwInt) {
      err("for-loop must start with `int <var> = ...`");
      return nullptr;
    }
    consume();  // int
    if (cur().kind != Tok::Ident) {
      err("expected loop variable name");
      return nullptr;
    }
    std::string var = consume().text;
    expect(Tok::Eq, "'='");
    auto lo = parseExpr();
    expect(Tok::Semi, "';'");
    if (cur().kind != Tok::Ident || cur().text != var) {
      err("expected `" + var + " < <hi>;` continuation");
      return nullptr;
    }
    consume();  // var
    expect(Tok::Lt, "'<'");
    auto hi = parseExpr();
    expect(Tok::Semi, "';'");
    // Tolerate any of: ++i / i++ / i = i + 1.
    if (cur().kind == Tok::PlusPlus) consume();
    if (cur().kind == Tok::Ident) consume();
    if (cur().kind == Tok::PlusPlus) consume();
    expect(Tok::RParen, "')'");
    expect(Tok::LBrace, "'{'");
    auto s = std::make_shared<pl::Stmt>();
    s->kind = pl::StmtKind::ForLoop;
    s->loc = L;
    s->for_var = var;
    s->for_lo = lo;
    s->for_hi = hi;
    parseBlock(s->body);
    return s;
  }
  if (cur().kind == Tok::KwIf) {
    consume();
    expect(Tok::LParen, "'('");
    auto pred = parseExpr();
    // Allow `c == k` shape.
    if (cur().kind == Tok::EqEq) {
      consume();
      auto rhs = parseExpr();
      auto cmp = std::make_shared<pl::Expr>();
      cmp->kind = pl::ExprKind::CmpEq;
      cmp->children = {pred, rhs};
      cmp->loc = L;
      pred = cmp;
    }
    expect(Tok::RParen, "')'");
    expect(Tok::LBrace, "'{'");
    auto s = std::make_shared<pl::Stmt>();
    s->kind = pl::StmtKind::IfStmt;
    s->loc = L;
    s->predicate = pred;
    parseBlock(s->then_body);
    if (accept(Tok::KwElse)) {
      expect(Tok::LBrace, "'{'");
      parseBlock(s->else_body);
    }
    return s;
  }
  // Type-led declaration: int/double/QReg/Bit.
  if (cur().kind == Tok::KwInt || cur().kind == Tok::KwDouble ||
      cur().kind == Tok::KwQReg || cur().kind == Tok::KwBit) {
    auto t = parseType();
    if (!t) return nullptr;
    if (cur().kind != Tok::Ident) {
      err("expected variable name");
      return nullptr;
    }
    auto s = std::make_shared<pl::Stmt>();
    s->kind = pl::StmtKind::VarDecl;
    s->loc = L;
    s->decl_type = *t;
    s->name = consume().text;
    if (s->decl_type.kind == pl::TypeKind::QReg && accept(Tok::LParen)) {
      if (cur().kind != Tok::Integer) {
        err("QReg constructor expects integer literal");
        return nullptr;
      }
      s->decl_type.qreg_size = static_cast<std::uint32_t>(
          std::stoll(consume().text));
      expect(Tok::RParen, "')'");
    } else if (accept(Tok::Eq)) {
      s->init = parseExpr();
    }
    expect(Tok::Semi, "';'");
    return s;
  }
  // Identifier-led: gate / lib / measure call (`q.method(args);`) or
  // assignment (`var = expr;`).
  if (cur().kind == Tok::Ident) {
    std::string name = cur().text;
    if (peek().kind == Tok::Dot) {
      consume(); consume();
      if (cur().kind != Tok::Ident) {
        err("expected method name after '.'");
        return nullptr;
      }
      std::string method = consume().text;
      expect(Tok::LParen, "'('");
      std::vector<pl::ExprPtr> args;
      if (cur().kind != Tok::RParen) {
        args.push_back(parseExpr());
        while (accept(Tok::Comma)) args.push_back(parseExpr());
      }
      expect(Tok::RParen, "')'");
      expect(Tok::Semi, "';'");
      auto s = std::make_shared<pl::Stmt>();
      // We don't consult the gate-method whitelist here; the lowerer
      // will resolve it the same way as the .pho path.
      s->kind = (method == "measure" || method == "measure_int")
                    ? (method == "measure_int" ? pl::StmtKind::MeasureInt
                                                : pl::StmtKind::MeasureAll)
                    : pl::StmtKind::GateCall;
      // If the method matches a library routine, prefer LibCall so the
      // lowerer expands it.
      static const auto isLib = [](const std::string& m) {
        return m == "bell_pair" || m == "ghz" || m == "qft" || m == "iqft" ||
               m == "grover" || m == "teleport" || m == "vqe_ansatz";
      };
      if (s->kind == pl::StmtKind::GateCall && isLib(method))
        s->kind = pl::StmtKind::LibCall;
      s->loc = L;
      s->receiver = std::move(name);
      s->method = std::move(method);
      s->args = std::move(args);
      return s;
    }
    if (peek().kind == Tok::Eq) {
      std::string lhs = consume().text;
      consume();  // '='
      auto e = parseExpr();
      expect(Tok::Semi, "';'");
      auto s = std::make_shared<pl::Stmt>();
      s->kind = pl::StmtKind::Assign;
      s->loc = L;
      s->name = std::move(lhs);
      s->init = e;
      return s;
    }
    // Fall-through: unrecognised statement; surface a diagnostic.
    err("unsupported statement starting with '" + name + "'");
    while (cur().kind != Tok::Semi && cur().kind != Tok::Eof) consume();
    accept(Tok::Semi);
    return nullptr;
  }
  err("unexpected token '" + cur().text + "' in statement");
  consume();
  return nullptr;
}

}  // namespace (anonymous)

IngestResult ingestCpp(std::string_view source,
                       std::string_view entry,
                       std::string_view target) {
  IngestResult r;
  auto toks = tokenize(source);
  Parser p(std::move(toks), "<cpp>");

  pl::Module m;
  m.target = std::string(target);

  // Top-level loop. We tolerate non-kernel functions (skip them) and
  // ignore everything outside function definitions.
  while (!p.fatal && p.cur().kind != Tok::Eof) {
    bool isKernel = false;
    // Match leading attributes; multiple `[[...]]` allowed.
    while (p.cur().kind == Tok::AttrOpen) {
      if (p.skipAttribute()) isKernel = true;
    }
    p.matchKernelAttr();  // GCC __attribute__ form, accepted but ignored.

    // Match a type token to start a declaration.
    if (p.cur().kind != Tok::KwInt && p.cur().kind != Tok::KwDouble &&
        p.cur().kind != Tok::KwVoid && p.cur().kind != Tok::KwAuto) {
      // Skip everything up to the next top-level semicolon or `}`.
      // Tolerates `using`, `namespace`, etc.
      if (p.cur().kind == Tok::Eof) break;
      // Skip a single token to avoid infinite loop on garbage.
      p.consume();
      continue;
    }
    // Look ahead: type then identifier then `(`.
    auto fopt = p.parseFunction(isKernel);
    if (!fopt) continue;
    if (fopt->is_kernel || fopt->name == std::string(entry)) {
      // Promote to kernel even if marker absent but name matches entry.
      fopt->is_kernel = true;
      m.functions.push_back(std::move(*fopt));
    }
    // Otherwise drop the parsed function (we still walked the body).
  }

  r.diag = std::move(p.diag);
  bool err = false;
  for (const auto& d : r.diag.items()) {
    if (d.severity == pl::DiagSeverity::Error) { err = true; break; }
  }
  if (!err) {
    if (m.functions.empty()) {
      r.diag.error("no [[photon::kernel]]-marked function named '" +
                       std::string(entry) + "' found",
                   {std::string("<cpp>"), 0, 0});
    } else {
      r.module = std::move(m);
    }
  }
  return r;
}

}  // namespace photon::cppfront

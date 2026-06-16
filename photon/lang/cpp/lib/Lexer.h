// photon/lang/cpp/lib/Lexer.h
//
// Photon (.pho) lexer. Tokens for the OO surface: keywords (target,
// kernel, def, return, for, if, else, in), built-in types (int,
// angle, QReg, Bit), gate-method identifiers, punctuation, and
// literals. Comments: `//` to end-of-line and `/* */` blocks
// (C-family, to feel natural to the C++ frontend audience), plus
// `#` and `;` (mirroring Phonon and shell syntax).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace photon::lang {

enum class Tok : std::uint8_t {
  // Keywords
  Target,
  Kernel,
  Def,
  Return,
  For,
  If,
  Else,
  In,
  // Type keywords
  Int,
  Angle,
  Bit,
  QReg,    // matches the literal identifier "QReg" so the AST can
           // distinguish constructor calls from generic identifiers.
  // Built-ins
  Pi,
  // Identifiers / literals
  Ident,
  Integer,
  Real,
  StringLit,
  // Punctuation
  LParen, RParen,
  LBrace, RBrace,
  LBracket, RBracket,
  Comma,
  Dot,
  Semicolon,
  Colon,
  Arrow,           // ->
  // Operators
  Equals,
  EqEq,
  NotEq,
  Lt, Gt, Le, Ge,
  Plus, Minus, Star, Slash,
  DotDot,          // .. (range)
  // Layout
  Newline,
  Eof,
};

struct Token {
  Tok kind = Tok::Eof;
  std::string text;
  std::uint32_t line = 0;
  std::uint32_t column = 0;
};

class Lexer {
 public:
  explicit Lexer(std::string_view src) : src_(src) {}
  std::vector<Token> tokenize();

 private:
  std::string_view src_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t col_ = 1;

  char peek(std::size_t k = 0) const {
    return (pos_ + k) < src_.size() ? src_[pos_ + k] : '\0';
  }
  char get();
  bool match(char c);
  void skipLineComment();
  void skipBlockComment();
  Token readWord();
  Token readNumber(char first);
  Token readString();
};

bool isPhotonGateMethod(std::string_view word);

}  // namespace photon::lang

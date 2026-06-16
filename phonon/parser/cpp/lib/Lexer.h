// phonon/parser/cpp/lib/Lexer.h

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace phonon::parser {

enum class Tok : std::uint8_t {
  Target,
  Generic,
  Qubit,
  Bit,
  Int,         // phonon
  Angle,       // phonon
  Measure,
  Reset,
  Barrier,
  Pi,
  // Phonon control flow + function keywords
  If,
  Else,
  For,
  While,
  Def,
  Return,
  In,
  // identifiers/literals
  GateName,
  Identifier,
  Integer,
  Real,
  // structural punctuation (Spinor + Phonon)
  LBracket, RBracket,
  LParen, RParen,
  LBrace, RBrace,        // phonon
  Comma,
  Equals,                // single '='
  EqEq,                  // ==
  NotEq,                 // !=
  Lt, Gt, Le, Ge,        // comparisons
  Plus, Minus, Star, Slash,
  DotDot,                // ..   (range operator in `for i in lo..hi`)
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
  Token readWord();
  Token readNumber(char first);
};

bool isGateMnemonic(std::string_view word);

}  // namespace phonon::parser

// spinor/parser/cpp/lib/Lexer.h
//
// Token stream for the Spinor production parser. The lexer
// emits NEWLINE tokens explicitly because Spinor is line-oriented
// (statements terminate with newlines, never semicolons).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spinor::parser {

enum class Tok : std::uint8_t {
  Target,
  Generic,
  Qubit,
  Bit,
  Measure,
  Reset,
  Barrier,
  Pi,
  GateName,    // h, x, ..., u1q
  Identifier,  // any other ident
  Integer,
  Real,        // decimal, possibly negative
  LBracket,    // [
  RBracket,    // ]
  LParen,
  RParen,
  Comma,
  Equals,
  Star,
  Slash,
  Minus,
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

  char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
  char get();
  bool match(char c);
  void skipLineComment();
  Token readWord();
  Token readNumber(char first);
};

bool isGateMnemonic(std::string_view word);

}  // namespace spinor::parser

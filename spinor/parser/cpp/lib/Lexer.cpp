// spinor/parser/cpp/lib/Lexer.cpp

#include "Lexer.h"

#include <cctype>
#include <set>

namespace spinor::parser {

namespace {

const std::set<std::string>& gateMnemonics() {
  static const std::set<std::string> s = {
      "h",    "x",    "y",    "z",    "s",    "sdg",  "t",    "tdg",
      "rx",   "ry",   "rz",
      "cx",   "cz",   "swap",
      "ecr",  "ms",   "rzz",  "sx",   "sxdg",
      "gpi",  "gpi2", "u1q",
  };
  return s;
}

}  // namespace

bool isGateMnemonic(std::string_view word) {
  return gateMnemonics().count(std::string(word)) > 0;
}

char Lexer::get() {
  if (pos_ >= src_.size()) return '\0';
  char c = src_[pos_++];
  if (c == '\n') { ++line_; col_ = 1; }
  else { ++col_; }
  return c;
}

bool Lexer::match(char c) {
  if (peek() == c) { get(); return true; }
  return false;
}

void Lexer::skipLineComment() {
  // Caller has already seen ';'.
  while (pos_ < src_.size() && src_[pos_] != '\n') {
    ++pos_;
    ++col_;
  }
}

Token Lexer::readWord() {
  Token t;
  t.line = line_;
  t.column = col_;
  std::string word;
  while (pos_ < src_.size()) {
    char c = src_[pos_];
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
      word.push_back(c);
      get();
    } else break;
  }
  t.text = word;
  if (word == "target")       t.kind = Tok::Target;
  else if (word == "generic") t.kind = Tok::Generic;
  else if (word == "qubit")   t.kind = Tok::Qubit;
  else if (word == "bit")     t.kind = Tok::Bit;
  else if (word == "measure") t.kind = Tok::Measure;
  else if (word == "reset")   t.kind = Tok::Reset;
  else if (word == "barrier") t.kind = Tok::Barrier;
  else if (word == "pi")      t.kind = Tok::Pi;
  else if (isGateMnemonic(word)) t.kind = Tok::GateName;
  else                        t.kind = Tok::Identifier;
  return t;
}

Token Lexer::readNumber(char first) {
  Token t;
  t.line = line_;
  t.column = col_;
  std::string num;
  if (first == '-') {
    num.push_back('-');
    get();  // consume '-'
  }
  bool sawDot = false;
  while (pos_ < src_.size()) {
    char c = src_[pos_];
    if (std::isdigit(static_cast<unsigned char>(c))) {
      num.push_back(c);
      get();
    } else if (c == '.' && !sawDot) {
      sawDot = true;
      num.push_back(c);
      get();
    } else if (c == 'e' || c == 'E') {
      num.push_back(c);
      get();
      if (peek() == '+' || peek() == '-') {
        num.push_back(get());
      }
    } else break;
  }
  t.text = num;
  t.kind = sawDot ? Tok::Real : Tok::Integer;
  return t;
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  while (pos_ < src_.size()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') { get(); continue; }
    if (c == ';') { skipLineComment(); continue; }
    if (c == '\n') {
      Token t; t.kind = Tok::Newline; t.line = line_; t.column = col_;
      get();
      // Collapse runs of newlines to a single token.
      while (pos_ < src_.size() &&
             (src_[pos_] == '\n' || src_[pos_] == ' ' ||
              src_[pos_] == '\t' || src_[pos_] == '\r' ||
              src_[pos_] == ';')) {
        if (src_[pos_] == ';') { skipLineComment(); continue; }
        get();
      }
      out.push_back(std::move(t));
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      out.push_back(readWord());
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      out.push_back(readNumber('+'));
      continue;
    }
    if (c == '-') {
      // Could be a unary minus on a number, or part of `-pi`. We
      // tokenise as Minus and let the parser decide.
      Token t; t.kind = Tok::Minus; t.text = "-";
      t.line = line_; t.column = col_;
      get();
      out.push_back(std::move(t));
      continue;
    }
    Token t; t.line = line_; t.column = col_;
    t.text = std::string(1, c);
    switch (c) {
      case '[': t.kind = Tok::LBracket; break;
      case ']': t.kind = Tok::RBracket; break;
      case '(': t.kind = Tok::LParen;   break;
      case ')': t.kind = Tok::RParen;   break;
      case ',': t.kind = Tok::Comma;    break;
      case '=': t.kind = Tok::Equals;   break;
      case '*': t.kind = Tok::Star;     break;
      case '/': t.kind = Tok::Slash;    break;
      default:  t.kind = Tok::Eof;      break;  // unknown char
    }
    get();
    if (t.kind != Tok::Eof) {
      out.push_back(std::move(t));
    }
  }
  Token end; end.kind = Tok::Eof; end.line = line_; end.column = col_;
  out.push_back(std::move(end));
  return out;
}

}  // namespace spinor::parser

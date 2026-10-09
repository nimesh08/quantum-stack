// phonon/parser/cpp/lib/Lexer.cpp

#include "Lexer.h"

#include <cctype>
#include <set>

namespace phonon::parser {

namespace {

const std::set<std::string>& gateMnemonics() {
  static const std::set<std::string> s = {
      "h",    "x",    "y",    "z",    "s",    "sdg",  "t",    "tdg",
      "rx",   "ry",   "rz",
      "cx",   "cz",   "swap",
      "ecr",  "ms",   "rzz", "rxx",  "sx",   "sxdg",
      "gpi",  "gpi2", "u1q", "gphase",
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
  // Caller has already seen ';' (Spinor-style) or "//" (Phonon-friendly).
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
  if      (word == "target")  t.kind = Tok::Target;
  else if (word == "generic") t.kind = Tok::Generic;
  else if (word == "qubit")   t.kind = Tok::Qubit;
  else if (word == "bit")     t.kind = Tok::Bit;
  else if (word == "int")     t.kind = Tok::Int;
  else if (word == "angle")   t.kind = Tok::Angle;
  else if (word == "bool")    t.kind = Tok::Bool;
  else if (word == "uint")    t.kind = Tok::UInt;
  else if (word == "output")  t.kind = Tok::Output;
  else if (word == "bounded") t.kind = Tok::Bounded;
  else if (word == "max_iterations") t.kind = Tok::MaxIterations;
  else if (word == "break")   t.kind = Tok::Break;
  else if (word == "continue") t.kind = Tok::Continue;
  else if (word == "discard") t.kind = Tok::Discard;
  else if (word == "measure") t.kind = Tok::Measure;
  else if (word == "reset")   t.kind = Tok::Reset;
  else if (word == "barrier") t.kind = Tok::Barrier;
  else if (word == "pi")      t.kind = Tok::Pi;
  else if (word == "if")      t.kind = Tok::If;
  else if (word == "else")    t.kind = Tok::Else;
  else if (word == "for")     t.kind = Tok::For;
  else if (word == "while")   t.kind = Tok::While;
  else if (word == "def")     t.kind = Tok::Def;
  else if (word == "return")  t.kind = Tok::Return;
  else if (word == "in")      t.kind = Tok::In;
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
    get();
  }
  bool sawDot = false;
  bool sawExponent = false, valid = true;
  while (std::isdigit(static_cast<unsigned char>(peek()))) num.push_back(get());
  // ".." is the range operator, not a decimal point.
  if (peek() == '.' && peek(1) != '.') {
    sawDot = true; num.push_back(get());
    while (std::isdigit(static_cast<unsigned char>(peek()))) num.push_back(get());
  }
  if (peek() == 'e' || peek() == 'E') {
    sawExponent = true; num.push_back(get());
    if (peek() == '+' || peek() == '-') num.push_back(get());
    valid = std::isdigit(static_cast<unsigned char>(peek())) != 0;
    while (std::isdigit(static_cast<unsigned char>(peek()))) num.push_back(get());
  }
  t.text = num;
  t.kind = !valid ? Tok::Invalid : (sawDot || sawExponent) ? Tok::Real : Tok::Integer;
  return t;
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  while (pos_ < src_.size()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') { get(); continue; }
    if (c == '"') {
      Token t; t.kind = Tok::String; t.line = line_; t.column = col_;
      get();
      bool closed = false;
      while (pos_ < src_.size()) {
        char value = get();
        if (value == '"') { closed = true; break; }
        if (value == '\n' || value == '\r') { t.kind = Tok::Invalid; break; }
        if (value == '\\') {
          if (pos_ >= src_.size()) break;
          value = get();
          if (value != '"' && value != '\\' && value != '/') t.kind = Tok::Invalid;
        }
        t.text.push_back(value);
      }
      if (!closed) t.kind = Tok::Invalid;
      out.push_back(std::move(t));
      continue;
    }
    if (c == ';') { skipLineComment(); continue; }
    if (c == '/' && peek(1) == '/') { get(); get(); skipLineComment(); continue; }
    if (c == '\n') {
      Token t; t.kind = Tok::Newline; t.line = line_; t.column = col_;
      get();
      while (pos_ < src_.size() &&
             (src_[pos_] == '\n' || src_[pos_] == ' ' ||
              src_[pos_] == '\t' || src_[pos_] == '\r' ||
              src_[pos_] == ';' ||
              (src_[pos_] == '/' && pos_ + 1 < src_.size() &&
               src_[pos_ + 1] == '/'))) {
        if (src_[pos_] == ';') { skipLineComment(); continue; }
        if (src_[pos_] == '/' && pos_ + 1 < src_.size() &&
            src_[pos_ + 1] == '/') {
          get(); get(); skipLineComment(); continue;
        }
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
      Token t; t.kind = Tok::Minus; t.text = "-";
      t.line = line_; t.column = col_;
      get();
      out.push_back(std::move(t));
      continue;
    }
    if (c == '.' && peek(1) == '.') {
      Token t; t.kind = Tok::DotDot; t.text = "..";
      t.line = line_; t.column = col_;
      get(); get();
      out.push_back(std::move(t));
      continue;
    }
    if (c == '=' && peek(1) == '=') {
      Token t; t.kind = Tok::EqEq; t.text = "==";
      t.line = line_; t.column = col_;
      get(); get(); out.push_back(std::move(t)); continue;
    }
    if (c == '!' && peek(1) == '=') {
      Token t; t.kind = Tok::NotEq; t.text = "!=";
      t.line = line_; t.column = col_;
      get(); get(); out.push_back(std::move(t)); continue;
    }
    if (c == '<' && peek(1) == '=') {
      Token t; t.kind = Tok::Le; t.text = "<=";
      t.line = line_; t.column = col_;
      get(); get(); out.push_back(std::move(t)); continue;
    }
    if (c == '>' && peek(1) == '=') {
      Token t; t.kind = Tok::Ge; t.text = ">=";
      t.line = line_; t.column = col_;
      get(); get(); out.push_back(std::move(t)); continue;
    }
    if ((c == '<' || c == '>') && peek(1) == c) {
      Token t; t.kind=c=='<'?Tok::Shl:Tok::Shr;t.text=std::string(2,c);t.line=line_;t.column=col_;
      get();get();out.push_back(std::move(t));continue;
    }
    Token t; t.line = line_; t.column = col_;
    t.text = std::string(1, c);
    switch (c) {
      case '[': t.kind = Tok::LBracket; break;
      case ']': t.kind = Tok::RBracket; break;
      case '(': t.kind = Tok::LParen;   break;
      case ')': t.kind = Tok::RParen;   break;
      case '{': t.kind = Tok::LBrace;   break;
      case '}': t.kind = Tok::RBrace;   break;
      case ',': t.kind = Tok::Comma;    break;
      case '=': t.kind = Tok::Equals;   break;
      case '<': t.kind = Tok::Lt;       break;
      case '>': t.kind = Tok::Gt;       break;
      case '+': t.kind = Tok::Plus;     break;
      case '*': t.kind = Tok::Star;     break;
      case '/': t.kind = Tok::Slash;    break;
      case '&': t.kind = Tok::Amp;      break;
      case '|': t.kind = Tok::Pipe;     break;
      case '^': t.kind = Tok::Caret;    break;
      case '!': t.kind = Tok::Bang;     break;
      case '~': t.kind = Tok::Tilde;    break;
      default:  t.kind = Tok::Invalid;  break;
    }
    get();
    out.push_back(std::move(t));
  }
  Token end; end.kind = Tok::Eof; end.line = line_; end.column = col_;
  out.push_back(std::move(end));
  return out;
}

}  // namespace phonon::parser

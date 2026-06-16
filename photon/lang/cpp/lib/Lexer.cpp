// photon/lang/cpp/lib/Lexer.cpp

#include "Lexer.h"

#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace photon::lang {

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
  while (pos_ < src_.size() && peek() != '\n') ++pos_;
}

void Lexer::skipBlockComment() {
  // We've already consumed the '/*'.
  while (pos_ < src_.size()) {
    if (peek() == '*' && peek(1) == '/') {
      get(); get();
      return;
    }
    get();
  }
}

namespace {

const std::unordered_map<std::string, Tok>& keywords() {
  static const std::unordered_map<std::string, Tok> kw{
      {"target",    Tok::Target},
      {"kernel",    Tok::Kernel},
      {"def",       Tok::Def},
      {"return",    Tok::Return},
      {"for",       Tok::For},
      {"if",        Tok::If},
      {"else",      Tok::Else},
      {"in",        Tok::In},
      {"int",       Tok::Int},
      {"angle",     Tok::Angle},
      {"bit",       Tok::Bit},
      {"Bit",       Tok::Bit},
      {"QReg",      Tok::QReg},
      {"pi",        Tok::Pi},
  };
  return kw;
}

const std::unordered_set<std::string>& gateMethods() {
  static const std::unordered_set<std::string> gates{
      "h", "x", "y", "z", "s", "sdg", "t", "tdg",
      "rx", "ry", "rz",
      "cx", "cz", "swap",
      "sx", "sxdg",
      "ecr", "ms", "rzz",
      "gpi", "gpi2", "u1q",
      // Photon convenience aliases:
      "cnot",        // alias for cx
      "hadamard",    // alias for h
      "phase",       // alias for s
  };
  return gates;
}

}  // namespace

bool isPhotonGateMethod(std::string_view word) {
  return gateMethods().contains(std::string(word));
}

Token Lexer::readWord() {
  std::uint32_t l = line_, c = col_;
  std::string s;
  while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') {
    s.push_back(get());
  }
  if (auto it = keywords().find(s); it != keywords().end()) {
    return {it->second, std::move(s), l, c};
  }
  return {Tok::Ident, std::move(s), l, c};
}

Token Lexer::readNumber(char first) {
  std::uint32_t l = line_, c = col_ - 1;
  std::string s;
  s.push_back(first);
  bool is_real = false;
  while (std::isdigit(static_cast<unsigned char>(peek()))) s.push_back(get());
  // Handle '..' range vs '.5' fractional.
  if (peek() == '.' && peek(1) != '.') {
    is_real = true;
    s.push_back(get());  // '.'
    while (std::isdigit(static_cast<unsigned char>(peek()))) s.push_back(get());
  }
  if (peek() == 'e' || peek() == 'E') {
    is_real = true;
    s.push_back(get());
    if (peek() == '+' || peek() == '-') s.push_back(get());
    while (std::isdigit(static_cast<unsigned char>(peek()))) s.push_back(get());
  }
  return {is_real ? Tok::Real : Tok::Integer, std::move(s), l, c};
}

Token Lexer::readString() {
  std::uint32_t l = line_, c = col_ - 1;
  std::string s;
  while (pos_ < src_.size() && peek() != '"') {
    if (peek() == '\\' && peek(1)) {
      get();  // backslash
      char n = get();
      switch (n) {
        case 'n': s.push_back('\n'); break;
        case 't': s.push_back('\t'); break;
        case '"': s.push_back('"'); break;
        case '\\': s.push_back('\\'); break;
        default: s.push_back(n); break;
      }
    } else {
      s.push_back(get());
    }
  }
  if (peek() == '"') get();
  return {Tok::StringLit, std::move(s), l, c};
}

std::vector<Token> Lexer::tokenize() {
  std::vector<Token> out;
  while (pos_ < src_.size()) {
    char c = peek();
    if (c == ' ' || c == '\t' || c == '\r') { get(); continue; }
    if (c == '\n') {
      out.push_back({Tok::Newline, "\n", line_, col_});
      get();
      continue;
    }
    if (c == '#' || c == ';') { skipLineComment(); continue; }
    if (c == '/' && peek(1) == '/') { skipLineComment(); continue; }
    if (c == '/' && peek(1) == '*') {
      get(); get();
      skipBlockComment();
      continue;
    }
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      out.push_back(readWord());
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      char d = get();
      out.push_back(readNumber(d));
      continue;
    }
    if (c == '"') { get(); out.push_back(readString()); continue; }

    std::uint32_t l = line_, col = col_;
    char ch = get();
    auto emit = [&](Tok k, std::string t) {
      out.push_back({k, std::move(t), l, col});
    };
    switch (ch) {
      case '(': emit(Tok::LParen, "("); break;
      case ')': emit(Tok::RParen, ")"); break;
      case '{': emit(Tok::LBrace, "{"); break;
      case '}': emit(Tok::RBrace, "}"); break;
      case '[': emit(Tok::LBracket, "["); break;
      case ']': emit(Tok::RBracket, "]"); break;
      case ',': emit(Tok::Comma, ","); break;
      case '.':
        if (match('.')) emit(Tok::DotDot, "..");
        else emit(Tok::Dot, ".");
        break;
      case ':': emit(Tok::Colon, ":"); break;
      case '+': emit(Tok::Plus, "+"); break;
      case '-':
        if (match('>')) emit(Tok::Arrow, "->");
        else emit(Tok::Minus, "-");
        break;
      case '*': emit(Tok::Star, "*"); break;
      case '/': emit(Tok::Slash, "/"); break;
      case '=':
        if (match('=')) emit(Tok::EqEq, "==");
        else emit(Tok::Equals, "=");
        break;
      case '!':
        if (match('=')) emit(Tok::NotEq, "!=");
        else emit(Tok::Ident, "!");  // unused but harmless.
        break;
      case '<':
        if (match('=')) emit(Tok::Le, "<=");
        else emit(Tok::Lt, "<");
        break;
      case '>':
        if (match('=')) emit(Tok::Ge, ">=");
        else emit(Tok::Gt, ">");
        break;
      default:
        // Unknown character: emit as identifier text so the parser
        // can produce a precise diagnostic.
        emit(Tok::Ident, std::string(1, ch));
        break;
    }
  }
  out.push_back({Tok::Eof, "", line_, col_});
  return out;
}

}  // namespace photon::lang

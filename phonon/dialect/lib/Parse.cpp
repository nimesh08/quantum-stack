// phonon/dialect/lib/Parse.cpp
//
// Textual parser for the Phonon dialect. Round-trippable
// against Print.cpp. Used in M1 round-trip tests only; the
// production parser is a separate component (M2).

#include "phonon/dialect/Phonon.h"

#include <cctype>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace phonon::dialect {

namespace {

struct Cursor {
  std::string_view s;
  std::size_t i = 0;
  std::uint32_t line = 1, col = 1;

  void advance(std::size_t n) {
    for (std::size_t k = 0; k < n && i < s.size(); ++k, ++i) {
      if (s[i] == '\n') { ++line; col = 1; } else { ++col; }
    }
  }
  void skipSpace() {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' ||
                            s[i] == '\n' || s[i] == '\r')) {
      advance(1);
    }
  }
  bool eat(std::string_view tok) {
    skipSpace();
    if (s.substr(i, tok.size()) == tok) { advance(tok.size()); return true; }
    return false;
  }
  std::string readUntil(char delim) {
    skipSpace();
    std::size_t start = i;
    while (i < s.size() && s[i] != delim) advance(1);
    return std::string(s.substr(start, i - start));
  }
  std::string readWord() {
    skipSpace();
    std::size_t start = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                            s[i] == '_' || s[i] == '.')) {
      advance(1);
    }
    return std::string(s.substr(start, i - start));
  }
  // %name ; returns "name" (without the %).
  std::string readValueRef() {
    skipSpace();
    if (i >= s.size() || s[i] != '%') return "";
    advance(1);
    std::size_t start = i;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                            s[i] == '_')) {
      advance(1);
    }
    return std::string(s.substr(start, i - start));
  }
  // Read a quoted string "..."; returns content without quotes.
  std::optional<std::string> readQuoted() {
    skipSpace();
    if (i >= s.size() || s[i] != '"') return std::nullopt;
    advance(1);
    std::size_t start = i;
    while (i < s.size() && s[i] != '"') advance(1);
    std::string out(s.substr(start, i - start));
    if (i < s.size()) advance(1);  // consume closing "
    return out;
  }
  // Read a number (double); returns nullopt if no number.
  std::optional<double> readNumber() {
    skipSpace();
    std::size_t start = i;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) advance(1);
    bool any = false;
    while (i < s.size() &&
           (std::isdigit(static_cast<unsigned char>(s[i])) ||
            s[i] == '.' || s[i] == 'e' || s[i] == 'E' ||
            s[i] == '+' || s[i] == '-')) {
      // be permissive for scientific notation
      char c = s[i];
      if ((c == '+' || c == '-') && i > start &&
          s[i - 1] != 'e' && s[i - 1] != 'E') break;
      advance(1);
      any = true;
    }
    if (!any) return std::nullopt;
    try {
      return std::stod(std::string(s.substr(start, i - start)));
    } catch (...) {
      return std::nullopt;
    }
  }
  bool atEnd() const { return i >= s.size(); }
};

// Build a mnemonic→OpKind map.
std::unordered_map<std::string, OpKind>& mnemonicMap() {
  static std::unordered_map<std::string, OpKind> m;
  if (!m.empty()) return m;
  for (int k = 0; k <= static_cast<int>(OpKind::Assign); ++k) {
    OpKind ok = static_cast<OpKind>(k);
    m[std::string(opMnemonic(ok))] = ok;
  }
  return m;
}

Type parseTypeName(std::string_view t) {
  if (t == "!spinor.qubit") return qubitType();
  if (t == "!spinor.bit")   return bitType();
  if (t == "!phonon.int")   return intType();
  if (t == "!phonon.angle") return angleType();
  if (t == "!phonon.func")  return funcType();
  return qubitType();  // fallback
}

}  // namespace

std::optional<Module> parse(std::string_view text, Diagnostics& diag) {
  Cursor c{text};

  Module m;

  // header: phonon.module @<name> attributes {target = "..."} {
  if (!c.eat("phonon.module")) {
    diag.error("expected 'phonon.module'");
    return std::nullopt;
  }
  c.skipSpace();
  if (!c.eat("@")) { diag.error("expected '@'"); return std::nullopt; }
  m.name = c.readWord();
  c.eat("attributes");
  c.eat("{");
  c.eat("target");
  c.eat("=");
  if (auto q = c.readQuoted()) m.targetAttr = *q;
  c.eat("}");
  c.eat("{");

  // value-name → ValueId map (for operand resolution).
  std::unordered_map<std::string, ValueId> nameToId;

  while (!c.atEnd()) {
    c.skipSpace();
    if (c.eat("}")) break;

    // Optional results: %a, %b = mnemonic operands {attrs} : type1, type2
    std::vector<std::string> resultNames;
    std::size_t save = c.i;
    std::uint32_t saveLine = c.line, saveCol = c.col;
    while (true) {
      c.skipSpace();
      if (c.i >= c.s.size() || c.s[c.i] != '%') break;
      std::string n = c.readValueRef();
      if (n.empty()) break;
      resultNames.push_back(n);
      c.skipSpace();
      if (c.eat(",")) continue;
      if (c.eat("=")) break;
      // Not a result-list — restore.
      resultNames.clear();
      c.i = save; c.line = saveLine; c.col = saveCol;
      break;
    }

    c.skipSpace();
    std::string mnemonic = c.readWord();
    if (mnemonic.empty()) { diag.error("expected op mnemonic"); break; }

    auto it = mnemonicMap().find(mnemonic);
    if (it == mnemonicMap().end()) {
      diag.error("unknown op mnemonic: " + mnemonic);
      return std::nullopt;
    }
    OpKind kind = it->second;

    // Operands: %a, %b, %c (until '{' or ':' or newline)
    std::vector<ValueId> operands;
    while (true) {
      c.skipSpace();
      if (c.i >= c.s.size()) break;
      char ch = c.s[c.i];
      if (ch == '{' || ch == ':' || ch == '\n') break;
      if (ch != '%') break;
      std::string n = c.readValueRef();
      auto vit = nameToId.find(n);
      if (vit == nameToId.end()) {
        diag.error("unknown value: %" + n);
        return std::nullopt;
      }
      operands.push_back(vit->second);
      c.skipSpace();
      if (!c.eat(",")) break;
    }

    // Attributes
    std::vector<Attribute> attrs;
    if (c.eat("{")) {
      while (true) {
        c.skipSpace();
        if (c.eat("}")) break;
        std::string key = c.readWord();
        c.eat("=");
        c.skipSpace();
        if (c.i < c.s.size() && c.s[c.i] == '"') {
          if (auto q = c.readQuoted()) attrs.push_back({key, *q});
        } else if (auto n = c.readNumber()) {
          attrs.push_back({key, *n});
        }
        c.skipSpace();
        c.eat(",");
      }
    }

    // Result types
    std::vector<Type> resultTypes;
    if (c.eat(":")) {
      while (true) {
        c.skipSpace();
        if (c.i >= c.s.size() || c.s[c.i] != '!') break;
        c.advance(1);  // consume '!'
        std::string tn = c.readWord();
        if (tn.empty()) break;
        resultTypes.push_back(parseTypeName(std::string("!") + tn));
        c.skipSpace();
        if (!c.eat(",")) break;
      }
    }

    // Build op + values
    Op op;
    op.kind = kind;
    op.operands = std::move(operands);
    op.attributes = std::move(attrs);
    OpId opId = m.addOp(std::move(op));

    // If no result types declared but result names exist, infer from
    // op kind defaults (covers the case where the printer omitted ":"
    // for ops with single non-qubit results — but our printer always
    // emits result types when results exist, so this is a fallback).
    if (resultTypes.empty() && !resultNames.empty()) {
      for (std::size_t k = 0; k < resultNames.size(); ++k) {
        resultTypes.push_back(qubitType());
      }
    }

    Op& live = m.opMut(opId);
    for (std::size_t k = 0; k < resultTypes.size(); ++k) {
      ValueId vid = m.addValue(resultTypes[k], opId);
      live.results.push_back(vid);
      if (k < resultNames.size()) {
        m.setName(vid, resultNames[k]);
        nameToId[resultNames[k]] = vid;
      }
    }

    c.skipSpace();
  }

  return m;
}

}  // namespace phonon::dialect

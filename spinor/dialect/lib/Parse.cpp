// spinor/dialect/lib/Parse.cpp
//
// Textual format parser for the Spinor dialect (consumed by
// round-trip tests). NOT the source-language parser — that is
// M2 (Lark prototype) and M7 (production C++).

#include "spinor/dialect/Spinor.h"

#include <cctype>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace spinor::dialect {

namespace {

class Lexer {
 public:
  explicit Lexer(std::string_view src) : src_(src) {}

  bool atEnd() const { return pos_ >= src_.size(); }
  char peek() const { return atEnd() ? '\0' : src_[pos_]; }
  char get() { return atEnd() ? '\0' : src_[pos_++]; }

  void skipWS() {
    while (!atEnd()) {
      char c = src_[pos_];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        if (c == '\n') { ++line_; col_ = 1; } else { ++col_; }
        ++pos_;
      } else if (c == ';') {
        while (!atEnd() && src_[pos_] != '\n') ++pos_;
      } else {
        break;
      }
    }
  }

  // Greedy identifier-like: letters, digits, underscore, dot.
  std::string readIdent() {
    std::string out;
    while (!atEnd()) {
      char c = src_[pos_];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
          c == '.') {
        out.push_back(c);
        ++pos_;
        ++col_;
      } else break;
    }
    return out;
  }

  // %name — including %v0 forms.
  std::string readValueName() {
    if (peek() != '%') return {};
    ++pos_;
    ++col_;
    return readIdent();
  }

  bool consume(char c) {
    skipWS();
    if (peek() == c) { ++pos_; ++col_; return true; }
    return false;
  }

  bool consumeKeyword(std::string_view kw) {
    skipWS();
    if (src_.substr(pos_).rfind(kw, 0) != 0) return false;
    // require non-ident-continuation after
    std::size_t after = pos_ + kw.size();
    if (after < src_.size()) {
      char c = src_[after];
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
          c == '.') {
        return false;
      }
    }
    pos_ = after;
    return true;
  }

  // Read until matching close-quote (no escaping needed for our format).
  std::optional<std::string> readQuoted() {
    skipWS();
    if (peek() != '"') return std::nullopt;
    ++pos_;
    std::string out;
    while (!atEnd() && src_[pos_] != '"') {
      out.push_back(src_[pos_++]);
    }
    if (atEnd()) return std::nullopt;
    ++pos_;  // closing "
    return out;
  }

  std::optional<double> readDouble() {
    skipWS();
    std::size_t start = pos_;
    // optional sign
    if (peek() == '+' || peek() == '-') { ++pos_; }
    while (!atEnd()) {
      char c = src_[pos_];
      if (std::isdigit(static_cast<unsigned char>(c)) || c == '.' ||
          c == 'e' || c == 'E' || c == '+' || c == '-') {
        ++pos_;
      } else break;
    }
    if (pos_ == start) return std::nullopt;
    double v = 0.0;
    auto r =
        std::from_chars(src_.data() + start, src_.data() + pos_, v);
    if (r.ec != std::errc{}) {
      pos_ = start;
      return std::nullopt;
    }
    return v;
  }

  std::size_t pos() const { return pos_; }
  std::uint32_t line() const { return line_; }
  std::uint32_t col() const { return col_; }

 private:
  std::string_view src_;
  std::size_t pos_ = 0;
  std::uint32_t line_ = 1;
  std::uint32_t col_ = 1;
};

OpKind opFromMnemonic(std::string_view mnemonic) {
  // Linear scan over the constexpr table; the set is small.
  static const struct Entry {
    OpKind k;
    const char* name;
  } table[] = {
      {OpKind::AllocQubit, "spinor.alloc_qubit"},
      {OpKind::AllocBit,   "spinor.alloc_bit"},
      {OpKind::H, "spinor.h"},     {OpKind::X, "spinor.x"},
      {OpKind::Y, "spinor.y"},     {OpKind::Z, "spinor.z"},
      {OpKind::S, "spinor.s"},     {OpKind::Sdg, "spinor.sdg"},
      {OpKind::T, "spinor.t"},     {OpKind::Tdg, "spinor.tdg"},
      {OpKind::Rx, "spinor.rx"},   {OpKind::Ry, "spinor.ry"},
      {OpKind::Rz, "spinor.rz"},
      {OpKind::Cx, "spinor.cx"},   {OpKind::Cz, "spinor.cz"},
      {OpKind::Swap, "spinor.swap"},
      {OpKind::Ecr, "spinor.ecr"}, {OpKind::Ms, "spinor.ms"},
      {OpKind::Rzz, "spinor.rzz"}, {OpKind::Sx, "spinor.sx"},
      {OpKind::Sxdg, "spinor.sxdg"},
      {OpKind::Gpi, "spinor.gpi"}, {OpKind::Gpi2, "spinor.gpi2"},
      {OpKind::U1q, "spinor.u1q"},
      {OpKind::Measure, "spinor.measure"},
      {OpKind::Reset,   "spinor.reset"},
      {OpKind::Barrier, "spinor.barrier"},
  };
  for (const auto& e : table) {
    if (mnemonic == e.name) return e.k;
  }
  return static_cast<OpKind>(0xFFFF);
}

bool isValidOp(OpKind k) {
  return static_cast<std::uint16_t>(k) <
         static_cast<std::uint16_t>(0xFFFF);
}

bool produces(OpKind k, bool& bitOut, int& nq) {
  // Hit the table in Spinor.cpp via opMnemonic; here we mirror.
  switch (k) {
    case OpKind::AllocQubit: nq = 1; bitOut = false; return true;
    case OpKind::AllocBit:   nq = 0; bitOut = true;  return true;
    case OpKind::Measure:    nq = 0; bitOut = true;  return true;
    case OpKind::Reset:      nq = 1; bitOut = false; return true;
    case OpKind::Barrier:    nq = 0; bitOut = false; return true;
    default:
      // gates: qResults == qOperands
      nq = qubitArity(k);
      bitOut = false;
      return nq >= 0;
  }
}

}  // namespace

std::optional<Module> parse(std::string_view text, Diagnostics& diag) {
  Module m;
  std::unordered_map<std::string, ValueId> nameTable;

  Lexer lx(text);
  lx.skipWS();

  if (!lx.consumeKeyword("spinor.module")) {
    diag.error("expected 'spinor.module'", Location{"<text>", lx.line(),
                                                    lx.col()});
    return std::nullopt;
  }
  lx.skipWS();
  if (lx.peek() == '@') {
    lx.get();
    m.name = lx.readIdent();
  }
  lx.skipWS();
  if (!lx.consumeKeyword("attributes")) {
    diag.error("expected 'attributes'", Location{"<text>", lx.line(),
                                                  lx.col()});
    return std::nullopt;
  }
  if (!lx.consume('{')) {
    diag.error("expected '{' after attributes", Location{"<text>",
                                                          lx.line(),
                                                          lx.col()});
    return std::nullopt;
  }
  lx.skipWS();
  if (!lx.consumeKeyword("target")) {
    diag.error("expected 'target' attribute", {});
    return std::nullopt;
  }
  if (!lx.consume('=')) {
    diag.error("expected '=' in target attribute", {});
    return std::nullopt;
  }
  auto qstr = lx.readQuoted();
  if (!qstr) {
    diag.error("expected quoted target id", {});
    return std::nullopt;
  }
  m.targetAttr = *qstr;
  if (!lx.consume('}')) {
    diag.error("expected '}' to close attributes", {});
    return std::nullopt;
  }
  if (!lx.consume('{')) {
    diag.error("expected '{' to open module body", {});
    return std::nullopt;
  }

  // Body
  while (true) {
    lx.skipWS();
    if (lx.peek() == '}') {
      lx.get();
      break;
    }
    if (lx.atEnd()) {
      diag.error("unexpected end of input inside module body", {});
      return std::nullopt;
    }

    // Op line. Either: %a, %b = mnemonic operands ... : types
    //                   mnemonic operands ...               (no results)
    // We try to read result names first; if we then see '=', they're
    // results; if we see an identifier starting with 'spinor.', the
    // line has no results.
    std::vector<std::string> resultNames;
    std::size_t saved = lx.pos();
    if (lx.peek() == '%') {
      while (lx.peek() == '%') {
        std::string n = lx.readValueName();
        if (n.empty()) {
          diag.error("expected result name after '%'",
                     {"<text>", lx.line(), lx.col()});
          return std::nullopt;
        }
        resultNames.push_back(n);
        lx.skipWS();
        if (lx.peek() == ',') { lx.get(); lx.skipWS(); continue; }
        break;
      }
      lx.skipWS();
      if (!lx.consume('=')) {
        // not an assignment: treat the leading %s as part of
        // operands later; re-parse from saved.
        // (But our format always assigns when there's a %. Be
        // defensive.)
        resultNames.clear();
        // Reset lexer by constructing a new one isn't easy; use a
        // private rewind via a fresh lexer. For simplicity we
        // recreate.
        Lexer rewound(text);
        // advance to saved
        while (rewound.pos() < saved) rewound.get();
        lx = rewound;
      }
    }

    lx.skipWS();
    std::string mnemonic;
    while (true) {
      char c = lx.peek();
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' ||
          c == '.') {
        mnemonic.push_back(c);
        lx.get();
      } else break;
    }
    if (mnemonic.empty()) {
      diag.error("expected op mnemonic", {"<text>", lx.line(), lx.col()});
      return std::nullopt;
    }
    OpKind kind = opFromMnemonic(mnemonic);
    if (!isValidOp(kind)) {
      diag.error("unknown op mnemonic: " + mnemonic,
                 {"<text>", lx.line(), lx.col()});
      return std::nullopt;
    }

    // Operands: zero or more %name, separated by ','. Stop at '{'
    // (attributes), ':' (type list), or newline.
    std::vector<std::string> operandNames;
    lx.skipWS();
    while (lx.peek() == '%') {
      std::string n = lx.readValueName();
      if (n.empty()) {
        diag.error("expected operand name after '%'",
                   {"<text>", lx.line(), lx.col()});
        return std::nullopt;
      }
      operandNames.push_back(n);
      lx.skipWS();
      if (lx.peek() == ',') {
        // could be operand separator; peek ahead to see next %
        std::size_t mark = lx.pos();
        lx.get();
        lx.skipWS();
        if (lx.peek() != '%') {
          // not another operand; rewind
          Lexer rewound(text);
          while (rewound.pos() < mark) rewound.get();
          lx = rewound;
          break;
        }
        continue;
      }
      break;
    }

    // Optional attribute block
    std::vector<Attribute> attrs;
    lx.skipWS();
    if (lx.peek() == '{') {
      lx.get();  // {
      while (true) {
        lx.skipWS();
        if (lx.peek() == '}') { lx.get(); break; }
        std::string key = lx.readIdent();
        if (key.empty()) {
          diag.error("expected attribute key", {"<text>", lx.line(),
                                                  lx.col()});
          return std::nullopt;
        }
        lx.skipWS();
        if (!lx.consume('=')) {
          diag.error("expected '=' in attribute", {"<text>", lx.line(),
                                                    lx.col()});
          return std::nullopt;
        }
        lx.skipWS();
        Attribute a;
        a.name = std::move(key);
        if (lx.peek() == '"') {
          auto s = lx.readQuoted();
          if (!s) {
            diag.error("malformed quoted attribute", {});
            return std::nullopt;
          }
          a.value = *s;
        } else {
          auto d = lx.readDouble();
          if (!d) {
            diag.error("expected number or string in attribute",
                       {"<text>", lx.line(), lx.col()});
            return std::nullopt;
          }
          a.value = *d;
        }
        attrs.push_back(std::move(a));
        lx.skipWS();
        if (lx.peek() == ',') { lx.get(); continue; }
      }
    }

    // Optional ': types' list (we ignore the listed types except as
    // a sanity check; the IR types are determined by the op signature).
    lx.skipWS();
    if (lx.peek() == ':') {
      lx.get();
      // consume type tokens until end-of-line or '}'
      while (true) {
        lx.skipWS();
        if (lx.peek() == '}' || lx.peek() == '\n' || lx.atEnd()) break;
        // try to read a type
        if (lx.peek() == '!') lx.get();  // '!'
        std::string ty = lx.readIdent();
        if (ty.empty()) break;
        lx.skipWS();
        if (lx.peek() == ',') { lx.get(); continue; }
        break;
      }
    }

    // Build operand id list
    std::vector<ValueId> operandIds;
    operandIds.reserve(operandNames.size());
    for (const auto& n : operandNames) {
      auto it = nameTable.find(n);
      if (it == nameTable.end()) {
        diag.error("undefined operand %" + n,
                   {"<text>", lx.line(), lx.col()});
        return std::nullopt;
      }
      operandIds.push_back(it->second);
    }

    // Construct op + allocate results.
    int nq = 0; bool bit = false;
    if (!produces(kind, bit, nq)) {
      diag.error("internal: unknown signature for op " + mnemonic, {});
      return std::nullopt;
    }
    Op op;
    op.kind = kind;
    op.operands = std::move(operandIds);
    op.attributes = std::move(attrs);
    OpId opId = m.addOp(std::move(op));
    Op& live = m.opMut(opId);
    int total = nq + (bit ? 1 : 0);
    if (static_cast<int>(resultNames.size()) != total) {
      diag.error("op " + mnemonic + " expects " +
                     std::to_string(total) + " result(s), got " +
                     std::to_string(resultNames.size()),
                 {"<text>", lx.line(), lx.col()});
      return std::nullopt;
    }
    for (int i = 0; i < nq; ++i) {
      ValueId v = m.addValue(qubitType(), opId);
      live.results.push_back(v);
      m.setName(v, resultNames[i]);
      nameTable.emplace(resultNames[i], v);
    }
    if (bit) {
      ValueId v = m.addValue(bitType(), opId);
      live.results.push_back(v);
      m.setName(v, resultNames.back());
      nameTable.emplace(resultNames.back(), v);
    }
  }

  return m;
}

}  // namespace spinor::dialect

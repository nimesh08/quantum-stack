// spinor/registry/lib/Yaml.cpp
//
// YAML subset parser implementation. Indentation-aware block parser
// for mappings and sequences, plus flow sequences. Tabs are
// rejected; only spaces.

#include "Yaml.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>

namespace spinor::registry::yaml {

namespace {

struct Line {
  std::size_t lineNo = 0;
  std::size_t indent = 0;       // count of leading spaces
  std::string text;             // trimmed-right; with leading indent stripped
  std::string raw;              // full text after newline, no trailing \r/\n
  bool blank = true;            // true if line is empty / comment-only
};

std::string rstrip(const std::string& s) {
  std::size_t e = s.size();
  while (e > 0 && (s[e - 1] == ' ' || s[e - 1] == '\r' || s[e - 1] == '\t')) {
    --e;
  }
  return s.substr(0, e);
}

// Tokenize raw text into Line records.
std::vector<Line> tokenize(const std::string& text) {
  std::vector<Line> lines;
  std::size_t lineNo = 0;
  std::size_t i = 0;
  while (i <= text.size()) {
    std::size_t start = i;
    while (i < text.size() && text[i] != '\n') ++i;
    std::string raw_full = text.substr(start, i - start);
    bool sawNewline = (i < text.size());
    if (sawNewline) ++i;  // consume '\n'
    ++lineNo;
    Line ln;
    ln.lineNo = lineNo;
    ln.raw = raw_full;
    // Reject tabs in indentation.
    std::size_t j = 0;
    while (j < raw_full.size() && raw_full[j] == ' ') ++j;
    ln.indent = j;
    if (j < raw_full.size() && raw_full[j] == '\t') {
      throw ParseError{"tab in indentation; only spaces allowed",
                       lineNo, j + 1};
    }
    std::string after_indent = rstrip(raw_full.substr(j));
    // Comment-only / blank?
    if (after_indent.empty() || after_indent.front() == '#') {
      ln.blank = true;
      ln.text = "";
      lines.push_back(ln);
      if (!sawNewline) break;
      continue;
    }
    ln.text = after_indent;
    ln.blank = false;
    lines.push_back(ln);
    if (!sawNewline) break;
  }
  return lines;
}

// Strip an inline `# ...` comment from a value; respects double quotes.
std::string stripInlineComment(std::string_view s) {
  std::string out;
  bool inDQ = false;
  for (std::size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == '"') inDQ = !inDQ;
    if (c == '#' && !inDQ) {
      // Require previous char to be whitespace (or start) for a real comment.
      if (out.empty() || out.back() == ' ' || out.back() == '\t') {
        // strip trailing space
        while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
          out.pop_back();
        }
        return out;
      }
    }
    out.push_back(c);
  }
  return out;
}

std::string ltrim(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
  return std::string(s.substr(i));
}
std::string trim(std::string_view s) {
  return rstrip(ltrim(s));
}

// Scalar parser: try int, double, bool, null, then string (possibly quoted).
Node parseScalar(std::string_view raw) {
  std::string s = trim(raw);
  if (s.empty() || s == "~" || s == "null") return Node::makeNull();
  if (s == "true" || s == "True")  return Node::makeBool(true);
  if (s == "false" || s == "False") return Node::makeBool(false);
  // quoted string
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return Node::makeString(s.substr(1, s.size() - 2));
  }
  if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
    return Node::makeString(s.substr(1, s.size() - 2));
  }
  // try integer
  {
    long long iv = 0;
    auto first = s.data();
    auto last = s.data() + s.size();
    if (!s.empty() && (s[0] == '-' || std::isdigit(static_cast<unsigned char>(s[0])))) {
      auto res = std::from_chars(first, last, iv);
      if (res.ec == std::errc{} && res.ptr == last) {
        return Node::makeInt(iv);
      }
    }
  }
  // try double
  try {
    std::size_t cnt = 0;
    double d = std::stod(s, &cnt);
    if (cnt == s.size()) return Node::makeDouble(d);
  } catch (...) {}
  return Node::makeString(s);
}

// Recursive descent block parser.
class BlockParser {
 public:
  explicit BlockParser(std::vector<Line> lines) : lines_(std::move(lines)) {}

  Node parse() {
    skipBlanks();
    if (atEnd()) return Node::makeNull();
    return parseBlock(currentIndent());
  }

 private:
  std::vector<Line> lines_;
  std::size_t pos_ = 0;

  bool atEnd() const { return pos_ >= lines_.size(); }
  const Line& cur() const { return lines_[pos_]; }
  std::size_t currentIndent() const { return lines_[pos_].indent; }

  void skipBlanks() {
    while (!atEnd() && cur().blank) ++pos_;
  }

  // Parse a block at the given indent. Returns either a map, an
  // array, or a scalar (if there is exactly one line with no
  // structural prefix).
  Node parseBlock(std::size_t indent) {
    skipBlanks();
    if (atEnd()) return Node::makeNull();
    if (cur().indent != indent) {
      // The block starts at a different indent than expected.
      throw ParseError{"unexpected indentation", cur().lineNo,
                       cur().indent + 1};
    }
    // Sequence?
    if (isSequenceItem(cur().text)) {
      return parseBlockSequence(indent);
    }
    // Otherwise mapping.
    return parseBlockMapping(indent);
  }

  static bool isSequenceItem(const std::string& text) {
    return !text.empty() && text[0] == '-' &&
           (text.size() == 1 || text[1] == ' ');
  }

  Node parseBlockSequence(std::size_t indent) {
    NodeArr items;
    while (!atEnd()) {
      skipBlanks();
      if (atEnd()) break;
      if (cur().indent != indent) break;
      if (!isSequenceItem(cur().text)) break;
      const Line& ln = cur();
      // Strip leading '- '.
      std::string after = ln.text.size() > 2 ? ln.text.substr(2) : "";
      after = stripInlineComment(after);
      if (after.empty()) {
        // The item is a nested block at indent+2 (or the next
        // non-blank line's indent).
        ++pos_;
        skipBlanks();
        if (atEnd()) {
          items.push_back(Node::makeNull());
          break;
        }
        items.push_back(parseBlock(cur().indent));
      } else {
        // Inline value: scalar or flow sequence. (Flow mapping not
        // supported.)
        ++pos_;
        items.push_back(parseInlineValue(after));
      }
    }
    return Node::makeArray(std::move(items));
  }

  Node parseBlockMapping(std::size_t indent) {
    NodeMap mapping;
    while (!atEnd()) {
      skipBlanks();
      if (atEnd()) break;
      if (cur().indent != indent) break;
      if (isSequenceItem(cur().text)) break;
      const std::string& text = cur().text;
      // Find the first ':' that is not inside quotes.
      std::size_t colon = std::string::npos;
      bool inDQ = false, inSQ = false;
      for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '"' && !inSQ) inDQ = !inDQ;
        else if (c == '\'' && !inDQ) inSQ = !inSQ;
        else if (c == ':' && !inDQ && !inSQ) {
          // Must be followed by whitespace or end.
          if (i + 1 == text.size() || text[i + 1] == ' ' ||
              text[i + 1] == '\t') {
            colon = i;
            break;
          }
        }
      }
      if (colon == std::string::npos) {
        throw ParseError{"expected 'key: value' or '- item' on this line",
                         cur().lineNo, cur().indent + 1};
      }
      std::string key = trim(std::string_view(text).substr(0, colon));
      std::string after =
          colon + 1 < text.size()
              ? trim(std::string_view(text).substr(colon + 1))
              : std::string{};
      after = stripInlineComment(after);
      // Strip surrounding quotes from key if present.
      if (key.size() >= 2 && key.front() == '"' && key.back() == '"') {
        key = key.substr(1, key.size() - 2);
      }
      ++pos_;
      if (after.empty()) {
        skipBlanks();
        if (atEnd() || cur().indent <= indent) {
          mapping[key] = Node::makeNull();
        } else {
          mapping[key] = parseBlock(cur().indent);
        }
      } else if (after == "|") {
        // Multi-line literal block scalar. Take the raw text from
        // each subsequent non-blank line whose indent > `indent`,
        // joining with newlines, with the body indent stripped.
        std::ostringstream os;
        std::size_t bodyIndent = 0;
        bool first = true;
        // Look at the first non-blank line to fix bodyIndent.
        std::size_t saved = pos_;
        while (!atEnd() && cur().blank) ++pos_;
        if (!atEnd() && cur().indent > indent) {
          bodyIndent = cur().indent;
        } else {
          pos_ = saved;
        }
        while (!atEnd() && (cur().blank || cur().indent >= bodyIndent)) {
          if (cur().blank) {
            // include blank lines as empty
            if (!first) os << "\n";
            first = false;
            ++pos_;
            continue;
          }
          if (cur().indent < bodyIndent) break;
          if (!first) os << "\n";
          first = false;
          // raw text minus bodyIndent leading spaces
          const std::string& r = cur().raw;
          if (r.size() >= bodyIndent) {
            os << r.substr(bodyIndent);
          } else {
            os << r;
          }
          ++pos_;
        }
        mapping[key] = Node::makeString(os.str());
      } else {
        // Inline value, possibly a multi-line flow sequence.
        std::string value = after;
        // If a flow sequence is opened but not closed on this
        // line, keep consuming subsequent lines until balanced.
        int depth = 0;
        bool inDQ = false, inSQ = false;
        for (char c : value) {
          if (c == '"' && !inSQ) inDQ = !inDQ;
          else if (c == '\'' && !inDQ) inSQ = !inSQ;
          else if (!inDQ && !inSQ) {
            if (c == '[') ++depth;
            else if (c == ']') --depth;
          }
        }
        while (depth > 0 && !atEnd()) {
          // Extend with the next line's text (after stripping
          // its own leading indent and inline comments).
          if (cur().blank) { ++pos_; continue; }
          std::string more = stripInlineComment(cur().text);
          for (char c : more) {
            if (c == '"' && !inSQ) inDQ = !inDQ;
            else if (c == '\'' && !inDQ) inSQ = !inSQ;
            else if (!inDQ && !inSQ) {
              if (c == '[') ++depth;
              else if (c == ']') --depth;
            }
          }
          value += " " + more;
          ++pos_;
        }
        mapping[key] = parseInlineValue(value);
      }
    }
    return Node::makeMap(std::move(mapping));
  }

  // Parse a value after `key: ` or after `- `. May be a scalar, a
  // flow sequence `[...]`, or empty.
  Node parseInlineValue(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return Node::makeNull();
    if (t.front() == '[') {
      return parseFlowSequence(t);
    }
    return parseScalar(t);
  }

  Node parseFlowSequence(const std::string& s) {
    // s starts with '[' and (we hope) ends with ']'.
    if (s.front() != '[' || s.back() != ']') {
      throw ParseError{"unterminated flow sequence: " + s, cur().lineNo, 0};
    }
    NodeArr items;
    std::string body = s.substr(1, s.size() - 2);
    // Split on commas at depth 0.
    int depth = 0;
    std::string buf;
    auto flush = [&]() {
      std::string t = trim(buf);
      if (!t.empty()) {
        if (!t.empty() && t.front() == '[') {
          items.push_back(parseFlowSequence(t));
        } else {
          items.push_back(parseScalar(t));
        }
      }
      buf.clear();
    };
    for (char c : body) {
      if (c == '[') ++depth;
      else if (c == ']') --depth;
      if (c == ',' && depth == 0) { flush(); continue; }
      buf.push_back(c);
    }
    flush();
    return Node::makeArray(std::move(items));
  }
};

}  // namespace

Node Parser::parse(const std::string& text) {
  std::vector<Line> lines = tokenize(text);
  BlockParser bp(std::move(lines));
  return bp.parse();
}

Node Parser::parse_file(const std::string& path) {
  std::ifstream f(path);
  if (!f) {
    throw ParseError{"cannot open " + path, 0, 0};
  }
  std::ostringstream os;
  os << f.rdbuf();
  return parse(os.str());
}

}  // namespace spinor::registry::yaml

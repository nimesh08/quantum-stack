// spinor/parser/qasm/lib/Qasm.cpp
//
// Tiny line-oriented OpenQASM 3 subset importer.

#include "spinor/parser/Qasm.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spinor::parser::qasm {

namespace {

using namespace spinor::dialect;

const std::set<std::string>& gateMnemonics() {
  static const std::set<std::string> s = {
      "h", "x", "y", "z", "s", "sdg", "t", "tdg",
      "rx", "ry", "rz", "cx", "cz", "swap",
      "ecr", "ms", "rzz", "sx", "sxdg",
      "gpi", "gpi2", "u1q",
  };
  return s;
}

struct Loc {
  std::string file;
  std::uint32_t line = 0;
};

std::string trim(std::string_view s) {
  std::size_t i = 0, j = s.size();
  while (i < j && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
  while (j > i && std::isspace(static_cast<unsigned char>(s[j - 1]))) --j;
  return std::string(s.substr(i, j - i));
}

// Strip line/block comments produced by `// ...` only.
std::string stripLineComment(std::string s) {
  bool inDQ = false;
  for (std::size_t i = 0; i + 1 < s.size(); ++i) {
    if (s[i] == '"') inDQ = !inDQ;
    if (!inDQ && s[i] == '/' && s[i + 1] == '/') {
      return s.substr(0, i);
    }
  }
  return s;
}

// Split a string on top-level commas (no commas inside parens or
// brackets).
std::vector<std::string> topSplit(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string buf;
  int depth = 0;
  for (char c : s) {
    if (c == '(' || c == '[') ++depth;
    else if (c == ')' || c == ']') --depth;
    if (c == sep && depth == 0) {
      out.push_back(trim(buf));
      buf.clear();
    } else buf.push_back(c);
  }
  out.push_back(trim(buf));
  return out;
}

double parseAngle(const std::string& s) {
  std::string t = s;
  // Handle pi forms: pi, -pi, pi/N, -pi/N, R*pi, -R*pi.
  bool neg = false;
  if (!t.empty() && t[0] == '-') { neg = true; t = trim(t.substr(1)); }
  if (t == "pi") return neg ? -M_PI : M_PI;
  if (t.rfind("pi/", 0) == 0) {
    int N = std::stoi(t.substr(3));
    double v = M_PI / N;
    return neg ? -v : v;
  }
  // Look for `* pi` or `*pi`.
  auto starP = t.find('*');
  if (starP != std::string::npos) {
    std::string lhs = trim(t.substr(0, starP));
    std::string rhs = trim(t.substr(starP + 1));
    if (rhs == "pi" || rhs == "PI") {
      double v = std::stod(lhs);
      return neg ? -v * M_PI : v * M_PI;
    }
  }
  double v = std::stod(t);
  return neg ? -v : v;
}

struct Reg {
  bool isQubit = true;
  int size = 0;
  std::vector<ValueId> liveQubits;
  std::vector<int> genQubits;
  std::vector<ValueId> latestBits;
  std::vector<int> genBits;
};

struct Driver {
  Module& m;
  Diagnostics& diag;
  std::string file;
  Builder b;
  std::unordered_map<std::string, Reg> regs;

  Driver(Module& mm, Diagnostics& d, std::string f)
      : m(mm), diag(d), file(std::move(f)), b(mm) {}

  void error(const std::string& msg, std::uint32_t line) {
    Location loc;
    loc.file = file;
    loc.line = line;
    diag.error(msg, loc);
  }

  // Returns true if the line was consumed.
  bool handleLine(std::string raw, std::uint32_t lineNo) {
    std::string s = trim(stripLineComment(std::move(raw)));
    if (s.empty()) return true;
    // Drop trailing semicolon.
    if (!s.empty() && s.back() == ';') s.pop_back();
    s = trim(s);
    if (s.empty()) return true;
    // Top-level keywords.
    if (s.rfind("OPENQASM", 0) == 0) return true;
    if (s.rfind("include", 0) == 0) return true;
    if (s.rfind("//", 0) == 0) return true;

    // qubit[N] name
    if (s.rfind("qubit", 0) == 0) {
      // Two forms: `qubit q` (single), `qubit[N] q`.
      std::string body = trim(s.substr(5));
      int size = 1;
      std::string name;
      if (!body.empty() && body[0] == '[') {
        auto rb = body.find(']');
        if (rb == std::string::npos) {
          error("malformed qubit declaration", lineNo);
          return false;
        }
        size = std::stoi(trim(body.substr(1, rb - 1)));
        name = trim(body.substr(rb + 1));
      } else {
        name = body;
      }
      if (name.empty()) {
        error("qubit declaration missing name", lineNo);
        return false;
      }
      Reg r;
      r.isQubit = true;
      r.size = size;
      for (int i = 0; i < size; ++i) {
        ValueId v = b.allocQubit();
        m.setName(v, name + std::to_string(i));
        r.liveQubits.push_back(v);
        r.genQubits.push_back(0);
      }
      regs.emplace(name, std::move(r));
      return true;
    }
    if (s.rfind("bit", 0) == 0) {
      std::string body = trim(s.substr(3));
      int size = 1;
      std::string name;
      if (!body.empty() && body[0] == '[') {
        auto rb = body.find(']');
        if (rb == std::string::npos) {
          error("malformed bit declaration", lineNo);
          return false;
        }
        size = std::stoi(trim(body.substr(1, rb - 1)));
        name = trim(body.substr(rb + 1));
      } else {
        name = body;
      }
      if (name.empty()) {
        error("bit declaration missing name", lineNo);
        return false;
      }
      Reg r;
      r.isQubit = false;
      r.size = size;
      for (int i = 0; i < size; ++i) {
        ValueId v = b.allocBit();
        m.setName(v, name + std::to_string(i));
        r.latestBits.push_back(v);
        r.genBits.push_back(0);
      }
      regs.emplace(name, std::move(r));
      return true;
    }
    if (s.rfind("reset", 0) == 0) {
      std::string body = trim(s.substr(5));
      auto pr = parseOperand(body, lineNo);
      if (!pr) return false;
      auto* r = qubitReg(pr->reg, lineNo);
      if (!r) return false;
      ValueId nv = b.reset(r->liveQubits[pr->idx]);
      ++r->genQubits[pr->idx];
      m.setName(nv, pr->reg + std::to_string(pr->idx) + "_" +
                    std::to_string(r->genQubits[pr->idx]));
      r->liveQubits[pr->idx] = nv;
      return true;
    }
    if (s.rfind("barrier", 0) == 0) {
      std::string body = trim(s.substr(7));
      std::vector<ValueId> qs;
      if (!body.empty()) {
        for (const auto& part : topSplit(body, ',')) {
          auto pr = parseOperand(part, lineNo);
          if (!pr) return false;
          auto* r = qubitReg(pr->reg, lineNo);
          if (!r) return false;
          qs.push_back(r->liveQubits[pr->idx]);
        }
      }
      b.barrier(qs);
      return true;
    }
    // Measure: `c[i] = measure q[i];`
    auto eq = s.find('=');
    if (eq != std::string::npos) {
      std::string lhs = trim(s.substr(0, eq));
      std::string rhs = trim(s.substr(eq + 1));
      if (rhs.rfind("measure", 0) == 0) {
        std::string qstr = trim(rhs.substr(7));
        auto qp = parseOperand(qstr, lineNo);
        auto bp = parseOperand(lhs, lineNo);
        if (!qp || !bp) return false;
        auto* qr = qubitReg(qp->reg, lineNo);
        auto* br = bitReg(bp->reg, lineNo);
        if (!qr || !br) return false;
        ValueId bit = b.measure(qr->liveQubits[qp->idx]);
        ++br->genBits[bp->idx];
        m.setName(bit, bp->reg + std::to_string(bp->idx) + "_" +
                       std::to_string(br->genBits[bp->idx]));
        br->latestBits[bp->idx] = bit;
        return true;
      }
      error("unsupported assignment in OpenQASM subset (only `c = measure q` is allowed)",
            lineNo);
      return false;
    }
    // Otherwise: gate statement: `name [(angles)] op [, op]`.
    return handleGate(s, lineNo);
  }

  struct ParsedOperand {
    std::string reg;
    int idx = 0;
  };
  std::optional<ParsedOperand> parseOperand(const std::string& s,
                                            std::uint32_t lineNo) {
    auto lb = s.find('[');
    auto rb = s.find(']');
    ParsedOperand p;
    if (lb == std::string::npos) {
      // Whole-register operand not supported in subset (Phonon
      // territory).
      error("operand must be `name[index]` (got '" + s + "')", lineNo);
      return std::nullopt;
    }
    if (rb == std::string::npos || rb < lb) {
      error("malformed operand: " + s, lineNo);
      return std::nullopt;
    }
    p.reg = trim(s.substr(0, lb));
    p.idx = std::stoi(trim(s.substr(lb + 1, rb - lb - 1)));
    return p;
  }

  Reg* qubitReg(const std::string& name, std::uint32_t lineNo) {
    auto it = regs.find(name);
    if (it == regs.end() || !it->second.isQubit) {
      error("'" + name + "' is not a declared qubit register", lineNo);
      return nullptr;
    }
    return &it->second;
  }
  Reg* bitReg(const std::string& name, std::uint32_t lineNo) {
    auto it = regs.find(name);
    if (it == regs.end() || it->second.isQubit) {
      error("'" + name + "' is not a declared bit register", lineNo);
      return nullptr;
    }
    return &it->second;
  }

  bool handleGate(const std::string& s, std::uint32_t lineNo) {
    // Split at the first space or '(' to find gate name.
    std::size_t i = 0;
    while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) ||
                            s[i] == '_')) ++i;
    std::string name = s.substr(0, i);
    if (gateMnemonics().count(name) == 0) {
      error("unsupported construct '" + name +
            "': Phonon (Phase B) is for control flow / gate definitions",
            lineNo);
      return false;
    }
    std::string rest = trim(s.substr(i));
    std::vector<double> angles;
    if (!rest.empty() && rest[0] == '(') {
      auto rp = rest.find(')');
      if (rp == std::string::npos) {
        error("missing ')' in gate parameters", lineNo);
        return false;
      }
      std::string params = rest.substr(1, rp - 1);
      for (const auto& a : topSplit(params, ',')) {
        if (a.empty()) continue;
        try {
          angles.push_back(parseAngle(a));
        } catch (...) {
          error("malformed angle '" + a + "'", lineNo);
          return false;
        }
      }
      rest = trim(rest.substr(rp + 1));
    }
    std::vector<ParsedOperand> ops;
    for (const auto& part : topSplit(rest, ',')) {
      auto p = parseOperand(part, lineNo);
      if (!p) return false;
      ops.push_back(*p);
    }
    // Resolve to ValueIds.
    auto sq1 = [&](auto fn) {
      if (ops.size() != 1) {
        error("gate '" + name + "' expects 1 qubit operand", lineNo);
        return false;
      }
      auto* r = qubitReg(ops[0].reg, lineNo);
      if (!r) return false;
      ValueId nv = (b.*fn)(r->liveQubits[ops[0].idx], Location{});
      ++r->genQubits[ops[0].idx];
      m.setName(nv, ops[0].reg + std::to_string(ops[0].idx) + "_" +
                    std::to_string(r->genQubits[ops[0].idx]));
      r->liveQubits[ops[0].idx] = nv;
      return true;
    };
    auto sq1a = [&](auto fn, double angle) {
      if (ops.size() != 1) {
        error("gate '" + name + "' expects 1 qubit operand", lineNo);
        return false;
      }
      auto* r = qubitReg(ops[0].reg, lineNo);
      if (!r) return false;
      ValueId nv = (b.*fn)(angle, r->liveQubits[ops[0].idx], Location{});
      ++r->genQubits[ops[0].idx];
      m.setName(nv, ops[0].reg + std::to_string(ops[0].idx) + "_" +
                    std::to_string(r->genQubits[ops[0].idx]));
      r->liveQubits[ops[0].idx] = nv;
      return true;
    };
    auto tq = [&](auto fn) {
      if (ops.size() != 2) {
        error("gate '" + name + "' expects 2 qubit operands", lineNo);
        return false;
      }
      auto* ra = qubitReg(ops[0].reg, lineNo);
      auto* rc = qubitReg(ops[1].reg, lineNo);
      if (!ra || !rc) return false;
      auto pair = (b.*fn)(ra->liveQubits[ops[0].idx],
                          rc->liveQubits[ops[1].idx], Location{});
      ++ra->genQubits[ops[0].idx];
      ++rc->genQubits[ops[1].idx];
      m.setName(pair.first, ops[0].reg + std::to_string(ops[0].idx) +
                            "_" + std::to_string(ra->genQubits[ops[0].idx]));
      m.setName(pair.second, ops[1].reg + std::to_string(ops[1].idx) +
                             "_" + std::to_string(rc->genQubits[ops[1].idx]));
      ra->liveQubits[ops[0].idx] = pair.first;
      rc->liveQubits[ops[1].idx] = pair.second;
      return true;
    };

    if (name == "h")    return sq1(&Builder::h);
    if (name == "x")    return sq1(&Builder::x);
    if (name == "y")    return sq1(&Builder::y);
    if (name == "z")    return sq1(&Builder::z);
    if (name == "s")    return sq1(&Builder::s);
    if (name == "sdg")  return sq1(&Builder::sdg);
    if (name == "t")    return sq1(&Builder::t);
    if (name == "tdg")  return sq1(&Builder::tdg);
    if (name == "sx")   return sq1(&Builder::sx);
    if (name == "sxdg") return sq1(&Builder::sxdg);
    if (name == "rx" || name == "ry" || name == "rz" ||
        name == "gpi" || name == "gpi2") {
      if (angles.size() != 1) {
        error("gate '" + name + "' expects 1 angle", lineNo);
        return false;
      }
      double a = angles[0];
      if (name == "rx")   return sq1a(&Builder::rx, a);
      if (name == "ry")   return sq1a(&Builder::ry, a);
      if (name == "rz")   return sq1a(&Builder::rz, a);
      if (name == "gpi")  return sq1a(&Builder::gpi, a);
      if (name == "gpi2") return sq1a(&Builder::gpi2, a);
    }
    if (name == "cx")   return tq(&Builder::cx);
    if (name == "cz")   return tq(&Builder::cz);
    if (name == "swap") return tq(&Builder::swap);
    if (name == "ecr")  return tq(&Builder::ecr);
    if (name == "ms")   return tq(&Builder::ms);
    error("unsupported gate '" + name + "' in OpenQASM subset", lineNo);
    return false;
  }
};

}  // namespace

ImportResult import(std::string_view text, std::string_view filename,
                    std::string target) {
  ImportResult r;
  r.module = Module();
  r.module->targetAttr = std::move(target);
  Driver drv(*r.module, r.diag, std::string(filename));
  std::string buf;
  std::uint32_t lineNo = 0;
  for (std::size_t i = 0; i <= text.size(); ++i) {
    if (i == text.size() || text[i] == '\n') {
      ++lineNo;
      // OpenQASM 3 statements terminate with `;`. We accept either
      // statement-per-line OR statements split by `;` within a
      // logical line. So split `buf` on top-level `;`.
      for (const auto& part : topSplit(buf, ';')) {
        if (!part.empty()) {
          if (!drv.handleLine(part, lineNo)) {
            r.module.reset();
            return r;
          }
        }
      }
      buf.clear();
    } else {
      buf.push_back(text[i]);
    }
  }
  return r;
}

}  // namespace spinor::parser::qasm

// spinor/registry/lib/Yaml.h
//
// Tiny YAML subset parser for the Spinor registry. Supports:
//   - block mappings (`key: value`)
//   - block sequences (`- item`)
//   - flow sequences (`[a, b, c]`)
//   - scalars (strings, numbers, booleans, null)
//   - comments (`# ...`)
//   - multi-line `|` strings
//
// Does NOT support: anchors, aliases, tags, complex flow
// mappings, merge keys.

#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace spinor::registry::yaml {

class Node;
using NodeMap = std::map<std::string, Node>;
using NodeArr = std::vector<Node>;

class Node {
 public:
  enum class Kind { Null, Bool, Int, Double, String, Array, Map };

  Node() = default;
  static Node makeNull() { return Node(); }
  static Node makeBool(bool b) { Node n; n.kind_ = Kind::Bool; n.b_ = b; return n; }
  static Node makeInt(long long i) { Node n; n.kind_ = Kind::Int; n.i_ = i; return n; }
  static Node makeDouble(double d) { Node n; n.kind_ = Kind::Double; n.d_ = d; return n; }
  static Node makeString(std::string s) {
    Node n; n.kind_ = Kind::String; n.s_ = std::move(s); return n;
  }
  static Node makeArray(NodeArr a) {
    Node n; n.kind_ = Kind::Array; n.a_ = std::make_shared<NodeArr>(std::move(a)); return n;
  }
  static Node makeMap(NodeMap m) {
    Node n; n.kind_ = Kind::Map; n.m_ = std::make_shared<NodeMap>(std::move(m)); return n;
  }

  Kind kind() const { return kind_; }
  bool isNull() const { return kind_ == Kind::Null; }
  bool isBool() const { return kind_ == Kind::Bool; }
  bool isInt() const { return kind_ == Kind::Int; }
  bool isDouble() const { return kind_ == Kind::Double; }
  bool isNumber() const { return kind_ == Kind::Int || kind_ == Kind::Double; }
  bool isString() const { return kind_ == Kind::String; }
  bool isArray() const { return kind_ == Kind::Array; }
  bool isMap() const { return kind_ == Kind::Map; }

  bool asBool() const { return b_; }
  long long asInt() const {
    if (kind_ == Kind::Int) return i_;
    if (kind_ == Kind::Double) return static_cast<long long>(d_);
    throw std::runtime_error("yaml: not an integer");
  }
  double asDouble() const {
    if (kind_ == Kind::Double) return d_;
    if (kind_ == Kind::Int) return static_cast<double>(i_);
    throw std::runtime_error("yaml: not a number");
  }
  const std::string& asString() const {
    if (kind_ != Kind::String)
      throw std::runtime_error("yaml: not a string");
    return s_;
  }
  const NodeArr& asArray() const {
    if (kind_ != Kind::Array)
      throw std::runtime_error("yaml: not an array");
    return *a_;
  }
  const NodeMap& asMap() const {
    if (kind_ != Kind::Map)
      throw std::runtime_error("yaml: not a map");
    return *m_;
  }

  // Convenience.
  bool has(const std::string& key) const {
    return isMap() && m_->find(key) != m_->end();
  }
  const Node& at(const std::string& key) const {
    if (!isMap()) throw std::runtime_error("yaml: not a map");
    auto it = m_->find(key);
    if (it == m_->end())
      throw std::runtime_error("yaml: missing key '" + key + "'");
    return it->second;
  }

 private:
  Kind kind_ = Kind::Null;
  bool b_ = false;
  long long i_ = 0;
  double d_ = 0.0;
  std::string s_;
  std::shared_ptr<NodeArr> a_;
  std::shared_ptr<NodeMap> m_;
};

struct ParseError {
  std::string message;
  std::size_t line = 0;
  std::size_t column = 0;
};

class Parser {
 public:
  static Node parse(const std::string& text);
  static Node parse_file(const std::string& path);
};

}  // namespace spinor::registry::yaml

// spinor/dialect/include/spinor/dialect/Spinor.h
//
// Public API for the Spinor IR (Phase A milestone M1).
//
// Two backends share this header: the in-tree backend (always
// available) and the MLIR-backed backend (when LLVM/MLIR is
// present). Callers see one API; the implementation switches
// behind it.
//
// This header MUST NOT include any LLVM/MLIR header — doing so
// would push the LLVM dependency onto every consumer. MLIR
// integration lives in lib/mlir/SpinorMLIR.cpp behind
// QSTACK_HAVE_LLVM.
//
// Spec: spinor_engineering_deep_dive.docx Part 1 §6.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace spinor::dialect {

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

enum class TypeKind : std::uint8_t {
  Qubit,
  Bit,
};

struct Type {
  TypeKind kind;
  friend constexpr bool operator==(Type, Type) = default;
};

inline constexpr Type qubitType() { return {TypeKind::Qubit}; }
inline constexpr Type bitType() { return {TypeKind::Bit}; }

std::string_view typeName(Type t);

// ---------------------------------------------------------------------------
// Op kinds
// ---------------------------------------------------------------------------

enum class OpKind : std::uint16_t {
  // memory
  AllocQubit,
  AllocBit,
  // single-qubit standard gates
  H,
  X,
  Y,
  Z,
  S,
  Sdg,
  T,
  Tdg,
  // single-qubit rotations (one F64 attribute "angle")
  Rx,
  Ry,
  Rz,
  // two-qubit standard gates
  Cx,
  Cz,
  Swap,
  // native gates (M3 W6 forbids these under target generic)
  Ecr,
  Ms,    // 2q
  Rzz,   // 2q with angle
  Sx,    // 1q
  Sxdg,  // 1q
  Gpi,   // 1q with angle
  Gpi2,  // 1q with angle
  U1q,   // 1q with two angles (theta, phi)
  // measurement / reset / barrier
  Measure,
  Reset,
  Barrier,
};

std::string_view opMnemonic(OpKind k);
bool isStandardGate(OpKind k);
bool isNativeGate(OpKind k);
// `arity` = number of qubit operands; -1 means variable (Barrier).
int qubitArity(OpKind k);

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

// Strong typedef. Monotonic; never reused within a Module.
struct ValueId {
  std::uint32_t v = 0;
  friend constexpr bool operator==(ValueId, ValueId) = default;
  friend constexpr bool operator<(ValueId a, ValueId b) {
    return a.v < b.v;
  }
};

inline constexpr ValueId kInvalidValue = ValueId{0xFFFFFFFFu};

// ---------------------------------------------------------------------------
// Source locations
// ---------------------------------------------------------------------------

struct Location {
  std::string file;  // empty => unknown
  std::uint32_t line = 0;
  std::uint32_t column = 0;
};

// ---------------------------------------------------------------------------
// Attributes
// ---------------------------------------------------------------------------

// Phase A attribute system is intentionally minimal: a key plus a
// double or string value. Rotation gates use {"angle", double}.
// U1q uses {"theta", double} and {"phi", double}.
struct Attribute {
  std::string name;
  std::variant<double, std::string> value;
};

// Convenience constructors.
inline Attribute angleAttr(double a) {
  return Attribute{"angle", a};
}
inline Attribute namedDouble(std::string name, double v) {
  return Attribute{std::move(name), v};
}
inline Attribute namedString(std::string name, std::string v) {
  return Attribute{std::move(name), std::move(v)};
}

// ---------------------------------------------------------------------------
// Op
// ---------------------------------------------------------------------------

struct OpId {
  std::uint32_t v = 0;
  friend constexpr bool operator==(OpId, OpId) = default;
  friend constexpr bool operator<(OpId a, OpId b) { return a.v < b.v; }
};

inline constexpr OpId kInvalidOp = OpId{0xFFFFFFFFu};

struct Op {
  OpKind kind;
  std::vector<ValueId> operands;
  std::vector<ValueId> results;
  std::vector<Attribute> attributes;
  Location loc;
};

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

enum class DiagSeverity { Error, Warning, Note };

struct Diagnostic {
  DiagSeverity severity = DiagSeverity::Error;
  std::string message;
  Location loc;     // empty if not associated with source
  OpId opId = kInvalidOp;
};

class Diagnostics {
 public:
  void error(std::string msg, Location loc = {}, OpId op = kInvalidOp) {
    items_.push_back({DiagSeverity::Error, std::move(msg),
                      std::move(loc), op});
  }
  void warn(std::string msg, Location loc = {}, OpId op = kInvalidOp) {
    items_.push_back({DiagSeverity::Warning, std::move(msg),
                      std::move(loc), op});
  }
  std::span<const Diagnostic> items() const { return items_; }
  bool empty() const { return items_.empty(); }
  bool hasErrors() const;

 private:
  std::vector<Diagnostic> items_;
};

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

class Module {
 public:
  // attributes
  std::string targetAttr;  // "generic" or a device id
  std::string name = "main";

  // value table
  ValueId addValue(Type t, OpId producer);
  Type typeOf(ValueId v) const;
  OpId producerOf(ValueId v) const;
  std::size_t numValues() const { return values_.size(); }

  // op table
  OpId addOp(Op op);
  const Op& op(OpId id) const;
  Op& opMut(OpId id);
  std::span<const Op> ops() const { return ops_; }
  std::size_t numOps() const { return ops_.size(); }

  // human-readable identifier for a value (used by printer)
  std::string nameOf(ValueId v) const;
  void setName(ValueId v, std::string n);  // for tests

 private:
  struct ValueInfo {
    Type type;
    OpId producer;
    std::string name;  // optional; printer falls back to %v<n>
  };
  std::vector<ValueInfo> values_;
  std::vector<Op> ops_;
};

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

class Builder {
 public:
  explicit Builder(Module& m) : m_(m) {}
  Module& module() { return m_; }

  // declarations
  ValueId allocQubit(Location loc = {});
  ValueId allocBit(Location loc = {});

  // single-qubit standard gates
  ValueId h(ValueId q, Location loc = {});
  ValueId x(ValueId q, Location loc = {});
  ValueId y(ValueId q, Location loc = {});
  ValueId z(ValueId q, Location loc = {});
  ValueId s(ValueId q, Location loc = {});
  ValueId sdg(ValueId q, Location loc = {});
  ValueId t(ValueId q, Location loc = {});
  ValueId tdg(ValueId q, Location loc = {});
  ValueId rx(double angle, ValueId q, Location loc = {});
  ValueId ry(double angle, ValueId q, Location loc = {});
  ValueId rz(double angle, ValueId q, Location loc = {});

  // two-qubit standard gates
  std::pair<ValueId, ValueId> cx(ValueId ctrl, ValueId tgt,
                                 Location loc = {});
  std::pair<ValueId, ValueId> cz(ValueId ctrl, ValueId tgt,
                                 Location loc = {});
  std::pair<ValueId, ValueId> swap(ValueId a, ValueId b,
                                   Location loc = {});

  // native single-qubit
  ValueId sx(ValueId q, Location loc = {});
  ValueId sxdg(ValueId q, Location loc = {});
  ValueId gpi(double angle, ValueId q, Location loc = {});
  ValueId gpi2(double angle, ValueId q, Location loc = {});
  ValueId u1q(double theta, double phi, ValueId q, Location loc = {});

  // native two-qubit
  std::pair<ValueId, ValueId> ecr(ValueId a, ValueId b,
                                  Location loc = {});
  std::pair<ValueId, ValueId> ms(ValueId a, ValueId b, Location loc = {});
  std::pair<ValueId, ValueId> rzz(double angle, ValueId a, ValueId b,
                                  Location loc = {});

  // measure / reset / barrier
  ValueId measure(ValueId q, Location loc = {});  // returns Bit
  ValueId reset(ValueId q, Location loc = {});    // returns fresh Qubit
  void barrier(std::span<const ValueId> qs, Location loc = {});

 private:
  Module& m_;
};

// ---------------------------------------------------------------------------
// Print / Parse / Verify
// ---------------------------------------------------------------------------

// Textual format. Round-trippable; deliberately MLIR-like so the
// MLIR-backed backend can hand off to MLIR's parser/printer.
std::string print(const Module& m);

// Parse a previously printed module. On error, returns nullopt and
// fills `diag`.
std::optional<Module> parse(std::string_view text, Diagnostics& diag);

// Structural verification (different from M3 W1-W6 semantic check):
// every operand has the right type, results are unique, attributes
// match the op's expected set, target is set.
void verify(const Module& m, Diagnostics& diag);

}  // namespace spinor::dialect

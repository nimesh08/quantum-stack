// phonon/dialect/include/phonon/dialect/Phonon.h
//
// Public API for the Phonon IR (Phase B milestone M1).
//
// Two backends share this header: the in-tree backend (always
// available) and the MLIR-backed backend (when LLVM/MLIR is
// present). Callers see one API; the implementation switches
// behind it.
//
// This header MUST NOT include any LLVM/MLIR header. MLIR
// integration lives in lib/mlir/PhononMLIR.cpp behind
// QSTACK_HAVE_LLVM.
//
// The Phonon dialect is a STRICT EXTENSION of the Spinor dialect:
// it re-exports every Spinor type / op / value-shape and adds
// classical-value types, control-flow ops, and function ops on top.
//
// Spec: phonon_engineering_deep_dive.docx Part 1 §§3.3, 4
// and docs/build/phaseB/M1_dialect.md.

#pragma once

#include "spinor/dialect/Spinor.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace phonon::dialect {

// --- Spinor re-exports -----------------------------------------------------

using spinor::dialect::Attribute;
using spinor::dialect::Diagnostics;
using spinor::dialect::Diagnostic;
using spinor::dialect::DiagSeverity;
using spinor::dialect::Location;
using spinor::dialect::ValueId;
using spinor::dialect::OpId;
using spinor::dialect::kInvalidValue;
using spinor::dialect::kInvalidOp;

// --- Types -----------------------------------------------------------------

// Five type kinds. Two are inherited from Spinor (Qubit, Bit) and
// share their identity for operand compatibility; three are new.
enum class TypeKind : std::uint8_t {
  Qubit,
  Bit,
  Int,
  Angle,
  Func,
};

struct Type {
  TypeKind kind;
  friend constexpr bool operator==(Type, Type) = default;
};

inline constexpr Type qubitType() { return {TypeKind::Qubit}; }
inline constexpr Type bitType()   { return {TypeKind::Bit}; }
inline constexpr Type intType()   { return {TypeKind::Int}; }
inline constexpr Type angleType() { return {TypeKind::Angle}; }
inline constexpr Type funcType()  { return {TypeKind::Func}; }

std::string_view typeName(Type t);

// Bridge: given a Spinor Type, get the equivalent Phonon Type.
inline Type fromSpinor(spinor::dialect::Type t) {
  using SK = spinor::dialect::TypeKind;
  return {t.kind == SK::Qubit ? TypeKind::Qubit : TypeKind::Bit};
}

inline spinor::dialect::Type toSpinor(Type t) {
  using SK = spinor::dialect::TypeKind;
  return {t.kind == TypeKind::Qubit ? SK::Qubit : SK::Bit};
}

// --- Op kinds --------------------------------------------------------------

// Phonon op kinds. The first batch mirrors every Spinor::OpKind; the
// second batch is the Phonon-only additions. The mnemonics
// distinguish: the first batch prints "spinor.<name>", the second
// prints "phonon.<name>".
enum class OpKind : std::uint16_t {
  // memory (spinor.*)
  AllocQubit,
  AllocBit,
  // single-qubit standard gates (spinor.*)
  H, X, Y, Z, S, Sdg, T, Tdg,
  // single-qubit rotations (spinor.*)
  Rx, Ry, Rz,
  // two-qubit standard gates (spinor.*)
  Cx, Cz, Swap,
  // native gates (spinor.*)
  Ecr, Ms, Rzz,
  Sx, Sxdg,
  Gpi, Gpi2, U1q,
  // measurement / reset / barrier (spinor.*)
  Measure, Reset, Barrier,
  // -- Phonon additions ---------------------------------------------------
  ConstInt,    // attr "value" : double (stored as int64 cast)
  ConstAngle,  // attr "value" : double (radians)
  BinOp,       // attr "op" : "+|-|*|/"; classical 2-input expression
  Cmp,         // attr "op" : "==|!=|<|>"; classical 2-input → 1 bit
  If,          // operand: predicate; attrs "then_count", "else_count"
  EndIf,
  For,         // operands: lo, hi (int values); attr "var"
  EndFor,
  While,       // operand: predicate-symbol or precomputed bound
  EndWhile,
  Def,         // attrs: "name", "param_count", "body_count", per-param types
  EndDef,
  Call,        // attr "name"; operands = args; results inferred from def
  Return,      // operands: returned values
  Assign,      // operand: source value; attr "name" (symbol)
};

std::string_view opMnemonic(OpKind k);
bool isSpinorKind(OpKind k);     // true for the first batch
bool isPhononKind(OpKind k);     // true for the second batch
bool isStandardGate(OpKind k);   // for Spinor-kind ops only
bool isNativeGate(OpKind k);     // for Spinor-kind ops only
int  qubitArity(OpKind k);       // qubit operand count; 0 for non-quantum

// Bridge: convert between Spinor and Phonon op kind enumerations
// (for the spinor.* ops, which share semantics).
spinor::dialect::OpKind toSpinorKind(OpKind k);
OpKind fromSpinorKind(spinor::dialect::OpKind k);

// --- Op --------------------------------------------------------------------

struct Op {
  OpKind kind;
  std::vector<ValueId> operands;
  std::vector<ValueId> results;
  std::vector<Attribute> attributes;
  Location loc;
};

// --- Module ----------------------------------------------------------------

class Module {
 public:
  std::string targetAttr;
  std::string name = "main";

  ValueId addValue(Type t, OpId producer);
  Type typeOf(ValueId v) const;
  OpId producerOf(ValueId v) const;
  std::size_t numValues() const { return values_.size(); }

  OpId addOp(Op op);
  const Op& op(OpId id) const;
  Op& opMut(OpId id);
  std::span<const Op> ops() const { return ops_; }
  std::size_t numOps() const { return ops_.size(); }

  std::string nameOf(ValueId v) const;
  void setName(ValueId v, std::string n);

 private:
  struct ValueInfo {
    Type type;
    OpId producer;
    std::string name;
  };
  std::vector<ValueInfo> values_;
  std::vector<Op> ops_;
};

// --- Builder ---------------------------------------------------------------

class Builder {
 public:
  explicit Builder(Module& m) : m_(m) {}
  Module& module() { return m_; }

  // Spinor-kind ops (every gate the Spinor dialect has).
  ValueId allocQubit(Location loc = {});
  ValueId allocBit(Location loc = {});
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
  std::pair<ValueId, ValueId> cx(ValueId a, ValueId b, Location loc = {});
  std::pair<ValueId, ValueId> cz(ValueId a, ValueId b, Location loc = {});
  std::pair<ValueId, ValueId> swap(ValueId a, ValueId b, Location loc = {});
  ValueId sx(ValueId q, Location loc = {});
  ValueId sxdg(ValueId q, Location loc = {});
  ValueId gpi(double angle, ValueId q, Location loc = {});
  ValueId gpi2(double angle, ValueId q, Location loc = {});
  ValueId u1q(double theta, double phi, ValueId q, Location loc = {});
  std::pair<ValueId, ValueId> ecr(ValueId a, ValueId b, Location loc = {});
  std::pair<ValueId, ValueId> ms(ValueId a, ValueId b, Location loc = {});
  std::pair<ValueId, ValueId> rzz(double angle, ValueId a, ValueId b,
                                  Location loc = {});
  ValueId measure(ValueId q, Location loc = {});
  ValueId reset(ValueId q, Location loc = {});
  void barrier(std::span<const ValueId> qs, Location loc = {});

  // Phonon classical ops.
  ValueId constInt(int64_t v, Location loc = {});
  ValueId constAngle(double rad, Location loc = {});
  ValueId binOp(std::string op, ValueId a, ValueId b, Location loc = {});
  ValueId cmp(std::string op, ValueId a, ValueId b, Location loc = {});

  // Phonon control-flow ops. Marker pairs MUST be balanced. The
  // begin* methods return the OpId of the open marker so callers
  // can pass it to the matching end*.
  OpId beginIf(ValueId predicate, Location loc = {});
  void elseIf(OpId begin);   // optional: tag the start of else
  void endIf(OpId begin, Location loc = {});
  OpId beginFor(std::string var, ValueId lo, ValueId hi,
                Location loc = {});
  void endFor(OpId begin, Location loc = {});
  OpId beginWhile(ValueId predicate, Location loc = {});
  void endWhile(OpId begin, Location loc = {});

  // Phonon function ops. Parameters are typed; the returned OpId
  // is the def marker. The body is whatever ops are added before
  // endDef. Use returnOp() to express the function's results.
  struct Param { Type type; std::string name; };
  OpId beginDef(std::string name, std::span<const Param> params,
                Location loc = {});
  void endDef(OpId begin, Location loc = {});
  void returnOp(std::span<const ValueId> values, Location loc = {});
  std::vector<ValueId> call(std::string name,
                            std::span<const ValueId> args,
                            std::span<const Type> resultTypes,
                            Location loc = {});

  // Within a `phonon.def`, parameter values are bound to the
  // following ValueIds: query them with paramValue(defId, idx).
  ValueId paramValue(OpId defId, std::size_t paramIdx) const;

  void assign(std::string name, ValueId v, Location loc = {});

 private:
  Module& m_;
};

// --- Print / Parse / Verify ------------------------------------------------

std::string print(const Module& m);
std::optional<Module> parse(std::string_view text, Diagnostics& diag);
void verify(const Module& m, Diagnostics& diag);

}  // namespace phonon::dialect

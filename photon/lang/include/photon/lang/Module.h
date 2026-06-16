// photon/lang/include/photon/lang/Module.h
//
// Photon AST data structures.
//
// Photon is the user-facing OO surface. The AST captured here is
// the result of parsing a `.pho` source file; lowering (Lower.h)
// turns it into a `phonon::dialect::Module`.

#pragma once

#include "spinor/dialect/Spinor.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace photon::lang {

using spinor::dialect::Diagnostic;
using spinor::dialect::DiagSeverity;
using spinor::dialect::Diagnostics;
using spinor::dialect::Location;

enum class TypeKind : std::uint8_t {
  QReg,    // qreg_size carries the register width.
  Int,
  Angle,
  Bit,
  Void,
  Oracle,  // forward-declared callable for photon.lib (M2).
};

struct Type {
  TypeKind kind = TypeKind::Void;
  std::uint32_t qreg_size = 0;  // Only used when kind == QReg.

  friend bool operator==(const Type&, const Type&) = default;
};

inline constexpr Type voidType()  { return {TypeKind::Void,   0}; }
inline constexpr Type intType()   { return {TypeKind::Int,    0}; }
inline constexpr Type angleType() { return {TypeKind::Angle,  0}; }
inline constexpr Type bitType()   { return {TypeKind::Bit,    0}; }
inline constexpr Type oracleType(){ return {TypeKind::Oracle, 0}; }
inline constexpr Type qregType(std::uint32_t n) {
  return {TypeKind::QReg, n};
}

// --- Expressions -----------------------------------------------------------

struct Expr;
using ExprPtr = std::shared_ptr<const Expr>;

enum class ExprKind : std::uint8_t {
  IntLit,
  RealLit,
  Ident,        // bare name; resolves to a parameter, local var, or QReg.
  BinOp,        // payload: op string, lhs, rhs.
  UnaryMinus,   // payload: rhs.
  Range,        // payload: lhs..rhs (used in for-loops).
  Call,         // payload: callee identifier or member, args.
  Member,       // payload: receiver, member name (e.g. q.h).
  Pi,           // sentinel; lowers to the literal pi.
  CmpEq,        // == ; needed for if-on-bit.
  CmpNeq,       // !=
  CmpLt, CmpGt, CmpLe, CmpGe,
};

struct Expr {
  ExprKind kind;
  std::string text;            // identifier name / op / member name.
  std::int64_t int_value = 0;
  double real_value = 0.0;
  std::vector<ExprPtr> children;
  Location loc;
};

// --- Statements ------------------------------------------------------------

struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;

enum class StmtKind : std::uint8_t {
  VarDecl,      // type ident = expr  OR  type ident(args)
  Assign,       // ident = expr
  GateCall,     // q.h(args)
  LibCall,      // lib.something(args)  (or any non-gate method)
  MeasureAll,   // q.measure() returns bit register
  MeasureInt,   // q.measure_int() returns int
  ForLoop,
  IfStmt,
  ReturnStmt,
  ExprStmt,
};

struct Stmt {
  StmtKind kind;
  Location loc;

  // VarDecl: type, name, optional init expr or constructor args.
  Type decl_type;
  std::string name;
  std::vector<ExprPtr> args;     // also used for GateCall/LibCall args.
  ExprPtr init;                  // VarDecl init, Assign rhs, Return value, ExprStmt.

  // GateCall / LibCall / MeasureAll / MeasureInt: receiver name + method.
  std::string receiver;
  std::string method;

  // ForLoop: var name, lo, hi, body.
  std::string for_var;
  ExprPtr for_lo;
  ExprPtr for_hi;
  std::vector<StmtPtr> body;

  // IfStmt: predicate, then-body, else-body.
  ExprPtr predicate;
  std::vector<StmtPtr> then_body;
  std::vector<StmtPtr> else_body;
};

// --- Function & Module -----------------------------------------------------

struct Param {
  Type type;
  std::string name;
  Location loc;
};

struct Function {
  std::string name;
  bool is_kernel = false;        // marked with `kernel`.
  std::vector<Param> params;
  Type return_type = voidType();
  std::vector<StmtPtr> body;
  Location loc;
};

class Module {
 public:
  std::string target = "generic";
  std::vector<Function> functions;

  const Function* findFunction(std::string_view name) const;
};

// Builder helpers (used by tests).
ExprPtr mkInt(std::int64_t v, Location loc = {});
ExprPtr mkReal(double v, Location loc = {});
ExprPtr mkIdent(std::string n, Location loc = {});
ExprPtr mkBinOp(std::string op, ExprPtr l, ExprPtr r, Location loc = {});
ExprPtr mkCall(std::string name, std::vector<ExprPtr> args,
               Location loc = {});
ExprPtr mkMember(std::string recv, std::string member, Location loc = {});

}  // namespace photon::lang

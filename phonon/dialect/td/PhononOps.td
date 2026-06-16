// phonon/dialect/td/PhononOps.td
//
// Phonon control-flow, classical, and function ops. The Spinor
// gate ops are NOT redeclared here; Phonon programs use them
// directly via the Spinor dialect.

#ifndef PHONON_OPS_TD
#define PHONON_OPS_TD

include "PhononDialect.td"
include "PhononTypes.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class Phonon_Op<string mnemonic, list<Trait> traits = []>
    : Op<Phonon_Dialect, mnemonic, traits>;

def Phonon_ConstIntOp : Phonon_Op<"const_int", [Pure]> {
  let summary  = "Materialise a classical integer constant.";
  let arguments = (ins SI64Attr:$value);
  let results   = (outs Phonon_IntType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
}

def Phonon_ConstAngleOp : Phonon_Op<"const_angle", [Pure]> {
  let summary  = "Materialise a classical angle constant (radians).";
  let arguments = (ins F64Attr:$value);
  let results   = (outs Phonon_AngleType:$result);
  let assemblyFormat = "$value attr-dict `:` type($result)";
}

def Phonon_BinOp : Phonon_Op<"binop", [Pure]> {
  let summary  = "Classical binary expression (+ - * /).";
  let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$op);
  let results   = (outs AnyType:$result);
}

def Phonon_CmpOp : Phonon_Op<"cmp", [Pure]> {
  let summary  = "Classical comparison (== != < >). Result is a !spinor.bit.";
  let arguments = (ins AnyType:$lhs, AnyType:$rhs, StrAttr:$op);
  // Result is a !spinor.bit, declared via the Spinor dialect.
  let results   = (outs AnyType:$result);
}

def Phonon_IfOp     : Phonon_Op<"if">     { let arguments = (ins AnyType:$predicate); }
def Phonon_ForOp    : Phonon_Op<"for">    {
  let arguments = (ins Phonon_IntType:$lo, Phonon_IntType:$hi,
                       StrAttr:$var);
}
def Phonon_WhileOp  : Phonon_Op<"while">  { let arguments = (ins AnyType:$predicate); }

def Phonon_DefOp    : Phonon_Op<"def">    {
  let arguments = (ins StrAttr:$name);
  // Parameters are encoded via attributes for round-trip with the
  // in-tree backend; the MLIR bridge maps them to function-arg
  // operand definitions.
}
def Phonon_CallOp   : Phonon_Op<"call">   {
  let arguments = (ins Variadic<AnyType>:$args, StrAttr:$name);
  let results   = (outs Variadic<AnyType>:$results);
}
def Phonon_ReturnOp : Phonon_Op<"return", [Terminator]> {
  let arguments = (ins Variadic<AnyType>:$values);
}
def Phonon_AssignOp : Phonon_Op<"assign"> {
  let arguments = (ins AnyType:$source, StrAttr:$name);
}

#endif // PHONON_OPS_TD

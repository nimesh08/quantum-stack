// spinor/dialect/td/SpinorDialect.td
//
// MLIR dialect declaration. Used only when LLVM/MLIR 22.1.x is
// available at build time. The in-tree backend
// (lib/Spinor.cpp) is the canonical implementation; this file
// is the source of truth for the *MLIR-backed* op set.
//
// Spec: spinor_engineering_deep_dive.docx Part 1 §6.

#ifndef SPINOR_DIALECT_TD
#define SPINOR_DIALECT_TD

include "mlir/IR/OpBase.td"

def Spinor_Dialect : Dialect {
  let name        = "spinor";
  let cppNamespace = "::mlir::spinor";
  let summary     = "Spinor portable / hardware quantum assembly dialect";
  let description = [{
    The spinor dialect is the in-IR representation of a Spinor
    program. Two contracts (target generic / target <device-id>)
    are encoded as the module attribute `spinor.target`. Every
    gate op consumes its qubit operand(s) and produces a fresh
    qubit value (SSA / value semantics) so that accidental reuse
    of a measured or transformed qubit is a detectable error.
  }];
  let useDefaultTypePrinterParser = 1;
}

#endif  // SPINOR_DIALECT_TD

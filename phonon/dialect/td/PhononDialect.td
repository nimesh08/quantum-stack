// phonon/dialect/td/PhononDialect.td
//
// Phonon MLIR dialect declaration. Consumed by the MLIR-backed
// bridge when QSTACK_HAVE_LLVM is ON. The in-tree backend does
// not require this file at build time.

#ifndef PHONON_DIALECT_TD
#define PHONON_DIALECT_TD

include "mlir/IR/OpBase.td"

def Phonon_Dialect : Dialect {
  let name = "phonon";
  let cppNamespace = "::phonon::mlir";
  let summary = "Phonon: structured middle layer + linear-typed optimizer IR.";
  let description = [{
    The Phonon dialect extends the Spinor dialect with control-flow,
    function definition/call, and classical-value ops. Lowering passes
    transform Phonon ops into the flat Spinor dialect; the optimizer
    runs once on Phonon, above all chips.
  }];
}

#endif // PHONON_DIALECT_TD

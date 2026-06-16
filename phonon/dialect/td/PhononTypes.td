// phonon/dialect/td/PhononTypes.td
//
// Phonon classical-value types. Spinor's !spinor.qubit and
// !spinor.bit are reused unchanged; this file declares the
// Phonon additions only.

#ifndef PHONON_TYPES_TD
#define PHONON_TYPES_TD

include "PhononDialect.td"
include "mlir/IR/AttrTypeBase.td"

class Phonon_Type<string name, string typeMnemonic>
    : TypeDef<Phonon_Dialect, name> {
  let mnemonic = typeMnemonic;
}

def Phonon_IntType   : Phonon_Type<"Int",   "int">   {
  let summary = "Classical integer (compile-time-known when used as bound).";
}
def Phonon_AngleType : Phonon_Type<"Angle", "angle"> {
  let summary = "Classical angle in radians (double-precision).";
}
def Phonon_FuncType  : Phonon_Type<"Func",  "func">  {
  let summary = "Function-handle type.";
}

#endif // PHONON_TYPES_TD

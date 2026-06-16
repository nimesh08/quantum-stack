// spinor/dialect/td/SpinorTypes.td

#ifndef SPINOR_TYPES_TD
#define SPINOR_TYPES_TD

include "SpinorDialect.td"
include "mlir/IR/AttrTypeBase.td"

class Spinor_Type<string name, string typeMnemonic, list<Trait> traits = []>
    : TypeDef<Spinor_Dialect, name, traits> {
  let mnemonic = typeMnemonic;
}

def Spinor_QubitType : Spinor_Type<"Qubit", "qubit"> {
  let summary     = "A single qubit, tracked as a distinct physical resource.";
  let description = [{
    The fundamental quantum value type. Each occurrence in the IR
    represents one logical qubit; SSA value-semantics ensures that
    using a qubit value after it has been consumed is a verifier
    error.
  }];
}

def Spinor_BitType : Spinor_Type<"Bit", "bit"> {
  let summary     = "A single classical bit holding a measurement result.";
}

def Spinor_AnyQubit : Type<CPred<"::llvm::isa<::mlir::spinor::QubitType>($_self)">,
                          "qubit type", "::mlir::spinor::QubitType">;
def Spinor_AnyBit   : Type<CPred<"::llvm::isa<::mlir::spinor::BitType>($_self)">,
                          "bit type", "::mlir::spinor::BitType">;

#endif  // SPINOR_TYPES_TD

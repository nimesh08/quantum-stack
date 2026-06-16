// spinor/dialect/td/SpinorOps.td

#ifndef SPINOR_OPS_TD
#define SPINOR_OPS_TD

include "SpinorDialect.td"
include "SpinorTypes.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class Spinor_Op<string mnemonic, list<Trait> traits = []>
    : Op<Spinor_Dialect, mnemonic, traits>;

// ---------- declarations ----------

def Spinor_AllocQubitOp : Spinor_Op<"alloc_qubit"> {
  let summary = "Allocate one fresh qubit value.";
  let arguments = (ins);
  let results   = (outs Spinor_AnyQubit:$out);
  let assemblyFormat = "attr-dict `:` type($out)";
}

def Spinor_AllocBitOp : Spinor_Op<"alloc_bit"> {
  let summary = "Allocate one classical bit slot.";
  let arguments = (ins);
  let results   = (outs Spinor_AnyBit:$out);
  let assemblyFormat = "attr-dict `:` type($out)";
}

// ---------- single-qubit standard gates ----------

class Spinor_OneQubit<string mnemonic> : Spinor_Op<mnemonic> {
  let arguments = (ins Spinor_AnyQubit:$in);
  let results   = (outs Spinor_AnyQubit:$out);
  let assemblyFormat = "$in attr-dict `:` type($out)";
}

def Spinor_HOp   : Spinor_OneQubit<"h">   { let summary = "Hadamard."; }
def Spinor_XOp   : Spinor_OneQubit<"x">;
def Spinor_YOp   : Spinor_OneQubit<"y">;
def Spinor_ZOp   : Spinor_OneQubit<"z">;
def Spinor_SOp   : Spinor_OneQubit<"s">;
def Spinor_SdgOp : Spinor_OneQubit<"sdg">;
def Spinor_TOp   : Spinor_OneQubit<"t">;
def Spinor_TdgOp : Spinor_OneQubit<"tdg">;
def Spinor_SxOp  : Spinor_OneQubit<"sx">;
def Spinor_SxdgOp: Spinor_OneQubit<"sxdg">;

class Spinor_OneQubitRot<string mnemonic> : Spinor_Op<mnemonic> {
  let arguments = (ins F64Attr:$angle, Spinor_AnyQubit:$in);
  let results   = (outs Spinor_AnyQubit:$out);
  let assemblyFormat = "`(` $angle `)` $in attr-dict `:` type($out)";
}

def Spinor_RxOp   : Spinor_OneQubitRot<"rx">;
def Spinor_RyOp   : Spinor_OneQubitRot<"ry">;
def Spinor_RzOp   : Spinor_OneQubitRot<"rz">;
def Spinor_GpiOp  : Spinor_OneQubitRot<"gpi">;
def Spinor_Gpi2Op : Spinor_OneQubitRot<"gpi2">;

def Spinor_U1qOp : Spinor_Op<"u1q"> {
  let arguments = (ins F64Attr:$theta, F64Attr:$phi, Spinor_AnyQubit:$in);
  let results   = (outs Spinor_AnyQubit:$out);
  let assemblyFormat = "`(` $theta `,` $phi `)` $in attr-dict `:` type($out)";
}

// ---------- two-qubit gates ----------

class Spinor_TwoQubit<string mnemonic> : Spinor_Op<mnemonic> {
  let arguments = (ins Spinor_AnyQubit:$a, Spinor_AnyQubit:$b);
  let results   = (outs Spinor_AnyQubit:$a_out, Spinor_AnyQubit:$b_out);
  let assemblyFormat = "$a `,` $b attr-dict `:` type($a_out) `,` type($b_out)";
}

def Spinor_CxOp   : Spinor_TwoQubit<"cx">   { let summary = "Controlled-NOT."; }
def Spinor_CzOp   : Spinor_TwoQubit<"cz">;
def Spinor_SwapOp : Spinor_TwoQubit<"swap">;
def Spinor_EcrOp  : Spinor_TwoQubit<"ecr">  { let summary = "Echoed Cross-Resonance."; }
def Spinor_MsOp   : Spinor_TwoQubit<"ms">   { let summary = "Mølmer–Sørensen."; }

def Spinor_RzzOp : Spinor_Op<"rzz"> {
  let arguments = (ins F64Attr:$angle, Spinor_AnyQubit:$a, Spinor_AnyQubit:$b);
  let results   = (outs Spinor_AnyQubit:$a_out, Spinor_AnyQubit:$b_out);
  let assemblyFormat = "`(` $angle `)` $a `,` $b attr-dict `:` type($a_out) `,` type($b_out)";
}

// ---------- measurement / reset / barrier ----------

def Spinor_MeasureOp : Spinor_Op<"measure"> {
  let summary = "Project a qubit, write the outcome to a fresh bit.";
  let arguments = (ins Spinor_AnyQubit:$in);
  let results   = (outs Spinor_AnyBit:$out);
  let assemblyFormat = "$in attr-dict `:` type($out)";
}

def Spinor_ResetOp : Spinor_Op<"reset"> {
  let summary = "Return a qubit to |0>; produces a fresh qubit value.";
  let arguments = (ins Spinor_AnyQubit:$in);
  let results   = (outs Spinor_AnyQubit:$out);
  let assemblyFormat = "$in attr-dict `:` type($out)";
}

def Spinor_BarrierOp : Spinor_Op<"barrier"> {
  let summary = "Optimization fence; carries no physical operation.";
  let arguments = (ins Variadic<Spinor_AnyQubit>:$qubits);
  let results   = (outs);
  let assemblyFormat = "($qubits^ )? attr-dict";
}

#endif  // SPINOR_OPS_TD

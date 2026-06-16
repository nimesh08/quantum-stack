// phonon/dialect/lib/Verify.cpp
//
// Structural verification for the Phonon dialect (M1).
// This is *structural* only: the linear type system is M3.

#include "phonon/dialect/Phonon.h"

#include <string>
#include <vector>

namespace phonon::dialect {

namespace {

bool isQuantumType(Type t) { return t.kind == TypeKind::Qubit; }
bool isClassicalScalar(Type t) {
  return t.kind == TypeKind::Int || t.kind == TypeKind::Angle ||
         t.kind == TypeKind::Bit;
}

}  // namespace

void verify(const Module& m, Diagnostics& diag) {
  if (m.targetAttr.empty()) {
    diag.error("module is missing target attribute");
  }

  // Walk ops; track region nesting via a stack of begin-marker
  // op kinds.
  std::vector<OpKind> stack;
  bool insideDef = false;

  for (std::uint32_t i = 0; i < m.numOps(); ++i) {
    OpId id{i};
    const Op& op = m.op(id);
    Location loc = op.loc;

    switch (op.kind) {
      // --- markers -----------------------------------------------------
      case OpKind::If: {
        if (op.operands.size() != 1) {
          diag.error("phonon.if expects exactly one predicate operand",
                     loc, id);
          break;
        }
        Type t = m.typeOf(op.operands[0]);
        if (!(t.kind == TypeKind::Bit || t.kind == TypeKind::Int)) {
          diag.error("phonon.if predicate must be of type !spinor.bit "
                     "or !phonon.int",
                     loc, id);
        }
        stack.push_back(OpKind::If);
        break;
      }
      case OpKind::EndIf:
        if (stack.empty() || stack.back() != OpKind::If) {
          diag.error("phonon.end_if without matching phonon.if", loc, id);
        } else {
          stack.pop_back();
        }
        break;
      case OpKind::For: {
        if (op.operands.size() != 2) {
          diag.error("phonon.for expects two operands (lo, hi)", loc, id);
          break;
        }
        Type tlo = m.typeOf(op.operands[0]);
        Type thi = m.typeOf(op.operands[1]);
        if (tlo.kind != TypeKind::Int || thi.kind != TypeKind::Int) {
          diag.error("phonon.for bounds must be of type !phonon.int",
                     loc, id);
        }
        stack.push_back(OpKind::For);
        break;
      }
      case OpKind::EndFor:
        if (stack.empty() || stack.back() != OpKind::For) {
          diag.error("phonon.end_for without matching phonon.for", loc, id);
        } else {
          stack.pop_back();
        }
        break;
      case OpKind::While:
        stack.push_back(OpKind::While);
        break;
      case OpKind::EndWhile:
        if (stack.empty() || stack.back() != OpKind::While) {
          diag.error("phonon.end_while without matching phonon.while",
                     loc, id);
        } else {
          stack.pop_back();
        }
        break;
      case OpKind::Def:
        if (insideDef) {
          diag.error("nested phonon.def is not allowed", loc, id);
        }
        insideDef = true;
        stack.push_back(OpKind::Def);
        break;
      case OpKind::EndDef:
        if (stack.empty() || stack.back() != OpKind::Def) {
          diag.error("phonon.end_def without matching phonon.def", loc, id);
        } else {
          stack.pop_back();
          insideDef = false;
        }
        break;
      case OpKind::Return:
        if (!insideDef) {
          diag.error("phonon.return outside of phonon.def", loc, id);
        }
        break;

      // --- classical ops ----------------------------------------------
      case OpKind::ConstInt:
      case OpKind::ConstAngle:
        // operand-free, exactly one result; producer attribute "value".
        if (!op.operands.empty() || op.results.size() != 1) {
          diag.error("phonon constant op malformed", loc, id);
        }
        break;
      case OpKind::BinOp: {
        if (op.operands.size() != 2 || op.results.size() != 1) {
          diag.error("phonon.binop expects 2 operands and 1 result",
                     loc, id);
          break;
        }
        Type ta = m.typeOf(op.operands[0]);
        Type tb = m.typeOf(op.operands[1]);
        if (ta != tb || !isClassicalScalar(ta) || ta.kind == TypeKind::Bit) {
          diag.error("phonon.binop operands must be matching int/angle",
                     loc, id);
        }
        break;
      }
      case OpKind::Cmp: {
        if (op.operands.size() != 2 || op.results.size() != 1) {
          diag.error("phonon.cmp expects 2 operands and 1 result", loc, id);
          break;
        }
        if (m.typeOf(op.results[0]).kind != TypeKind::Bit) {
          diag.error("phonon.cmp result must be of type !spinor.bit",
                     loc, id);
        }
        break;
      }
      case OpKind::Call:
      case OpKind::Assign:
        // Light-touch checks; M3 will enforce calling-convention rules.
        break;

      // --- spinor.* ops -------------------------------------------------
      default: {
        if (!isSpinorKind(op.kind)) break;  // already covered above
        int arity = qubitArity(op.kind);
        if (arity >= 0 &&
            static_cast<int>(op.operands.size()) != arity) {
          diag.error(std::string("op '") +
                     std::string(opMnemonic(op.kind)) +
                     "' has wrong qubit-operand count",
                     loc, id);
        }
        for (ValueId v : op.operands) {
          (void)v;  // type-checking handled by isQuantumType where it matters
        }
        break;
      }
    }
  }

  // Anything left on the stack is unpaired.
  for (OpKind k : stack) {
    std::string mnemonic;
    switch (k) {
      case OpKind::If:    mnemonic = "phonon.if";    break;
      case OpKind::For:   mnemonic = "phonon.for";   break;
      case OpKind::While: mnemonic = "phonon.while"; break;
      case OpKind::Def:   mnemonic = "phonon.def";   break;
      default:            mnemonic = "<unknown>";    break;
    }
    diag.error("unpaired " + mnemonic + " (missing matching end marker)");
  }

  // Suppress unused warnings.
  (void)isQuantumType;
}

}  // namespace phonon::dialect

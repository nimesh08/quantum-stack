// phonon/dialect/lib/Verify.cpp
//
// Structural verification for the Phonon dialect (M1).
// This is *structural* only: the linear type system is M3.

#include "phonon/dialect/Phonon.h"

#include <string>
#include <cmath>
#include <vector>

namespace phonon::dialect {

namespace {

bool isQuantumType(Type t) { return t.kind == TypeKind::Qubit; }
bool isClassicalScalar(Type t) {
  return t.kind == TypeKind::Int || t.kind == TypeKind::Angle ||
         t.kind == TypeKind::Bit || t.kind == TypeKind::UInt;
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
      case OpKind::LoopBody:
        if(op.results.size()!=op.operands.size()+1)diag.error("loop body result arity must include carried state and break flag",loc,id);
        for(auto value:op.operands)if(m.typeOf(value).kind!=TypeKind::UInt&&m.typeOf(value).kind!=TypeKind::Bit)diag.error("bounded loop state requires bool or uint values",loc,id);
        stack.push_back(OpKind::LoopBody);break;
      case OpKind::EndLoopBody:
        if(stack.empty()||stack.back()!=OpKind::LoopBody)diag.error("phonon.end_loop_body without matching loop body",loc,id);
        else stack.pop_back();break;
      case OpKind::Break:
      case OpKind::Continue: {
        bool loop=false;
        for(auto marker=stack.rbegin();marker!=stack.rend();++marker){if(*marker==OpKind::Def)break;if(*marker==OpKind::LoopBody){loop=true;break;}}
        if(!loop)diag.error("break/continue requires a bounded loop in this function",loc,id);break;
      }

      // --- classical ops ----------------------------------------------
      case OpKind::ConstInt:
      case OpKind::ConstAngle:
      case OpKind::ConstUInt:
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
        if (!isClassicalScalar(ta) || !isClassicalScalar(tb) ||
            (ta.kind == TypeKind::Bit && tb.kind != TypeKind::Bit)) {
          diag.error("phonon.binop operands must be numeric int/angle",
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
        int qubitCount = 0;
        for (auto operand : op.operands) if (isQuantumType(m.typeOf(operand))) ++qubitCount;
        if (arity >= 0 && qubitCount != arity) {
          diag.error(std::string("op '") +
                     std::string(opMnemonic(op.kind)) +
                     "' has wrong qubit-operand count",
                     loc, id);
        }
        std::vector<bool> parameterOperands(op.operands.size(), false);
        for (const auto& attribute : op.attributes) {
          if (attribute.name != "angle_operand" && attribute.name != "theta_operand" &&
              attribute.name != "phi_operand") continue;
          const auto* index = std::get_if<double>(&attribute.value);
          if (!index || !std::isfinite(*index) || *index < 0 ||
              std::floor(*index) != *index || *index >= op.operands.size()) {
            diag.error("symbolic gate parameter references an invalid operand", loc, id);
            continue;
          }
          const auto position = static_cast<std::size_t>(*index);
          const auto type = m.typeOf(op.operands[position]).kind;
          if (parameterOperands[position] || (type != TypeKind::Int && type != TypeKind::Angle))
            diag.error("symbolic gate parameter must reference one numeric operand", loc, id);
          parameterOperands[position] = true;
        }
        for (std::size_t position = 0; position < op.operands.size(); ++position) {
          if (!isQuantumType(m.typeOf(op.operands[position])) && !parameterOperands[position])
            diag.error("gate has an unreferenced non-qubit operand", loc, id);
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

// phonon/dialect/lib/Phonon.cpp
//
// In-tree backend for the Phonon dialect (M1).
// See include/phonon/dialect/Phonon.h.

#include "phonon/dialect/Phonon.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

namespace phonon::dialect {

// --- Type / op tables ------------------------------------------------------

std::string_view typeName(Type t) {
  switch (t.kind) {
    case TypeKind::Qubit: return "!spinor.qubit";
    case TypeKind::Bit:   return "!spinor.bit";
    case TypeKind::Int:   return "!phonon.int";
    case TypeKind::Angle: return "!phonon.angle";
    case TypeKind::Func:  return "!phonon.func";
  }
  return "<invalid>";
}

namespace {

struct OpSig {
  OpKind kind;
  const char* mnemonic;
  bool spinorKind;
  bool standard;
  int qOperands;     // number of qubit operands; -1 = variable (Barrier)
};

// Mirrors the spinor::dialect signatures, plus the Phonon additions.
constexpr OpSig kSigs[] = {
    // spinor.* ----------------------------------------------------------
    {OpKind::AllocQubit, "spinor.alloc_qubit", true,  true,  0},
    {OpKind::AllocBit,   "spinor.alloc_bit",   true,  true,  0},
    {OpKind::H,          "spinor.h",           true,  true,  1},
    {OpKind::X,          "spinor.x",           true,  true,  1},
    {OpKind::Y,          "spinor.y",           true,  true,  1},
    {OpKind::Z,          "spinor.z",           true,  true,  1},
    {OpKind::S,          "spinor.s",           true,  true,  1},
    {OpKind::Sdg,        "spinor.sdg",         true,  true,  1},
    {OpKind::T,          "spinor.t",           true,  true,  1},
    {OpKind::Tdg,        "spinor.tdg",         true,  true,  1},
    {OpKind::Rx,         "spinor.rx",          true,  true,  1},
    {OpKind::Ry,         "spinor.ry",          true,  true,  1},
    {OpKind::Rz,         "spinor.rz",          true,  true,  1},
    {OpKind::Cx,         "spinor.cx",          true,  true,  2},
    {OpKind::Cz,         "spinor.cz",          true,  true,  2},
    {OpKind::Swap,       "spinor.swap",        true,  true,  2},
    {OpKind::Ecr,        "spinor.ecr",         true,  false, 2},
    {OpKind::Ms,         "spinor.ms",          true,  false, 2},
    {OpKind::Rzz,        "spinor.rzz",         true,  false, 2},
    {OpKind::Sx,         "spinor.sx",          true,  false, 1},
    {OpKind::Sxdg,       "spinor.sxdg",        true,  false, 1},
    {OpKind::Gpi,        "spinor.gpi",         true,  false, 1},
    {OpKind::Gpi2,       "spinor.gpi2",        true,  false, 1},
    {OpKind::U1q,        "spinor.u1q",         true,  false, 1},
    {OpKind::Measure,    "spinor.measure",     true,  true,  1},
    {OpKind::Reset,      "spinor.reset",       true,  true,  1},
    {OpKind::Barrier,    "spinor.barrier",     true,  true,  -1},
    // phonon.* ----------------------------------------------------------
    {OpKind::ConstInt,   "phonon.const_int",   false, true,  0},
    {OpKind::ConstAngle, "phonon.const_angle", false, true,  0},
    {OpKind::BinOp,      "phonon.binop",       false, true,  0},
    {OpKind::Cmp,        "phonon.cmp",         false, true,  0},
    {OpKind::If,         "phonon.if",          false, true,  0},
    {OpKind::EndIf,      "phonon.end_if",      false, true,  0},
    {OpKind::For,        "phonon.for",         false, true,  0},
    {OpKind::EndFor,     "phonon.end_for",     false, true,  0},
    {OpKind::While,      "phonon.while",       false, true,  0},
    {OpKind::EndWhile,   "phonon.end_while",   false, true,  0},
    {OpKind::Def,        "phonon.def",         false, true,  0},
    {OpKind::EndDef,     "phonon.end_def",     false, true,  0},
    {OpKind::Call,       "phonon.call",        false, true,  0},
    {OpKind::Return,     "phonon.return",      false, true,  0},
    {OpKind::Assign,     "phonon.assign",      false, true,  0},
};

const OpSig& sig(OpKind k) {
  for (const auto& s : kSigs) if (s.kind == k) return s;
  throw std::logic_error("unknown phonon::OpKind");
}

}  // namespace

std::string_view opMnemonic(OpKind k)  { return sig(k).mnemonic; }
bool isSpinorKind(OpKind k)            { return sig(k).spinorKind; }
bool isPhononKind(OpKind k)            { return !sig(k).spinorKind; }
bool isStandardGate(OpKind k) {
  return sig(k).spinorKind && sig(k).standard &&
         k != OpKind::AllocQubit && k != OpKind::AllocBit &&
         k != OpKind::Measure && k != OpKind::Reset &&
         k != OpKind::Barrier;
}
bool isNativeGate(OpKind k) {
  return sig(k).spinorKind && !sig(k).standard;
}
int qubitArity(OpKind k) { return sig(k).qOperands; }

// --- Bridge ----------------------------------------------------------------

spinor::dialect::OpKind toSpinorKind(OpKind k) {
  using SK = spinor::dialect::OpKind;
  switch (k) {
    case OpKind::AllocQubit: return SK::AllocQubit;
    case OpKind::AllocBit:   return SK::AllocBit;
    case OpKind::H:          return SK::H;
    case OpKind::X:          return SK::X;
    case OpKind::Y:          return SK::Y;
    case OpKind::Z:          return SK::Z;
    case OpKind::S:          return SK::S;
    case OpKind::Sdg:        return SK::Sdg;
    case OpKind::T:          return SK::T;
    case OpKind::Tdg:        return SK::Tdg;
    case OpKind::Rx:         return SK::Rx;
    case OpKind::Ry:         return SK::Ry;
    case OpKind::Rz:         return SK::Rz;
    case OpKind::Cx:         return SK::Cx;
    case OpKind::Cz:         return SK::Cz;
    case OpKind::Swap:       return SK::Swap;
    case OpKind::Ecr:        return SK::Ecr;
    case OpKind::Ms:         return SK::Ms;
    case OpKind::Rzz:        return SK::Rzz;
    case OpKind::Sx:         return SK::Sx;
    case OpKind::Sxdg:       return SK::Sxdg;
    case OpKind::Gpi:        return SK::Gpi;
    case OpKind::Gpi2:       return SK::Gpi2;
    case OpKind::U1q:        return SK::U1q;
    case OpKind::Measure:    return SK::Measure;
    case OpKind::Reset:      return SK::Reset;
    case OpKind::Barrier:    return SK::Barrier;
    default:
      throw std::logic_error("toSpinorKind on a Phonon-only op");
  }
}

OpKind fromSpinorKind(spinor::dialect::OpKind k) {
  using SK = spinor::dialect::OpKind;
  switch (k) {
    case SK::AllocQubit: return OpKind::AllocQubit;
    case SK::AllocBit:   return OpKind::AllocBit;
    case SK::H:          return OpKind::H;
    case SK::X:          return OpKind::X;
    case SK::Y:          return OpKind::Y;
    case SK::Z:          return OpKind::Z;
    case SK::S:          return OpKind::S;
    case SK::Sdg:        return OpKind::Sdg;
    case SK::T:          return OpKind::T;
    case SK::Tdg:        return OpKind::Tdg;
    case SK::Rx:         return OpKind::Rx;
    case SK::Ry:         return OpKind::Ry;
    case SK::Rz:         return OpKind::Rz;
    case SK::Cx:         return OpKind::Cx;
    case SK::Cz:         return OpKind::Cz;
    case SK::Swap:       return OpKind::Swap;
    case SK::Ecr:        return OpKind::Ecr;
    case SK::Ms:         return OpKind::Ms;
    case SK::Rzz:        return OpKind::Rzz;
    case SK::Sx:         return OpKind::Sx;
    case SK::Sxdg:       return OpKind::Sxdg;
    case SK::Gpi:        return OpKind::Gpi;
    case SK::Gpi2:       return OpKind::Gpi2;
    case SK::U1q:        return OpKind::U1q;
    case SK::Measure:    return OpKind::Measure;
    case SK::Reset:      return OpKind::Reset;
    case SK::Barrier:    return OpKind::Barrier;
  }
  throw std::logic_error("unknown spinor::OpKind in fromSpinorKind");
}

// --- Module ----------------------------------------------------------------

ValueId Module::addValue(Type t, OpId producer) {
  ValueId id{static_cast<std::uint32_t>(values_.size())};
  values_.push_back({t, producer, /*name=*/{}});
  return id;
}
Type Module::typeOf(ValueId v) const { return values_.at(v.v).type; }
OpId Module::producerOf(ValueId v) const { return values_.at(v.v).producer; }

OpId Module::addOp(Op op) {
  OpId id{static_cast<std::uint32_t>(ops_.size())};
  ops_.push_back(std::move(op));
  return id;
}
const Op& Module::op(OpId id) const { return ops_.at(id.v); }
Op&       Module::opMut(OpId id)    { return ops_.at(id.v); }

std::string Module::nameOf(ValueId v) const {
  if (v.v >= values_.size()) return "%??";
  const auto& info = values_[v.v];
  if (!info.name.empty()) return "%" + info.name;
  return "%v" + std::to_string(v.v);
}
void Module::setName(ValueId v, std::string n) {
  values_.at(v.v).name = std::move(n);
}

}  // namespace phonon::dialect

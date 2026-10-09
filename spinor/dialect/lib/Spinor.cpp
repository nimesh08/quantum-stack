// spinor/dialect/lib/Spinor.cpp
//
// In-tree backend implementation for the Spinor dialect.
// See include/spinor/dialect/Spinor.h.

#include "spinor/dialect/Spinor.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <string>

namespace spinor::dialect {

// ---------------------------------------------------------------------------
// Type / op helpers
// ---------------------------------------------------------------------------

std::string_view typeName(Type t) {
  switch (t.kind) {
    case TypeKind::Qubit: return "!spinor.qubit";
    case TypeKind::Bit:   return "!spinor.bit";
  }
  return "<invalid>";
}

namespace {

struct OpSig {
  OpKind kind;
  const char* mnemonic;
  // qubit operand count; -1 means variable (Barrier).
  int qOperands;
  // qubit result count; equals qOperands for gates, 0/1 for measure/reset.
  int qResults;
  // does this op also produce a Bit result? (true for Measure)
  bool producesBit;
  // attribute key list (for verify): nullptr-terminated.
  const char* const* attrs;
  bool standard;  // standard set vs native
};

constexpr const char* kAngleAttr[] = {"angle", nullptr};
constexpr const char* kU1qAttrs[]  = {"theta", "phi", nullptr};
constexpr const char* kPhasedXZAttrs[] = {"x", "z", "axis_phase", nullptr};
constexpr const char* kIfAttrs[] = {"condition_clbit", "condition_value", nullptr};
constexpr const char* kNoAttrs[]   = {nullptr};

constexpr OpSig kSigs[] = {
    {OpKind::AllocQubit, "spinor.alloc_qubit", 0, 1, false, kNoAttrs,   true },
    {OpKind::AllocBit,   "spinor.alloc_bit",   0, 0, true,  kNoAttrs,   true },
    {OpKind::H,          "spinor.h",           1, 1, false, kNoAttrs,   true },
    {OpKind::X,          "spinor.x",           1, 1, false, kNoAttrs,   true },
    {OpKind::Y,          "spinor.y",           1, 1, false, kNoAttrs,   true },
    {OpKind::Z,          "spinor.z",           1, 1, false, kNoAttrs,   true },
    {OpKind::S,          "spinor.s",           1, 1, false, kNoAttrs,   true },
    {OpKind::Sdg,        "spinor.sdg",         1, 1, false, kNoAttrs,   true },
    {OpKind::T,          "spinor.t",           1, 1, false, kNoAttrs,   true },
    {OpKind::Tdg,        "spinor.tdg",         1, 1, false, kNoAttrs,   true },
    {OpKind::Rx,         "spinor.rx",          1, 1, false, kAngleAttr, true },
    {OpKind::Ry,         "spinor.ry",          1, 1, false, kAngleAttr, true },
    {OpKind::Rz,         "spinor.rz",          1, 1, false, kAngleAttr, true },
    {OpKind::Cx,         "spinor.cx",          2, 2, false, kNoAttrs,   true },
    {OpKind::Cz,         "spinor.cz",          2, 2, false, kNoAttrs,   true },
    {OpKind::Swap,       "spinor.swap",        2, 2, false, kNoAttrs,   true },
    {OpKind::Ecr,        "spinor.ecr",         2, 2, false, kNoAttrs,   false},
    {OpKind::Ms,         "spinor.ms",          2, 2, false, kNoAttrs,   false},
    {OpKind::Rzz,        "spinor.rzz",         2, 2, false, kAngleAttr, false},
    {OpKind::Rxx,        "spinor.rxx",         2, 2, false, kAngleAttr, false},
    {OpKind::Sx,         "spinor.sx",          1, 1, false, kNoAttrs,   false},
    {OpKind::Sxdg,       "spinor.sxdg",        1, 1, false, kNoAttrs,   false},
    {OpKind::Gpi,        "spinor.gpi",         1, 1, false, kAngleAttr, false},
    {OpKind::Gpi2,       "spinor.gpi2",        1, 1, false, kAngleAttr, false},
    {OpKind::U1q,        "spinor.u1q",         1, 1, false, kU1qAttrs,  false},
    {OpKind::PhasedXZ,"spinor.phased_xz",1,1,false,kPhasedXZAttrs,false},
    {OpKind::SqrtISwap,"spinor.sqrt_iswap",2,2,false,kNoAttrs,false},
    {OpKind::ISwap,"spinor.iswap",2,2,false,kNoAttrs,false},
    {OpKind::SqrtISwapInv,"spinor.sqrt_iswap_inv",2,2,false,kNoAttrs,false},
    {OpKind::Syc,"spinor.syc",2,2,false,kNoAttrs,false},
    {OpKind::Move,"spinor.move",2,2,false,kNoAttrs,false},
    {OpKind::If, "spinor.if", 0, 0, false, kIfAttrs, true},
    {OpKind::Else, "spinor.else", 0, 0, false, kNoAttrs, true},
    {OpKind::EndIf, "spinor.endif", 0, 0, false, kNoAttrs, true},
    {OpKind::GlobalPhase, "spinor.gphase", 0, 0, false, kAngleAttr, true},
    {OpKind::Measure,    "spinor.measure",     1, 0, true,  kNoAttrs,   true },
    {OpKind::Reset,      "spinor.reset",       1, 1, false, kNoAttrs,   true },
    {OpKind::Barrier,    "spinor.barrier",     -1, 0, false, kNoAttrs,  true },
    {OpKind::CConst, "spinor.c_const", 0, 0, false, kNoAttrs, true},
    {OpKind::CCopy, "spinor.c_copy", 0, 0, false, kNoAttrs, true},
    {OpKind::CNot, "spinor.c_not", 0, 0, false, kNoAttrs, true},
    {OpKind::CAnd, "spinor.c_and", 0, 0, false, kNoAttrs, true},
    {OpKind::COr, "spinor.c_or", 0, 0, false, kNoAttrs, true},
    {OpKind::CXor, "spinor.c_xor", 0, 0, false, kNoAttrs, true},
    {OpKind::CAdd, "spinor.c_add", 0, 0, false, kNoAttrs, true},
    {OpKind::CSub, "spinor.c_sub", 0, 0, false, kNoAttrs, true},
    {OpKind::CEq, "spinor.c_eq", 0, 0, false, kNoAttrs, true},
    {OpKind::CNe, "spinor.c_ne", 0, 0, false, kNoAttrs, true},
    {OpKind::CLt, "spinor.c_lt", 0, 0, false, kNoAttrs, true},
    {OpKind::CLe, "spinor.c_le", 0, 0, false, kNoAttrs, true},
    {OpKind::CGt, "spinor.c_gt", 0, 0, false, kNoAttrs, true},
    {OpKind::CGe, "spinor.c_ge", 0, 0, false, kNoAttrs, true},
    {OpKind::CShl, "spinor.c_shl", 0, 0, false, kNoAttrs, true},
    {OpKind::CShr, "spinor.c_shr", 0, 0, false, kNoAttrs, true},
    {OpKind::CCast, "spinor.c_cast", 0, 0, false, kNoAttrs, true},
    {OpKind::CSelect, "spinor.c_select", 0, 0, false, kNoAttrs, true},
};

const OpSig& sig(OpKind k) {
  for (const auto& s : kSigs) if (s.kind == k) return s;
  // Should be unreachable; the enum is closed.
  throw std::logic_error("unknown OpKind");
}

}  // namespace

std::string_view opMnemonic(OpKind k) { return sig(k).mnemonic; }
bool isStandardGate(OpKind k) { return sig(k).standard; }
bool isNativeGate(OpKind k)   { return !sig(k).standard; }
int  qubitArity(OpKind k)     { return sig(k).qOperands; }

bool Diagnostics::hasErrors() const {
  for (const auto& d : items_) {
    if (d.severity == DiagSeverity::Error) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Module
// ---------------------------------------------------------------------------

ValueId Module::addValue(Type t, OpId producer) {
  ValueId id{static_cast<std::uint32_t>(values_.size())};
  values_.push_back({t, producer, /*name=*/{}});
  return id;
}

Type Module::typeOf(ValueId v) const {
  return values_.at(v.v).type;
}

OpId Module::producerOf(ValueId v) const {
  return values_.at(v.v).producer;
}

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
  if(n=="v"+std::to_string(v.v)){values_.at(v.v).name.clear();return;}
  if(n.size()>1&&n.front()=='v'&&n.find_first_not_of("0123456789",1)==std::string::npos)n="named_"+n;
  for(std::size_t index=0;index<values_.size();++index)if(index!=v.v&&!n.empty()&&values_[index].name==n){n+="_"+std::to_string(v.v);index=static_cast<std::size_t>(-1);}
  values_.at(v.v).name = std::move(n);
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

namespace {

// Construct an op + add it + allocate results. Used by the Builder
// methods to keep them tiny.
struct Build {
  Module& m;
  OpKind kind;
  Location loc;
  std::vector<ValueId> operands;
  std::vector<Attribute> attrs;

  // Issue the op. Returns the freshly-allocated results, in the
  // signature's order (qubit results first; if `producesBit`, the
  // bit result is appended at the end).
  std::vector<ValueId> issue() {
    const OpSig& s = sig(kind);
    Op op;
    op.kind = kind;
    op.operands = std::move(operands);
    op.attributes = std::move(attrs);
    op.loc = std::move(loc);

    // Reserve OpId slot first; we fill results before pushing, so we
    // need the id up-front. We push the op without results, get its
    // id, allocate results, then back-patch op.results.
    OpId opId = m.addOp(std::move(op));
    Op& live = m.opMut(opId);

    int nq = s.qResults;
    bool bit = s.producesBit;
    int total = nq + (bit ? 1 : 0);
    live.results.reserve(static_cast<std::size_t>(total));
    for (int i = 0; i < nq; ++i) {
      live.results.push_back(m.addValue(qubitType(), opId));
    }
    if (bit) {
      live.results.push_back(m.addValue(bitType(), opId));
    }
    return live.results;  // copy of the vector
  }
};

}  // namespace

ValueId Builder::allocQubit(Location loc) {
  Build b{m_, OpKind::AllocQubit, std::move(loc), {}, {}};
  auto r = b.issue();
  return r[0];
}

ValueId Builder::allocBit(Location loc) {
  ++m_.numClbits;
  Build b{m_, OpKind::AllocBit, std::move(loc), {}, {}};
  auto r = b.issue();
  return r[0];
}

#define SQG(name, kind_)                                              \
  ValueId Builder::name(ValueId q, Location loc) {                    \
    Build b{m_, OpKind::kind_, std::move(loc), {q}, {}};              \
    auto r = b.issue();                                               \
    return r[0];                                                      \
  }

SQG(h,    H)
SQG(x,    X)
SQG(y,    Y)
SQG(z,    Z)
SQG(s,    S)
SQG(sdg,  Sdg)
SQG(t,    T)
SQG(tdg,  Tdg)
SQG(sx,   Sx)
SQG(sxdg, Sxdg)
#undef SQG

#define SQGA(name, kind_)                                             \
  ValueId Builder::name(double angle, ValueId q, Location loc) {      \
    Build b{m_, OpKind::kind_, std::move(loc), {q},                   \
            {Attribute{"angle", angle}}};                             \
    auto r = b.issue();                                               \
    return r[0];                                                      \
  }

SQGA(rx,   Rx)
SQGA(ry,   Ry)
SQGA(rz,   Rz)
SQGA(gpi,  Gpi)
SQGA(gpi2, Gpi2)
#undef SQGA

ValueId Builder::phasedXZ(double x,double z,double axis,ValueId q,Location loc){
  Build op{m_,OpKind::PhasedXZ,std::move(loc),{q},{namedDouble("x",x),namedDouble("z",z),namedDouble("axis_phase",axis)}};
  return op.issue()[0];
}
void Builder::beginIf(std::size_t clbit,bool value,Location loc) {
  m_.addOp({OpKind::If,{}, {}, {namedDouble("condition_clbit",clbit),namedDouble("condition_value",value?1:0)},std::move(loc)});
  m_.numClbits=std::max(m_.numClbits,clbit+1);
}
void Builder::elseBranch(Location loc){m_.addOp({OpKind::Else,{}, {},{},std::move(loc)});}
void Builder::endIf(Location loc){m_.addOp({OpKind::EndIf,{}, {},{},std::move(loc)});}
void Builder::globalPhase(double angle,Location loc){m_.addOp({OpKind::GlobalPhase,{}, {},{angleAttr(angle)},std::move(loc)});}

ValueId Builder::u1q(double theta, double phi, ValueId q, Location loc) {
  Build b{m_, OpKind::U1q, std::move(loc), {q},
          {Attribute{"theta", theta}, Attribute{"phi", phi}}};
  auto r = b.issue();
  return r[0];
}

#define TQG(name, kind_)                                              \
  std::pair<ValueId, ValueId> Builder::name(ValueId a, ValueId b_,    \
                                            Location loc) {           \
    Build b{m_, OpKind::kind_, std::move(loc), {a, b_}, {}};          \
    auto r = b.issue();                                               \
    return {r[0], r[1]};                                              \
  }

TQG(cx,   Cx)
TQG(cz,   Cz)
TQG(swap, Swap)
TQG(ecr,  Ecr)
TQG(ms,   Ms)
TQG(sqrtISwap,SqrtISwap)
TQG(iSwap,ISwap)
TQG(sqrtISwapInv,SqrtISwapInv)
TQG(syc,Syc)
#undef TQG

std::pair<ValueId, ValueId> Builder::rzz(double angle, ValueId a,
                                         ValueId b_, Location loc) {
  Build b{m_, OpKind::Rzz, std::move(loc), {a, b_},
          {Attribute{"angle", angle}}};
  auto r = b.issue();
  return {r[0], r[1]};
}

ValueId Builder::measure(ValueId q, Location loc) {
  Build b{m_, OpKind::Measure, std::move(loc), {q}, {}};
  auto r = b.issue();
  // Measure has 0 qubit results + 1 bit result, so r has exactly 1 elt.
  return r.front();
}

std::pair<ValueId, ValueId> Builder::rxx(double angle, ValueId a,
                                      ValueId b_, Location loc) {
  Build b{m_, OpKind::Rxx, std::move(loc), {a, b_}, {angleAttr(angle)}};
  auto r = b.issue();
  return {r[0], r[1]};
}

void setMeasurementTarget(Module& m, ValueId bit, std::size_t index) {
  auto& op = m.opMut(m.producerOf(bit));
  if (op.kind != OpKind::Measure) throw std::invalid_argument("expected measurement result");
  for (auto& attr : op.attributes) if (attr.name == "clbit") {
    attr.value = static_cast<double>(index);
    m.numClbits = std::max(m.numClbits, index + 1);
    return;
  }
  op.attributes.push_back(namedDouble("clbit", static_cast<double>(index)));
  m.numClbits = std::max(m.numClbits, index + 1);
}

ValueId Builder::reset(ValueId q, Location loc) {
  Build b{m_, OpKind::Reset, std::move(loc), {q}, {}};
  auto r = b.issue();
  return r[0];
}

void Builder::barrier(std::span<const ValueId> qs, Location loc) {
  Build b{m_, OpKind::Barrier, std::move(loc),
          std::vector<ValueId>(qs.begin(), qs.end()), {}};
  // Barrier has no results.
  (void)b.issue();
}

}  // namespace spinor::dialect

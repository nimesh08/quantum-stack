// phonon/dialect/lib/Builder.cpp
//
// Builder methods for the Phonon dialect.

#include "phonon/dialect/Phonon.h"
#include "spinor/dialect/ExactInteger.h"
#include <cstdlib>
#include <stdexcept>

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

namespace phonon::dialect {

namespace {

// Helper: issue an op with the given operands, attributes, location,
// and result-type list. Returns the freshly-allocated result IDs.
std::vector<ValueId> issue(Module& m, OpKind kind,
                           std::vector<ValueId> operands,
                           std::vector<Attribute> attrs,
                           std::vector<Type> resultTypes,
                           Location loc) {
  Op op;
  op.kind = kind;
  op.operands = std::move(operands);
  op.attributes = std::move(attrs);
  op.loc = std::move(loc);
  OpId opId = m.addOp(std::move(op));
  Op& live = m.opMut(opId);
  live.results.reserve(resultTypes.size());
  for (Type t : resultTypes) {
    live.results.push_back(m.addValue(t, opId));
  }
  return live.results;
}

}  // namespace

// --- Spinor-kind ops -------------------------------------------------------

ValueId Builder::allocQubit(Location loc) {
  if(!reusableQubits_.empty()){const auto value=reusableQubits_.back();reusableQubits_.pop_back();return reset(value,std::move(loc));}
  auto r = issue(m_, OpKind::AllocQubit, {}, {}, {qubitType()}, std::move(loc));
  return r[0];
}
void Builder::discard(ValueId q,Location loc){reusableQubits_.push_back(reset(q,std::move(loc)));}
ValueId Builder::allocBit(Location loc) {
  auto r = issue(m_, OpKind::AllocBit, {}, {}, {bitType()}, std::move(loc));
  return r[0];
}
void Builder::globalPhase(double angle, Location loc) {
  issue(m_, OpKind::GlobalPhase, {}, {Attribute{"angle", angle}}, {}, std::move(loc));
}

#define SQG(name, kind_)                                              \
  ValueId Builder::name(ValueId q, Location loc) {                    \
    auto r = issue(m_, OpKind::kind_, {q}, {}, {qubitType()},         \
                   std::move(loc));                                    \
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
    auto r = issue(m_, OpKind::kind_, {q},                            \
                   {Attribute{"angle", angle}},                        \
                   {qubitType()}, std::move(loc));                     \
    return r[0];                                                      \
  }

SQGA(rx,   Rx)
SQGA(ry,   Ry)
SQGA(rz,   Rz)
SQGA(gpi,  Gpi)
SQGA(gpi2, Gpi2)
#undef SQGA

ValueId Builder::u1q(double theta, double phi, ValueId q, Location loc) {
  auto r = issue(m_, OpKind::U1q, {q},
                 {Attribute{"theta", theta}, Attribute{"phi", phi}},
                 {qubitType()}, std::move(loc));
  return r[0];
}

#define TQG(name, kind_)                                              \
  std::pair<ValueId, ValueId> Builder::name(ValueId a, ValueId b_,    \
                                            Location loc) {           \
    auto r = issue(m_, OpKind::kind_, {a, b_}, {},                    \
                   {qubitType(), qubitType()}, std::move(loc));        \
    return {r[0], r[1]};                                              \
  }

TQG(cx,   Cx)
TQG(cz,   Cz)
TQG(swap, Swap)
TQG(ecr,  Ecr)
TQG(ms,   Ms)
#undef TQG

std::pair<ValueId, ValueId> Builder::rzz(double angle, ValueId a,
                                         ValueId b_, Location loc) {
  auto r = issue(m_, OpKind::Rzz, {a, b_},
                 {Attribute{"angle", angle}},
                 {qubitType(), qubitType()}, std::move(loc));
  return {r[0], r[1]};
}

std::pair<ValueId, ValueId> Builder::rxx(double angle, ValueId a,
                                         ValueId b_, Location loc) {
  auto r = issue(m_, OpKind::Rxx, {a, b_},
                 {Attribute{"angle", angle}},
                 {qubitType(), qubitType()}, std::move(loc));
  return {r[0], r[1]};
}

ValueId Builder::measure(ValueId q, Location loc) {
  auto r = issue(m_, OpKind::Measure, {q}, {}, {bitType()}, std::move(loc));
  return r[0];
}
ValueId Builder::reset(ValueId q, Location loc) {
  auto r = issue(m_, OpKind::Reset, {q}, {}, {qubitType()}, std::move(loc));
  return r[0];
}
void Builder::barrier(std::span<const ValueId> qs, Location loc) {
  issue(m_, OpKind::Barrier,
        std::vector<ValueId>(qs.begin(), qs.end()), {}, {},
        std::move(loc));
}

// --- Phonon classical ops --------------------------------------------------

ValueId Builder::constInt(int64_t v, Location loc) {
  auto r = issue(m_, OpKind::ConstInt, {},
                 {Attribute{"value", v}},
                 {intType()}, std::move(loc));
  return r[0];
}
ValueId Builder::constAngle(double rad, Location loc) {
  auto r = issue(m_, OpKind::ConstAngle, {},
                 {Attribute{"value", rad}}, {angleType()},
                 std::move(loc));
  return r[0];
}
ValueId Builder::constUInt(std::uint64_t v, std::uint32_t width, Location loc) {
  return issue(m_,OpKind::ConstUInt,{},{{"value",v}},{uintType(width)},std::move(loc))[0];
}
ValueId Builder::copy(ValueId value,Type type,Location loc) {
  return issue(m_,OpKind::Copy,{value},{},{type},std::move(loc))[0];
}
ValueId Builder::bitNot(ValueId value,Location loc){
  return issue(m_,OpKind::Copy,{value},{{"complement",1.0}},{m_.typeOf(value)},std::move(loc))[0];
}
ValueId Builder::select(ValueId predicate,ValueId thenValue,ValueId elseValue,Location loc) {
  return issue(m_,OpKind::Select,{predicate,thenValue,elseValue},{},{m_.typeOf(thenValue)},std::move(loc))[0];
}
void Builder::output(std::string name,ValueId value,std::string role,Location loc) {
  issue(m_,OpKind::Output,{value},{{"name",std::move(name)},{"role",std::move(role)}},{},std::move(loc));
}
Builder::BoundedLoopResult Builder::boundedWhile(std::size_t maxIterations,std::span<const ValueId> initial,
    const std::function<ValueId(std::span<const ValueId>)>& condition,
    const std::function<LoopStep(std::span<const ValueId>)>& body,Location loc){
  std::size_t budget=100000;if(const auto* text=std::getenv("QSTACK_EXPANDED_OPERATION_BUDGET"))budget=spinor::dialect::parseExactInteger<std::size_t>(text);
  if(!maxIterations||!budget||maxIterations>budget)throw std::invalid_argument("boundedWhile requires a positive bound within QSTACK_EXPANDED_OPERATION_BUDGET");
  const auto start=m_.numOps();std::vector<ValueId> values(initial.begin(),initial.end());
  for(auto value:values)if(m_.typeOf(value).kind!=TypeKind::UInt&&m_.typeOf(value).kind!=TypeKind::Bit)throw std::invalid_argument("boundedWhile state must contain bool or explicit uint values");
  auto live=copy(constInt(1,loc),bitType(),loc);
  for(std::size_t iteration=0;iteration<maxIterations;++iteration){
    const auto conditionGuard=beginIf(live,loc);
    const auto test=condition(values);if(m_.typeOf(test)!=bitType())throw std::invalid_argument("boundedWhile condition must produce bool");
    endIf(conditionGuard,loc);
    const auto predicate=select(live,test,copy(constInt(0,loc),bitType(),loc),loc);
    live=predicate;const auto marker=beginIf(predicate,loc);
    std::vector<Type> types;for(auto value:values)types.push_back(m_.typeOf(value));
    auto resultTypes=types;resultTypes.push_back(bitType());
    const auto bodyResults=issue(m_,OpKind::LoopBody,values,{},resultTypes,loc);
    loopTypes_.push_back(types);
    LoopStep step;
    try{step=body(values);}catch(...){loopTypes_.pop_back();throw;}
    loopTypes_.pop_back();
    if(step.values.size()!=values.size())throw std::invalid_argument("boundedWhile body changed carried value arity");
    for(std::size_t i=0;i<values.size();++i)if(m_.typeOf(values[i])!=m_.typeOf(step.values[i]))throw std::invalid_argument("boundedWhile body changed carried value type or width");
    auto yielded=step.values;
    if(step.breakWhen){if(m_.typeOf(*step.breakWhen)!=bitType())throw std::invalid_argument("boundedWhile breakWhen must be bool");yielded.push_back(*step.breakWhen);}
    issue(m_,OpKind::EndLoopBody,yielded,{{"has_break",std::int64_t(step.breakWhen.has_value())}},{},loc);
    const auto nextLive=select(bodyResults.back(),copy(constInt(0,loc),bitType(),loc),live,loc);
    endIf(marker,loc);
    for(std::size_t i=0;i<values.size();++i)values[i]=select(predicate,bodyResults[i],values[i],loc);
    if(nextLive!=live)live=select(predicate,nextLive,live,loc);
    if(m_.numOps()-start>budget)throw std::invalid_argument("boundedWhile expanded operation budget exceeded");
  }
  const auto conditionGuard=beginIf(live,loc);
  const auto test=condition(values);if(m_.typeOf(test)!=bitType())throw std::invalid_argument("boundedWhile condition must produce bool");
  endIf(conditionGuard,loc);
  const auto exhausted=select(live,test,copy(constInt(0,loc),bitType(),loc),loc);
  output("loop_exhausted_builder_"+std::to_string(start),exhausted,"loop_exhausted",loc);
  if(m_.numOps()-start>budget)throw std::invalid_argument("boundedWhile expanded operation budget exceeded");
  return {values,exhausted};
}
void Builder::breakLoop(std::span<const ValueId> values,Location loc){
  if(loopTypes_.empty())throw std::invalid_argument("breakLoop requires an enclosing boundedWhile callback");
  if(values.size()!=loopTypes_.back().size())throw std::invalid_argument("breakLoop changes loop-carried arity");
  for(std::size_t i=0;i<values.size();++i)if(m_.typeOf(values[i])!=loopTypes_.back()[i])throw std::invalid_argument("breakLoop changes loop-carried type or width");
  issue(m_,OpKind::Break,std::vector<ValueId>(values.begin(),values.end()),{},{},std::move(loc));
}
void Builder::continueLoop(std::span<const ValueId> values,Location loc){
  if(loopTypes_.empty())throw std::invalid_argument("continueLoop requires an enclosing boundedWhile callback");
  if(values.size()!=loopTypes_.back().size())throw std::invalid_argument("continueLoop changes loop-carried arity");
  for(std::size_t i=0;i<values.size();++i)if(m_.typeOf(values[i])!=loopTypes_.back()[i])throw std::invalid_argument("continueLoop changes loop-carried type or width");
  issue(m_,OpKind::Continue,std::vector<ValueId>(values.begin(),values.end()),{},{},std::move(loc));
}
ValueId Builder::binOp(std::string op, ValueId a, ValueId b, Location loc) {
  // Result type follows the operand type (int + int → int, angle + angle
  // → angle). We probe `a`'s type.
  Type rt = m_.typeOf(a).kind == TypeKind::Angle || m_.typeOf(b).kind == TypeKind::Angle
      ? angleType() : m_.typeOf(a);
  auto r = issue(m_, OpKind::BinOp, {a, b},
                 {Attribute{"op", std::move(op)}}, {rt},
                 std::move(loc));
  return r[0];
}
ValueId Builder::cmp(std::string op, ValueId a, ValueId b, Location loc) {
  auto r = issue(m_, OpKind::Cmp, {a, b},
                 {Attribute{"op", std::move(op)}}, {bitType()},
                 std::move(loc));
  return r[0];
}

// --- Phonon control-flow ---------------------------------------------------

OpId Builder::beginIf(ValueId predicate, Location loc) {
  Op op;
  op.kind = OpKind::If;
  op.operands = {predicate};
  op.loc = std::move(loc);
  return m_.addOp(std::move(op));
}
void Builder::elseIf(OpId begin) {
  // Mark the start of else by recording the current op count as
  // "then_count" on the begin op.
  Op& b = m_.opMut(begin);
  bool already = false;
  for (const auto& a : b.attributes) if (a.name == "then_count") already = true;
  if (already) return;
  std::uint32_t pos = static_cast<std::uint32_t>(m_.numOps());
  std::uint32_t base = begin.v + 1;
  b.attributes.push_back(Attribute{"then_count",
                                   static_cast<double>(pos - base)});
}
void Builder::endIf(OpId begin, Location loc) {
  // If `elseIf` was not called, set "then_count" = body length and
  // "else_count" = 0; otherwise compute "else_count" from the gap.
  std::uint32_t endPos = static_cast<std::uint32_t>(m_.numOps());
  std::uint32_t base   = begin.v + 1;
  Op& b = m_.opMut(begin);
  bool hasThen = false;
  std::uint32_t thenCount = 0;
  for (const auto& a : b.attributes) {
    if (a.name == "then_count") {
      hasThen = true;
      thenCount = static_cast<std::uint32_t>(std::get<double>(a.value));
    }
  }
  if (!hasThen) {
    thenCount = endPos - base;
    b.attributes.push_back(Attribute{"then_count",
                                     static_cast<double>(thenCount)});
    b.attributes.push_back(Attribute{"else_count", 0.0});
  } else {
    std::uint32_t elseCount = endPos - (base + thenCount);
    b.attributes.push_back(Attribute{"else_count",
                                     static_cast<double>(elseCount)});
  }
  Op end;
  end.kind = OpKind::EndIf;
  end.loc = std::move(loc);
  m_.addOp(std::move(end));
}

OpId Builder::beginFor(std::string var, ValueId lo, ValueId hi,
                       Location loc) {
  Op op;
  op.kind = OpKind::For;
  op.operands = {lo, hi};
  op.attributes = {Attribute{"var", std::move(var)}};
  op.loc = std::move(loc);
  return m_.addOp(std::move(op));
}
void Builder::endFor(OpId begin, Location loc) {
  std::uint32_t endPos = static_cast<std::uint32_t>(m_.numOps());
  std::uint32_t base   = begin.v + 1;
  Op& b = m_.opMut(begin);
  b.attributes.push_back(Attribute{"body_count",
                                   static_cast<double>(endPos - base)});
  Op end;
  end.kind = OpKind::EndFor;
  end.loc = std::move(loc);
  m_.addOp(std::move(end));
}

OpId Builder::beginWhile(ValueId predicate, Location loc) {
  Op op;
  op.kind = OpKind::While;
  op.operands = {predicate};
  op.loc = std::move(loc);
  return m_.addOp(std::move(op));
}
void Builder::endWhile(OpId begin, Location loc) {
  std::uint32_t endPos = static_cast<std::uint32_t>(m_.numOps());
  std::uint32_t base   = begin.v + 1;
  Op& b = m_.opMut(begin);
  b.attributes.push_back(Attribute{"body_count",
                                   static_cast<double>(endPos - base)});
  Op end;
  end.kind = OpKind::EndWhile;
  end.loc = std::move(loc);
  m_.addOp(std::move(end));
}

OpId Builder::beginDef(std::string name, std::span<const Param> params,
                       Location loc) {
  Op op;
  op.kind = OpKind::Def;
  op.attributes = {Attribute{"name", std::move(name)},
                   Attribute{"param_count",
                             static_cast<double>(params.size())}};
  op.loc = std::move(loc);
  for (const auto& p : params) {
    op.attributes.push_back(Attribute{"param_type",
                                       std::string(typeName(p.type))});
    op.attributes.push_back(Attribute{"param_name", p.name});
  }
  OpId defId = m_.addOp(std::move(op));
  // Allocate parameter values in module value table; their producer
  // is the def op itself.
  for (const auto& p : params) {
    ValueId vid = m_.addValue(p.type, defId);
    m_.setName(vid, p.name);
    m_.opMut(defId).results.push_back(vid);
  }
  return defId;
}
void Builder::endDef(OpId begin, Location loc) {
  std::uint32_t endPos = static_cast<std::uint32_t>(m_.numOps());
  std::uint32_t base   = begin.v + 1;
  Op& b = m_.opMut(begin);
  b.attributes.push_back(Attribute{"body_count",
                                   static_cast<double>(endPos - base)});
  Op end;
  end.kind = OpKind::EndDef;
  end.loc = std::move(loc);
  m_.addOp(std::move(end));
}
OpId Builder::beginTypedDef(std::string name,std::span<const Param> params,std::span<const Type> results,Location loc){
  const auto id=beginDef(std::move(name),params,std::move(loc));
  auto& op=m_.opMut(id);op.attributes.push_back({"explicit_results",std::int64_t{1}});
  for(auto type:results){op.attributes.push_back({"result_type",std::string(typeName(type))});if(type.kind==TypeKind::UInt)op.attributes.push_back({"result_width",std::uint64_t(type.width)});}
  return id;
}
void Builder::returnOp(std::span<const ValueId> values, Location loc) {
  Op op;
  op.kind = OpKind::Return;
  op.operands = std::vector<ValueId>(values.begin(), values.end());
  op.loc = std::move(loc);
  m_.addOp(std::move(op));
}
std::vector<ValueId> Builder::call(std::string name,
                                   std::span<const ValueId> args,
                                   std::span<const Type> resultTypes,
                                   Location loc) {
  Op op;
  op.kind = OpKind::Call;
  op.operands = std::vector<ValueId>(args.begin(), args.end());
  op.attributes = {Attribute{"name", std::move(name)}};
  op.loc = std::move(loc);
  OpId id = m_.addOp(std::move(op));
  std::vector<ValueId> rs;
  rs.reserve(resultTypes.size());
  for (Type t : resultTypes) {
    ValueId v = m_.addValue(t, id);
    rs.push_back(v);
    m_.opMut(id).results.push_back(v);
  }
  return rs;
}

ValueId Builder::paramValue(OpId defId, std::size_t paramIdx) const {
  return m_.op(defId).results.at(paramIdx);
}

void Builder::assign(std::string name, ValueId v, Location loc) {
  Op op;
  op.kind = OpKind::Assign;
  op.operands = {v};
  op.attributes = {Attribute{"name", std::move(name)}};
  op.loc = std::move(loc);
  m_.addOp(std::move(op));
}

}  // namespace phonon::dialect

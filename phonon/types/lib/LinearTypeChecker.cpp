// phonon/types/lib/LinearTypeChecker.cpp

#include "phonon/types/LinearTypeChecker.h"
#include "spinor/dialect/ExactInteger.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace phonon::types {

namespace pd = phonon::dialect;

Options optionsForTarget(const spinor::verify::TargetInfo& t) {
  Options o;
  o.midCircuitMeasure = t.midCircuitMeasure;
  return o;
}

namespace {

struct ValueState {
  bool consumed = false;
  bool measured = false;
  pd::OpId producer = pd::kInvalidOp;
  pd::OpKind producerKind = pd::OpKind::AllocQubit;
  bool terminalMeasure = false;
};

bool isQubit(pd::Type t) { return t.kind == pd::TypeKind::Qubit; }

// Validate quantum uses, including non-consuming measurement/barrier reads.
// Return and call transfer ownership of their quantum operands.
bool checksQubitOperands(pd::OpKind k) {
  if (pd::isSpinorKind(k)) {
    // Allocations have no quantum input to validate.
    return k != pd::OpKind::AllocQubit && k != pd::OpKind::AllocBit;
  }
  return k == pd::OpKind::Call || k == pd::OpKind::Return ||
         k == pd::OpKind::If   || k == pd::OpKind::While;
}

}  // namespace

bool typecheck(const pd::Module& m, const Options& opt,
               pd::Diagnostics& diag) {
  std::size_t errsBefore = 0;
  for (const auto& d : diag.items()) {
    if (d.severity == pd::DiagSeverity::Error) ++errsBefore;
  }

  std::unordered_map<std::uint32_t, ValueState> states;

  auto setState = [&](pd::ValueId v, ValueState s) {
    states[v.v] = s;
  };
  const auto mergeState=[](ValueState& into,const ValueState& other){
    const bool staleGate=(into.consumed&&!into.terminalMeasure)||(other.consumed&&!other.terminalMeasure);
    into.consumed|=other.consumed;into.measured|=other.measured;into.terminalMeasure=into.consumed&&!staleGate;
  };

  const auto matching=[&](std::uint32_t start,pd::OpKind begin,pd::OpKind end){
    std::size_t depth=1;auto i=start+1;for(;i<m.numOps();++i){const auto kind=m.op(pd::OpId{i}).kind;if(kind==begin)++depth;if(kind==end&&!--depth)break;}return i;
  };
  // A source branch threads mutable quantum slots into the next textual arm.
  // On the path that did not execute those gates, their result names denote
  // identity forwarding of the original wire, never a copy of its state.
  auto skipAliases=[&](std::uint32_t lo,std::uint32_t hi){
    for(auto i=lo;i<hi;++i){const auto& op=m.op(pd::OpId{i});std::vector<pd::ValueId> inputs;
      for(auto value:op.operands)if(isQubit(m.typeOf(value)))inputs.push_back(value);
      std::size_t slot=0;
      for(auto result:op.results)if(isQubit(m.typeOf(result))){
        ValueState fresh;fresh.producer=pd::OpId{i};fresh.producerKind=op.kind;
        if(slot<inputs.size()){auto& original=states[inputs[slot++].v];fresh.measured=original.measured;original.consumed=true;}
        states[result.v]=fresh;
      }
    }
  };
  std::function<bool(std::uint32_t,std::uint32_t)> visit;
  std::function<std::optional<std::int64_t>(pd::ValueId)> integer;
  integer=[&](pd::ValueId value)->std::optional<std::int64_t>{
    const auto& op=m.op(m.producerOf(value));
    if(op.kind==pd::OpKind::ConstInt)for(const auto& attr:op.attributes)if(attr.name=="value")if(const auto* exact=std::get_if<std::int64_t>(&attr.value))return *exact;
    if(op.kind==pd::OpKind::BinOp&&op.operands.size()==2){const auto a=integer(op.operands[0]),b=integer(op.operands[1]);if(a&&b)for(const auto& attr:op.attributes)if(attr.name=="op")try{return spinor::dialect::checkedInteger(std::get<std::string>(attr.value),*a,*b);}catch(...){return std::nullopt;}}
    return std::nullopt;
  };
  std::vector<std::vector<std::unordered_map<std::uint32_t,ValueState>>> loopTransfers;
  visit=[&](std::uint32_t lo,std::uint32_t hi){
  for (std::uint32_t i = lo; i < hi; ++i) {
    pd::OpId id{i};
    const pd::Op& op = m.op(id);
    if(op.kind==pd::OpKind::For||op.kind==pd::OpKind::While){
      const auto end=matching(i,op.kind,op.kind==pd::OpKind::For?pd::OpKind::EndFor:pd::OpKind::EndWhile);
      std::optional<std::int64_t> low,high;
      if(op.kind==pd::OpKind::For&&op.operands.size()==2){low=integer(op.operands[0]);high=integer(op.operands[1]);}
      if(low&&high&&*high<=*low){skipAliases(i+1,end);i=end;continue;}
      const auto before=states;const bool terminated=visit(i+1,end);const auto executed=states;
      if(low&&high){if(terminated)return true;}
      else{
        // An unknown static bound may execute zero times. A body return cannot
        // hide reachable ownership checks in the following continuation.
        states=before;skipAliases(i+1,end);
        if(!terminated)for(const auto& [value,state]:executed){auto found=states.find(value);if(found==states.end())states[value]=state;else mergeState(found->second,state);}
      }i=end;continue;
    }
    if(op.kind==pd::OpKind::LoopBody){
      const auto end=matching(i,pd::OpKind::LoopBody,pd::OpKind::EndLoopBody);
      loopTransfers.emplace_back();const bool terminated=visit(i+1,end);
      auto exits=std::move(loopTransfers.back());loopTransfers.pop_back();
      if(!terminated)exits.push_back(states);
      if(exits.empty())return true;
      states=exits.front();for(std::size_t path=1;path<exits.size();++path)for(const auto& [value,state]:exits[path]){
        auto found=states.find(value);if(found==states.end())states[value]=state;
        else mergeState(found->second,state);
      }i=end;continue;
    }
    if(op.kind==pd::OpKind::Def){
      const auto end=matching(i,pd::OpKind::Def,pd::OpKind::EndDef);const auto outer=states;
      for(auto value:op.results)if(isQubit(m.typeOf(value)))states[value.v]={false,false,id,pd::OpKind::Def};
      visit(i+1,end);states=outer;i=end;continue;
    }
    if(op.kind==pd::OpKind::If){
      const auto end=matching(i,pd::OpKind::If,pd::OpKind::EndIf);auto count=end-i-1;std::uint32_t other=0;
      for(const auto& attr:op.attributes){if(attr.name=="then_count")count=static_cast<std::uint32_t>(std::get<double>(attr.value));if(attr.name=="else_count")other=static_cast<std::uint32_t>(std::get<double>(attr.value));}
      const auto before=states;const bool thenDone=visit(i+1,i+1+count);
      if(!thenDone)skipAliases(i+1+count,i+1+count+other);
      const auto yes=states;states=before;skipAliases(i+1,i+1+count);const bool elseDone=visit(i+1+count,i+1+count+other);
      if(thenDone&&elseDone)return true;
      if(elseDone)states=yes;
      else if(!thenDone){for(const auto& [value,state]:yes){auto found=states.find(value);if(found==states.end())states[value]=state;else mergeState(found->second,state);}}
      i=end;continue;
    }

    bool isMeasure = (op.kind == pd::OpKind::Measure);
    bool isReset   = (op.kind == pd::OpKind::Reset);
    const bool isBarrier=(op.kind==pd::OpKind::Barrier);

    if (checksQubitOperands(op.kind)) {
      std::unordered_set<std::uint32_t> operandsSeen;
      for (pd::ValueId operand : op.operands) {
        if (!isQubit(m.typeOf(operand))) continue;
        if(!operandsSeen.insert(operand.v).second){diag.error("E1: duplicate quantum operand "+m.nameOf(operand),op.loc,id);continue;}
        auto it = states.find(operand.v);
        if (it == states.end()) {
          // Operand is a function parameter or alloc result: bootstrap.
          ValueState boot;
          boot.producer = m.producerOf(operand);
          states[operand.v] = boot;
          it = states.find(operand.v);
        }
        ValueState& s = it->second;
        if (s.consumed && !(isReset && s.measured) && !(isBarrier&&s.terminalMeasure)) {
          diag.error("E1: qubit value " + m.nameOf(operand) +
                     " used more than once (no-cloning)",
                     op.loc, id);
        } else if (s.measured && !isReset && !isBarrier && !opt.midCircuitMeasure) {
          diag.error("E2: qubit " + m.nameOf(operand) +
                     " used after measurement (chip lacks "
                     "mid-circuit measurement)",
                     op.loc, id);
        }
        // Measurement projects the state on this existing wire; it neither
        // destroys the physical qubit nor creates another quantum value. The
        // IR measure operation returns only classical data, so a supported
        // mid-circuit measurement leaves its quantum operand available for a
        // subsequent gate, reset, or repeated measurement.
        // Barrier is a scheduling fence with no replacement quantum SSA value.
        // Validate its operands as reads, retaining both their ownership and
        // any previous consumption state (a stale alias remains an error).
        if(!isBarrier){s.consumed = !(isMeasure && opt.midCircuitMeasure);s.terminalMeasure=isMeasure&&!opt.midCircuitMeasure;}
        if (isMeasure) s.measured = true;
        if (isReset) s.measured = false;
      }
    }

    // Allocate fresh state for every qubit-typed result.
    for (pd::ValueId r : op.results) {
      if (!isQubit(m.typeOf(r))) continue;
      ValueState fresh;
      fresh.producer = id;
      fresh.producerKind = op.kind;
      setState(r, fresh);
    }
    if(op.kind==pd::OpKind::Break||op.kind==pd::OpKind::Continue){if(!loopTransfers.empty())loopTransfers.back().push_back(states);return true;}
    if(op.kind==pd::OpKind::Return)return true;
  }
  return false;
  };
  visit(0,static_cast<std::uint32_t>(m.numOps()));

  if (opt.warnImplicitDiscard) {
    for (const auto& [vid, st] : states) {
      pd::ValueId v{vid};
      if (v.v >= m.numValues()) continue;
      if (!isQubit(m.typeOf(v))) continue;
      if (st.consumed) continue;
      // Only warn for qubit values whose producer is *not* the alloc
      // op (i.e. an intermediate gate result that is dead). The alloc
      // case is handled below.
      diag.warn("qubit " + m.nameOf(v) +
                " is implicitly discarded (produced but never used)");
    }
  }

  std::size_t errsAfter = 0;
  for (const auto& d : diag.items()) {
    if (d.severity == pd::DiagSeverity::Error) ++errsAfter;
  }
  return errsAfter == errsBefore;
}

}  // namespace phonon::types

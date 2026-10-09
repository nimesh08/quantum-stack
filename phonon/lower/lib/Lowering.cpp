// phonon/lower/lib/Lowering.cpp

#include "phonon/lower/Lowering.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/dialect/ExactInteger.h"
#include "spinor/dialect/Classical.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace phonon::lower {

namespace pd = phonon::dialect;
namespace sd = spinor::dialect;

namespace {

struct FuncRange {
  std::uint32_t bodyStart = 0;   // index after the def op
  std::uint32_t bodyEnd = 0;     // index of end_def (exclusive)
  std::uint32_t defId = 0;
  std::vector<pd::ValueId> paramValues;  // results on the def op
  std::vector<pd::Type> paramTypes;
};

struct Lowerer {
  const pd::Module& src;
  sd::Module out;
  sd::Builder b;
  sd::Diagnostics diag;
  const spinor::verify::TargetInfo* target = nullptr;

  // Map from Phonon ValueId -> Spinor ValueId.
  std::unordered_map<std::uint32_t, sd::ValueId> vmap;
  // Compile-time integer values for classical scalars + for-vars.
  std::unordered_map<std::uint32_t, double> ctMap;
  std::unordered_map<std::uint32_t, std::int64_t> exactMap;
  std::unordered_map<std::uint32_t,std::string> runtimeValues;
  bool controller=false;
  std::unordered_map<std::string,std::string> loopOutputs;

  // Function index.
  std::unordered_map<std::string, FuncRange> funcs;
  // For each call we are inlining, push the function name; used to
  // detect recursion.
  std::vector<std::string> callStack;
  std::vector<sd::ValueId>* returnSink = nullptr;
  bool returning = false;
  std::size_t expandedIterations = 0;
  std::size_t nextAnonymousBit = 0;
  std::size_t runtimeDepth = 0;
  std::unordered_map<std::uint32_t, std::size_t> quantumWire;
  std::vector<sd::ValueId> wireValue;

  Lowerer(const pd::Module& m,
          const spinor::verify::TargetInfo* t)
      : src(m), b(out), target(t) {
    out.targetAttr = m.targetAttr;
    out.name = m.name;
    // Reserve source register slots and explicit destinations before assigning
    // anonymous library measurements. A later named readout must not overwrite
    // a bit which an earlier feed-forward predicate still references.
    std::size_t declaredBits = 0;
    for (const auto& op : m.ops()) {
      if (op.kind == pd::OpKind::AllocBit) ++declaredBits;
      if (op.kind != pd::OpKind::Measure) continue;
      for (const auto& a : op.attributes) if (a.name == "clbit") {
        const auto* value = std::get_if<double>(&a.value);
        if (!value || !std::isfinite(*value) || *value < 0 ||
            std::floor(*value) != *value || *value >= 1000000) {
          diag.error("measurement destination must be an integer in [0, 1000000)", op.loc);
          continue;
        }
        nextAnonymousBit = std::max(nextAnonymousBit, static_cast<std::size_t>(*value) + 1);
      }
    }
    nextAnonymousBit = std::max(nextAnonymousBit, declaredBits);
    out.numClbits = nextAnonymousBit;
    for(const auto& op:m.ops())if(op.kind==pd::OpKind::Copy||op.kind==pd::OpKind::ConstUInt||op.kind==pd::OpKind::Output||(op.kind==pd::OpKind::Select&&std::none_of(op.attributes.begin(),op.attributes.end(),[](const auto& attr){return attr.name=="mutable_clbit";})))controller=true;
    if(controller)for(std::size_t bit=0;bit<out.numClbits;++bit){
      out.classicalStorage.push_back({"s"+std::to_string(bit),1,{static_cast<int>(bit)},"exported",true,"0"});
      out.exportedClbits.push_back(static_cast<int>(bit));
    }
    if(controller)for(const auto& op:m.ops())if(op.kind==pd::OpKind::Output){
      std::string name,role;for(const auto& attr:op.attributes){if(attr.name=="name")name=std::get<std::string>(attr.value);if(attr.name=="role")role=std::get<std::string>(attr.value);}
      if(role=="loop_exhausted"&&!loopOutputs.count(name))loopOutputs[name]=emitController(sd::OpKind::CConst,pd::bitType(),{},op.loc,"0");
    }
  }

  std::string newValue(pd::Type type,std::optional<int> existing=std::nullopt,bool initialized=false){
    const auto width=type.kind==pd::TypeKind::UInt?type.width:1u;
    if(width<1||width>64)throw std::invalid_argument("classical width must be in [1,64]");
    sd::ClassicalValue value;value.id="v"+std::to_string(out.classicalValues.size());
    value.type=type.kind==pd::TypeKind::UInt?"uint":"bool";value.width=width;value.visibility="private";value.initialized=initialized;if(initialized)value.initialValue="0";
    if(existing)value.storage={*existing};
    else{
      for(unsigned i=0;i<width;++i)value.storage.push_back(static_cast<int>(out.numClbits++));
      out.classicalStorage.push_back({"s"+std::to_string(out.classicalStorage.size()),width,value.storage,"private",false,{}});
      nextAnonymousBit=out.numClbits;
    }
    out.classicalValues.push_back(value);return value.id;
  }
  const sd::ClassicalValue& valueInfo(const std::string& id)const{
    for(const auto& value:out.classicalValues)if(value.id==id)return value;
    throw std::invalid_argument("unknown classical value: "+id);
  }
  std::string emitController(sd::OpKind kind,pd::Type type,const std::vector<std::string>& inputs,const pd::Location& loc,std::string literal={}){
    const auto result=newValue(type);sd::Op op;op.kind=kind;op.loc=loc;op.attributes.push_back({"result",result});
    for(const auto& input:inputs)op.attributes.push_back({"input",input});
    if(kind==sd::OpKind::CConst)op.attributes.push_back({"value",std::move(literal)});
    out.addOp(std::move(op));return result;
  }
  std::string runtimeValue(pd::ValueId value,std::optional<pd::Type> desired=std::nullopt){
    if(auto found=runtimeValues.find(value.v);found!=runtimeValues.end())return found->second;
    const auto type=desired.value_or(src.typeOf(value));
    if(type.kind==pd::TypeKind::Bit){
      if(auto found=ctMap.find(value.v);found!=ctMap.end()&&(found->second==0||found->second==1))return emitController(sd::OpKind::CConst,type,{},src.op(src.producerOf(value)).loc,found->second==0?"0":"1");
    }
    if(auto found=exactMap.find(value.v);found!=exactMap.end()){
      if(found->second<0||(type.kind!=pd::TypeKind::UInt&&found->second>1))throw std::invalid_argument("runtime int arithmetic requires an explicit uint[width]; bit snapshots contain only 0 or 1");
      if(type.kind==pd::TypeKind::UInt&&static_cast<std::uint64_t>(found->second)>sd::classicalMask(type.width))throw std::invalid_argument("integer literal does not fit controller width");
      return emitController(sd::OpKind::CConst,type,{},src.op(src.producerOf(value)).loc,std::to_string(found->second));
    }
    if(auto found=vmap.find(value.v);found!=vmap.end()&&out.typeOf(found->second)==sd::bitType()){
      const auto result=newValue(pd::bitType(),static_cast<int>(sd::classicalIndex(out,found->second)),true);
      runtimeValues[value.v]=result;return result;
    }
    throw std::invalid_argument("runtime value is undefined or is an unsupported floating-point/int expression");
  }

  sd::ValueId mapValue(pd::ValueId pv) {
    auto it = vmap.find(pv.v);
    if (it == vmap.end()) {
      diag.error("internal: unmapped Phonon value " +
                 std::to_string(pv.v));
      return sd::kInvalidValue;
    }
    return it->second;
  }

  // -- Index pass: find all function definitions ----------------------
  void indexFunctions() {
    for (std::uint32_t i = 0; i < src.numOps(); ++i) {
      pd::OpId id{i};
      const pd::Op& op = src.op(id);
      if (op.kind == pd::OpKind::Def) {
        std::string name;
        for (const auto& a : op.attributes) {
          if (a.name == "name") name = std::get<std::string>(a.value);
        }
        FuncRange fr;
        fr.defId = i;
        fr.bodyStart = i + 1;
        // find matching end_def
        std::uint32_t depth = 1;
        std::uint32_t j = i + 1;
        while (j < src.numOps() && depth > 0) {
          const pd::Op& q = src.op(pd::OpId{j});
          if (q.kind == pd::OpKind::Def) ++depth;
          else if (q.kind == pd::OpKind::EndDef) --depth;
          if (depth == 0) break;
          ++j;
        }
        fr.bodyEnd = j;
        fr.paramValues = op.results;
        for (pd::ValueId v : op.results) fr.paramTypes.push_back(src.typeOf(v));
        funcs[name] = std::move(fr);
        i = j;  // skip past end_def
      }
    }
  }

  // -- Match marker: for `If`, find end_if (handling nested) ----------
  std::uint32_t matchMarker(std::uint32_t startIdx,
                            pd::OpKind beginKind,
                            pd::OpKind endKind) {
    std::uint32_t depth = 1;
    std::uint32_t j = startIdx + 1;
    while (j < src.numOps() && depth > 0) {
      pd::OpKind k = src.op(pd::OpId{j}).kind;
      if (k == beginKind) ++depth;
      else if (k == endKind) --depth;
      if (depth == 0) break;
      ++j;
    }
    return j;
  }

  // -- Emit a single spinor.* op as a copy with mapped operands -------
  void emitSpinorOp(pd::OpId pid) {
    const pd::Op& op = src.op(pid);
    sd::OpKind sk = pd::toSpinorKind(op.kind);
    if (runtimeDepth && sk == sd::OpKind::AllocBit) {
      diag.error("runtime branches cannot allocate classical registers; declare the register before the branch", op.loc);
      return;
    }
    sd::Op sop;
    sop.kind = sk;
    for (pd::ValueId v : op.operands) {
      const auto type = src.typeOf(v).kind;
      if (type == pd::TypeKind::Qubit || type == pd::TypeKind::Bit)
        sop.operands.push_back(mapValue(v));
    }
    if (diag.hasErrors()) return;
    // Copy attributes: only "angle" / "theta" / "phi" carry to spinor.*
    for (const auto& a : op.attributes) {
      if (a.name == "angle" || a.name == "theta" || a.name == "phi" || a.name == "clbit") {
        sop.attributes.push_back(sd::Attribute{a.name, a.value});
      } else if (a.name == "angle_operand" || a.name == "theta_operand" || a.name == "phi_operand") {
        const auto* index = std::get_if<double>(&a.value);
        if (!index || !std::isfinite(*index) || *index < 0 ||
            std::floor(*index) != *index || *index >= op.operands.size()) {
          diag.error("invalid symbolic gate parameter operand", op.loc); return;
        }
        const auto found = ctMap.find(op.operands[static_cast<std::size_t>(*index)].v);
        if (found == ctMap.end() || !std::isfinite(found->second)) {
          diag.error("unbound gate parameter: bind a finite angle before compiling", op.loc); return;
        }
        sop.attributes.push_back({a.name.substr(0, a.name.size() - 8), found->second});
      }
    }
    if (sk == sd::OpKind::Measure &&
        std::none_of(sop.attributes.begin(), sop.attributes.end(),
                     [](const auto& a) { return a.name == "clbit"; })) {
      sop.attributes.push_back({"clbit", static_cast<double>(nextAnonymousBit++)});
    }
    sop.loc = sd::Location{op.loc.file, op.loc.line, op.loc.column};
    sd::OpId sid = out.addOp(std::move(sop));
    sd::Op& live = out.opMut(sid);
    for (const auto& attr : live.attributes) if (attr.name == "clbit") {
      out.numClbits = std::max(out.numClbits, static_cast<std::size_t>(std::get<double>(attr.value)) + 1);
    }
    // Allocate result types matching the Spinor signature, in order.
    int qResults = 0;
    bool producesBit = false;
    if (sk == sd::OpKind::AllocQubit || sk == sd::OpKind::Reset) {
      qResults = 1;
    } else if (sk == sd::OpKind::AllocBit || sk == sd::OpKind::Measure) {
      producesBit = true;
    } else if (sk == sd::OpKind::Cx || sk == sd::OpKind::Cz ||
               sk == sd::OpKind::Swap || sk == sd::OpKind::Ecr ||
               sk == sd::OpKind::Ms || sk == sd::OpKind::Rzz || sk == sd::OpKind::Rxx) {
      qResults = 2;
    } else if (sk == sd::OpKind::Barrier || sk == sd::OpKind::GlobalPhase) {
      qResults = 0;
    } else {
      // single-qubit gates
      qResults = 1;
    }
    std::vector<sd::ValueId> outResults;
    for (int k = 0; k < qResults; ++k) {
      sd::ValueId v = out.addValue(sd::qubitType(), sid);
      outResults.push_back(v);
      live.results.push_back(v);
    }
    if (producesBit) {
      sd::ValueId v = out.addValue(sd::bitType(), sid);
      outResults.push_back(v);
      live.results.push_back(v);
    }
    for (int k = 0; k < qResults; ++k) {
      const auto wire = sk == sd::OpKind::AllocQubit ? wireValue.size()
          : quantumWire.at(live.operands.at(k).v);
      quantumWire[outResults[k].v] = wire;
      if (wire == wireValue.size()) {
        wireValue.push_back(outResults[k]);
        bool fresh=runtimeDepth||!callStack.empty();for(const auto& attr:op.attributes)if(attr.name=="fresh")fresh=true;
        (fresh?out.reservedPool:out.quantumInputs).push_back(static_cast<int>(wire));
      }
      else wireValue[wire] = outResults[k];
    }
    // Map Phonon results to the freshly emitted Spinor results.
    for (std::size_t k = 0; k < op.results.size() && k < outResults.size(); ++k) {
      vmap[op.results[k].v] = outResults[k];
    }
    // Repeated function/loop bodies must consume the latest SSA value for
    // their slot, rather than reuse the value from the first invocation.
    for (std::size_t k = 0; k < op.operands.size() && k < outResults.size(); ++k) {
      if (src.typeOf(op.operands[k]).kind != pd::TypeKind::Qubit ||
          out.typeOf(outResults[k]).kind != sd::TypeKind::Qubit) continue;
      auto previous = live.operands[k];
      for (auto& [_, value] : vmap) if (value == previous) value = outResults[k];
    }
    // Carry value names through (improves output readability).
    for (std::size_t k = 0; k < op.results.size() && k < outResults.size(); ++k) {
      std::string n = src.nameOf(op.results[k]);
      if (!n.empty() && n[0] == '%') n = n.substr(1);
      if (!n.empty() && n.find('?') == std::string::npos) {
        out.setName(outResults[k], n);
      }
    }
    if(controller&&sk==sd::OpKind::Measure){
      const auto bit=static_cast<int>(sd::classicalIndex(out,outResults.at(0)));
      const auto measured=newValue(pd::bitType(),bit);
      out.opMut(sid).attributes.push_back({"result",measured});
      const auto snapshot=emitController(sd::OpKind::CCopy,pd::bitType(),{measured},op.loc);
      for(auto result:op.results)runtimeValues[result.v]=snapshot;
    }
  }

  // The source builders thread mutable register slots through both branches.
  // If a predicate is constant (also possible for a bit compared with -1/2),
  // skipped producers still name those same slots in the retained branch and
  // in following code. Resolve their identities without applying their gates.
  void aliasSkippedRange(std::uint32_t lo, std::uint32_t hi) {
    for (std::uint32_t i = lo; i < hi && !diag.hasErrors(); ++i) {
      const auto& op = src.op(pd::OpId{i});
      if(op.kind==pd::OpKind::AllocQubit){emitSpinorOp(pd::OpId{i});continue;}
      if (op.kind == pd::OpKind::AllocBit) {
        diag.error("conditional branches cannot allocate or redeclare registers; declare the register before the branch", op.loc);
        return;
      }
      if (op.kind == pd::OpKind::Measure) {
        std::optional<std::size_t> destination;
        for (const auto& attr : op.attributes) if (attr.name == "clbit")
          destination = static_cast<std::size_t>(std::get<double>(attr.value));
        if (destination) for (const auto& [_, value] : vmap) {
          if (out.typeOf(value).kind == sd::TypeKind::Bit && sd::classicalIndex(out, value) == *destination) {
            for (auto result : op.results) vmap[result.v] = value;
            break;
          }
        }
        continue;
      }
      if (!pd::isSpinorKind(op.kind) && op.kind != pd::OpKind::Call) continue;
      std::vector<pd::ValueId> operands;
      for (auto operand : op.operands) if (src.typeOf(operand).kind == pd::TypeKind::Qubit)
        operands.push_back(operand);
      std::size_t slot = 0;
      for (auto result : op.results) if (src.typeOf(result).kind == pd::TypeKind::Qubit) {
        if (slot >= operands.size()) {
          diag.error("untaken branch requires unsupported quantum value merging", op.loc); return;
        }
        vmap[result.v] = mapValue(operands[slot++]);
      }
    }
  }

  // -- Emit a range of ops, recursively unrolling control flow --------
  void emitRange(std::uint32_t lo, std::uint32_t hi) {
    std::uint32_t i = lo;
    while (i < hi && !returning && !diag.hasErrors()) {
      pd::OpId pid{i};
      const pd::Op& op = src.op(pid);

      if (pd::isSpinorKind(op.kind)) {
        emitSpinorOp(pid);
        ++i; continue;
      }

      switch (op.kind) {
        case pd::OpKind::ConstUInt:{
          const auto type=src.typeOf(op.results.at(0));std::uint64_t value=0;
          for(const auto& attr:op.attributes)if(attr.name=="value")value=std::get<std::uint64_t>(attr.value);
          if(value>sd::classicalMask(type.width)){diag.error("uint literal does not fit its width",op.loc);return;}
          const auto result=emitController(sd::OpKind::CConst,type,{},op.loc,std::to_string(value));
          for(auto valueId:op.results)runtimeValues[valueId.v]=result;++i;break;
        }
        case pd::OpKind::Copy:{
          const auto type=src.typeOf(op.results.at(0));
          const auto input=runtimeValue(op.operands.at(0),type);
          const auto& info=valueInfo(input);
          const bool same=info.width==(type.kind==pd::TypeKind::UInt?type.width:1)&&info.type==(type.kind==pd::TypeKind::UInt?"uint":"bool");
          const bool complement=std::any_of(op.attributes.begin(),op.attributes.end(),[](const auto& attr){return attr.name=="complement";});
          const auto result=emitController(complement?sd::OpKind::CNot:same?sd::OpKind::CCopy:sd::OpKind::CCast,type,{input},op.loc);
          for(auto valueId:op.results)runtimeValues[valueId.v]=result;++i;break;
        }
        case pd::OpKind::Select:{
          if(!controller){
            for(const auto& attr:op.attributes)if(attr.name=="mutable_clbit"){
              const auto bit=static_cast<std::size_t>(std::get<double>(attr.value));
              for(const auto& [_,mapped]:vmap)if(out.typeOf(mapped)==sd::bitType()&&sd::classicalIndex(out,mapped)==bit){vmap[op.results.at(0).v]=mapped;break;}
            }
            ++i;break;
          }
          const auto type=src.typeOf(op.results.at(0));
          auto condition=ctMap.find(op.operands.at(0).v);
          if(condition!=ctMap.end())runtimeValues[op.results.at(0).v]=runtimeValue(op.operands.at(condition->second?1:2),type);
          else runtimeValues[op.results.at(0).v]=emitController(sd::OpKind::CSelect,type,{runtimeValue(op.operands.at(0),pd::bitType()),runtimeValue(op.operands.at(1),type),runtimeValue(op.operands.at(2),type)},op.loc);
          ++i;break;
        }
        case pd::OpKind::Output:{
          const auto result=runtimeValue(op.operands.at(0));const auto info=valueInfo(result);std::string name,role="value";
          for(const auto& attr:op.attributes){if(attr.name=="name")name=std::get<std::string>(attr.value);if(attr.name=="role")role=std::get<std::string>(attr.value);}
          if(role=="loop_exhausted"){
            // A structured function can be called more than once. Exhaustion
            // is cumulative over those executions, never overwritten by a
            // later successful invocation of the same lexical loop.
            const auto previous=loopOutputs.find(name);
            loopOutputs[name]=previous==loopOutputs.end()?result:
              emitController(sd::OpKind::COr,pd::bitType(),{previous->second,result},op.loc);
          }
          else out.classicalOutputs.push_back({name,result,info.type,info.width,role});++i;break;
        }
        case pd::OpKind::ConstInt: {
          std::optional<std::int64_t> value;
          for (const auto& a : op.attributes) if (a.name == "value") {
            if(const auto* exact=std::get_if<std::int64_t>(&a.value))value=*exact;
            else if(const auto* old=std::get_if<double>(&a.value);old&&std::isfinite(*old)&&std::floor(*old)==*old&&std::abs(*old)<=9007199254740991.0)value=static_cast<std::int64_t>(*old);
          }
          if(!value){diag.error("integer constant requires an exact int64 attribute",op.loc);return;}
          for (pd::ValueId r : op.results) {exactMap[r.v]=*value;ctMap[r.v]=static_cast<double>(*value);}
          ++i; break;
        }
        case pd::OpKind::ConstAngle: {
          double v = 0.0;
          for (const auto& a : op.attributes) if (a.name == "value") v = std::get<double>(a.value);
          for (pd::ValueId r : op.results) ctMap[r.v] = v;
          ++i; break;
        }
        case pd::OpKind::BinOp: {
          if(controller&&(runtimeValues.count(op.operands[0].v)||runtimeValues.count(op.operands[1].v)||src.typeOf(op.results.at(0)).kind==pd::TypeKind::UInt)){
            std::string symbol;for(const auto& attr:op.attributes)if(attr.name=="op")symbol=std::get<std::string>(attr.value);
            auto type=src.typeOf(op.results.at(0));
            if(type.kind==pd::TypeKind::Int){diag.error("runtime arithmetic requires explicit uint[width] values",op.loc);return;}
            if(type.kind==pd::TypeKind::Bit&&symbol!="&"&&symbol!="|"&&symbol!="^"){diag.error("Boolean arithmetic requires an explicit uint cast; bool supports only &, | and ^",op.loc);return;}
            sd::OpKind kind;
            if(symbol=="+")kind=sd::OpKind::CAdd;else if(symbol=="-")kind=sd::OpKind::CSub;
            else if(symbol=="&")kind=sd::OpKind::CAnd;else if(symbol=="|")kind=sd::OpKind::COr;else if(symbol=="^")kind=sd::OpKind::CXor;
            else if(symbol=="<<")kind=sd::OpKind::CShl;else if(symbol==">>")kind=sd::OpKind::CShr;
            else{diag.error("runtime controller operator is unsupported: "+symbol,op.loc);return;}
            if(kind==sd::OpKind::CShl||kind==sd::OpKind::CShr){
              std::optional<std::uint64_t> shift;const auto& producer=src.op(src.producerOf(op.operands[1]));
              if(auto exact=exactMap.find(op.operands[1].v);exact!=exactMap.end()&&exact->second>=0)shift=static_cast<std::uint64_t>(exact->second);
              if(producer.kind==pd::OpKind::ConstUInt)for(const auto& attr:producer.attributes)if(attr.name=="value")shift=std::get<std::uint64_t>(attr.value);
              if(!shift||*shift>=type.width){diag.error("shift count must be a constant in [0, width)",op.loc);return;}
            }
            const auto left=runtimeValue(op.operands[0],type),right=runtimeValue(op.operands[1],type);
            if(valueInfo(left).type!=valueInfo(right).type||valueInfo(left).width!=valueInfo(right).width){diag.error("runtime arithmetic operands require identical types and widths; cast explicitly",op.loc);return;}
            runtimeValues[op.results.at(0).v]=emitController(kind,type,{left,right},op.loc);++i;break;
          }
          const auto ai=exactMap.find(op.operands[0].v),bi=exactMap.find(op.operands[1].v);
          if(ai!=exactMap.end()&&bi!=exactMap.end()){
            std::string symbol;for(const auto& attr:op.attributes)if(attr.name=="op")symbol=std::get<std::string>(attr.value);
            try{
              if(symbol!="/"|| !bi->second || (ai->second==std::numeric_limits<std::int64_t>::min()&&bi->second==-1)||ai->second%bi->second==0){
                const auto value=sd::checkedInteger(symbol,ai->second,bi->second);
                for(auto result:op.results){exactMap[result.v]=value;ctMap[result.v]=static_cast<double>(value);}++i;break;
              }
            }catch(const std::exception& error){diag.error(error.what(),op.loc);return;}
          }
          // Evaluate symbolically if both operands have ct values.
          auto a = ctMap.find(op.operands[0].v);
          auto b_ = ctMap.find(op.operands[1].v);
          if (a != ctMap.end() && b_ != ctMap.end()) {
            std::string opn;
            for (const auto& at : op.attributes) if (at.name == "op") opn = std::get<std::string>(at.value);
            double r = 0.0;
            if      (opn == "+") r = a->second + b_->second;
            else if (opn == "-") r = a->second - b_->second;
            else if (opn == "*") r = a->second * b_->second;
            else if (opn == "/" && b_->second != 0) r = a->second / b_->second;
            else { diag.error("unsupported or invalid compile-time arithmetic"); break; }
            if (!std::isfinite(r)) { diag.error("compile-time arithmetic must remain finite", op.loc); return; }
            for (pd::ValueId rv : op.results) ctMap[rv.v] = r;
          }
          ++i; break;
        }
        case pd::OpKind::Cmp: {
          if(controller&&(runtimeValues.count(op.operands[0].v)||runtimeValues.count(op.operands[1].v)||src.typeOf(op.operands[0]).kind==pd::TypeKind::Bit||src.typeOf(op.operands[1]).kind==pd::TypeKind::Bit)){
            std::string symbol;for(const auto& attr:op.attributes)if(attr.name=="op")symbol=std::get<std::string>(attr.value);
            if(symbol!="=="&&symbol!="!="&&symbol!="<"&&symbol!="<="&&symbol!=">"&&symbol!=">="){
              diag.error("unsupported comparison predicate: "+symbol,op.loc);return;
            }
            // A measured bit has the exact domain {0,1}; preserve comparisons
            // with arbitrary finite constants without coercing the constant
            // into a one-bit integer (which would truncate or reject it).
            bool reduced=false;
            for(unsigned side=0;side<2&&!reduced;++side){
              const auto bit=op.operands[side],constant=op.operands[1-side];
              const auto known=ctMap.find(constant.v);const auto runtime=runtimeValues.find(bit.v);
              const bool isBit=src.typeOf(bit).kind==pd::TypeKind::Bit||(runtime!=runtimeValues.end()&&valueInfo(runtime->second).type=="bool");
              if(!isBit||known==ctMap.end())continue;
              auto truth=[&](double value){const double a=side?known->second:value,b=side?value:known->second;
                if(symbol=="==")return a==b;if(symbol=="!=")return a!=b;if(symbol=="<")return a<b;
                if(symbol=="<=")return a<=b;if(symbol==">")return a>b;if(symbol==">=")return a>=b;
                throw std::invalid_argument("unsupported comparison predicate");};
              const bool zero=truth(0),one=truth(1);
              runtimeValues[op.results.at(0).v]=zero==one?emitController(sd::OpKind::CConst,pd::bitType(),{},op.loc,zero?"1":"0"):
                emitController(one?sd::OpKind::CCopy:sd::OpKind::CNot,pd::bitType(),{runtimeValue(bit,pd::bitType())},op.loc);
              reduced=true;
            }
            if(reduced){++i;break;}
            auto type=src.typeOf(op.operands[0]);if(type.kind==pd::TypeKind::Int)type=src.typeOf(op.operands[1]);
            sd::OpKind kind=symbol=="=="?sd::OpKind::CEq:symbol=="!="?sd::OpKind::CNe:symbol=="<"?sd::OpKind::CLt:symbol=="<="?sd::OpKind::CLe:symbol==">"?sd::OpKind::CGt:sd::OpKind::CGe;
            const auto left=runtimeValue(op.operands[0],type),right=runtimeValue(op.operands[1],type);
            if(valueInfo(left).type!=valueInfo(right).type||valueInfo(left).width!=valueInfo(right).width){diag.error("runtime comparisons require identical types and widths; cast explicitly",op.loc);return;}
            runtimeValues[op.results.at(0).v]=emitController(kind,pd::bitType(),{left,right},op.loc);++i;break;
          }
          auto lhs = ctMap.find(op.operands.at(0).v);
          auto rhs = ctMap.find(op.operands.at(1).v);
          if (lhs != ctMap.end() && rhs != ctMap.end()) {
            std::string predicate;
            for (const auto& attr : op.attributes)
              if (attr.name == "op") predicate = std::get<std::string>(attr.value);
            const double a = lhs->second, b = rhs->second;
            bool result;
            const auto ai=exactMap.find(op.operands[0].v),bi=exactMap.find(op.operands[1].v);
            if(ai!=exactMap.end()&&bi!=exactMap.end()) {
              const auto x=ai->second,y=bi->second;
              if(predicate=="==")result=x==y;else if(predicate=="!=")result=x!=y;
              else if(predicate=="<")result=x<y;else if(predicate==">")result=x>y;
              else if(predicate=="<=")result=x<=y;else if(predicate==">=")result=x>=y;
              else{diag.error("unsupported comparison predicate",op.loc);return;}
            }
            else if (predicate == "==") result = a == b;
            else if (predicate == "!=") result = a != b;
            else if (predicate == "<") result = a < b;
            else if (predicate == ">") result = a > b;
            else if (predicate == "<=") result = a <= b;
            else if (predicate == ">=") result = a >= b;
            else { diag.error("unsupported comparison predicate"); break; }
            for (auto value : op.results) ctMap[value.v] = result ? 1 : 0;
          }
          ++i; break;
        }
        case pd::OpKind::Assign: {
          if (op.operands.size() == 1 && src.typeOf(op.operands[0]).kind == pd::TypeKind::Bit) {
            diag.error("copying measured data into a scalar is unsupported; use a separate measurement destination", op.loc);
            return;
          }
          // Re-bind classical name; if rhs has ct value, propagate.
          if (op.operands.size() == 1) {
            auto it = ctMap.find(op.operands[0].v);
            if (it != ctMap.end()) ctMap[op.operands[0].v] = it->second;
          }
          ++i; break;
        }

        case pd::OpKind::Def: {
          // Skip the entire def block; it has been indexed in pass 1.
          std::string name;
          for (const auto& a : op.attributes) if (a.name == "name") name = std::get<std::string>(a.value);
          auto it = funcs.find(name);
          std::uint32_t skipTo = (it != funcs.end()) ? it->second.bodyEnd + 1 : i + 1;
          i = skipTo;
          break;
        }
        case pd::OpKind::EndDef:
          ++i; break;

        case pd::OpKind::Call: {
          std::string name;
          for (const auto& a : op.attributes) if (a.name == "name") name = std::get<std::string>(a.value);
          auto it = funcs.find(name);
          if (it == funcs.end()) {
            diag.error("call to unknown function: " + name);
            ++i; break;
          }
          // Recursion check.
          if (std::find(callStack.begin(), callStack.end(), name) != callStack.end()) {
            diag.error("recursive call to '" + name + "' is not supported");
            return;
          }
          // Bind parameter values via vmap (qubit args) and ctMap
          // (classical args).
          const FuncRange& fr = it->second;
          if (fr.paramValues.size() != op.operands.size()) {
            diag.error("argument count mismatch for function '" + name + "'");
            return;
          }
          // Save vmap entries for params so we can restore after inlining.
          std::vector<std::pair<std::uint32_t, std::optional<sd::ValueId>>> savedV;
          std::vector<std::size_t> argumentWires;
          auto savedCt = ctMap;
          auto savedExact = exactMap;
          auto savedRuntime = runtimeValues;
          for (std::size_t k = 0; k < fr.paramValues.size() &&
                                   k < op.operands.size(); ++k) {
            std::uint32_t pv = fr.paramValues[k].v;
            runtimeValues.erase(pv);
            auto sv = vmap.find(pv);
            savedV.push_back({pv, sv != vmap.end() ? std::optional<sd::ValueId>{sv->second} : std::nullopt});
            if (fr.paramTypes[k].kind == pd::TypeKind::Int ||
                fr.paramTypes[k].kind == pd::TypeKind::Angle) {
              auto value = ctMap.find(op.operands[k].v);
              if (value == ctMap.end()) { diag.error("unbound classical function argument"); return; }
              if (!std::isfinite(value->second) ||
                  (fr.paramTypes[k].kind == pd::TypeKind::Int && std::floor(value->second) != value->second)) {
                diag.error("function argument must match its finite numeric parameter type", op.loc); return;
              }
              ctMap[pv] = value->second;
              if(auto exact=exactMap.find(op.operands[k].v);exact!=exactMap.end())exactMap[pv]=exact->second;
              else exactMap.erase(pv);
            } else {
              vmap[pv] = mapValue(op.operands[k]);
              if(fr.paramTypes[k].kind==pd::TypeKind::Bit){
                if(auto value=savedRuntime.find(op.operands[k].v);value!=savedRuntime.end())runtimeValues[pv]=value->second;
              }
              if (fr.paramTypes[k].kind == pd::TypeKind::Qubit)
                argumentWires.push_back(quantumWire.at(vmap[pv].v));
            }
          }
          // Inline the body. Track "return values" produced by a
          // phonon.return inside.
          std::vector<sd::ValueId> returnValues;
          callStack.push_back(name);
          auto oldSink = returnSink;
          returnSink = &returnValues;
          {
            emitRange(fr.bodyStart, fr.bodyEnd);
            if (!returning) {
              // Auto-return: the latest vmap binding of each qubit
              // parameter is the function's result.
              for (std::size_t k = 0; k < fr.paramValues.size(); ++k) {
                if (fr.paramTypes[k].kind == pd::TypeKind::Qubit) {
                  auto vmit = vmap.find(fr.paramValues[k].v);
                  if (vmit != vmap.end()) returnValues.push_back(vmit->second);
                }
              }
            }
          }
          returnSink = oldSink;
          returning = false;
          callStack.pop_back();
          ctMap = std::move(savedCt);
          exactMap = std::move(savedExact);
          runtimeValues = std::move(savedRuntime);
          std::vector<std::size_t> returnedWires;
          for (auto value : returnValues) {
            if (out.typeOf(value).kind != sd::TypeKind::Qubit) {
              diag.error("function must return qubit values: " + name, op.loc); return;
            }
            const auto wire = quantumWire.at(value.v);
            if (std::find(returnedWires.begin(), returnedWires.end(), wire) != returnedWires.end()) {
              diag.error("function cannot return duplicate qubit aliases: " + name, op.loc); return;
            }
            returnedWires.push_back(wire);
          }
          if (runtimeDepth && returnValues.size() == argumentWires.size()) {
            // Represent conditional return aliases as actual state transfers,
            // leaving caller slot identities invariant across the branch join.
            auto locations=argumentWires;
            for(auto wire:returnedWires)if(std::find(locations.begin(),locations.end(),wire)==locations.end())locations.push_back(wire);
            auto stateAt=locations;
            for (std::size_t k = 0; k < argumentWires.size(); ++k) {
              auto found = std::find(stateAt.begin() + k, stateAt.end(), returnedWires[k]);
              const auto j = static_cast<std::size_t>(found - stateAt.begin());
              if (k == j) continue;
              const auto a = locations[k], b_ = locations[j];
              auto swapped = b.swap(wireValue[a], wireValue[b_], op.loc);
              quantumWire[swapped.first.v] = a; quantumWire[swapped.second.v] = b_;
              wireValue[a] = swapped.first; wireValue[b_] = swapped.second;
              for (auto& [_, value] : vmap) if (out.typeOf(value).kind == sd::TypeKind::Qubit) {
                const auto wire = quantumWire.at(value.v);
                if (wire == a) value = swapped.first;
                else if (wire == b_) value = swapped.second;
              }
              std::swap(stateAt[k], stateAt[j]);
            }
            returnValues.clear();
            for (auto wire : argumentWires) returnValues.push_back(wireValue[wire]);
          }
          // Bind call results to the returned values, in order
          // (qubit results only).
          std::size_t rk = 0;
          for (pd::ValueId rv : op.results) {
            if (rk < returnValues.size()) {
              vmap[rv.v] = returnValues[rk++];
            }
          }
          if (rk != op.results.size()) diag.error("function returned the wrong number of values: " + name);
          // Restore param vmap entries.
          for (const auto& p : savedV) {
            if (p.second) vmap[p.first] = *p.second;
            else vmap.erase(p.first);
          }
          ++i; break;
        }

        case pd::OpKind::For: {
          // Operands: lo (int value), hi (int value).
          double lo = 0.0, hi = 0.0;
          if (op.operands.size() >= 2) {
            auto a = ctMap.find(op.operands[0].v);
            auto b_ = ctMap.find(op.operands[1].v);
            if (a == ctMap.end() || b_ == ctMap.end()) {
              diag.error("for-loop bounds must be compile-time integers");
              ++i; break;
            }
            lo = a->second; hi = b_->second;
          }
          std::string var;
          for (const auto& a : op.attributes) if (a.name == "var") var = std::get<std::string>(a.value);
          std::uint32_t bodyStart = i + 1;
          std::uint32_t bodyEnd   = matchMarker(i, pd::OpKind::For,
                                                 pd::OpKind::EndFor);
          if (!std::isfinite(lo) || !std::isfinite(hi) ||
              std::floor(lo) != lo || std::floor(hi) != hi || hi - lo > 100000 ||
              std::abs(lo) > 9007199254740991.0 || std::abs(hi) > 9007199254740991.0) {
            diag.error("for-loop requires finite integer bounds and at most 100000 iterations");
            return;
          }
          auto loInt = static_cast<std::int64_t>(lo);
          auto hiInt = static_cast<std::int64_t>(hi);
          for (auto v = loInt; v < hiInt && !returning; ++v) {
            // Source-level induction indices were resolved before SSA construction.
            // A programmatically built For repeats its fixed body while the
            // value mapping threads each slot's latest value between iterations.
            (void)v;
            if (++expandedIterations > 100000) { diag.error("static loop expansion exceeds 100000 iterations"); return; }
            emitRange(bodyStart, bodyEnd);
          }
          i = bodyEnd + 1;  // skip end_for
          break;
        }
        case pd::OpKind::EndFor:
          ++i; break;

        case pd::OpKind::If: {
          std::uint32_t bodyStart = i + 1;
          std::uint32_t bodyEnd   = matchMarker(i, pd::OpKind::If,
                                                 pd::OpKind::EndIf);
          // Find then_count / else_count attrs to split.
          std::uint32_t thenCount = bodyEnd - bodyStart;
          std::uint32_t elseCount = 0;
          for (const auto& a : op.attributes) {
            if (a.name == "then_count") thenCount = static_cast<std::uint32_t>(std::get<double>(a.value));
            if (a.name == "else_count") elseCount = static_cast<std::uint32_t>(std::get<double>(a.value));
          }
          auto condition = op.operands.empty() ? ctMap.end() : ctMap.find(op.operands.front().v);
          if(controller&&runtimeValues.count(op.operands.at(0).v)){
            const auto predicate=runtimeValues.at(op.operands[0].v);const auto info=valueInfo(predicate);
            if(info.width!=1){diag.error("if predicate must be Boolean; compare uint values explicitly",op.loc);return;}
            const auto marker=out.numOps();b.beginIf(info.storage.at(0),true,op.loc);
            out.opMut(sd::OpId{static_cast<std::uint32_t>(marker)}).attributes.push_back({"condition",predicate});
            const auto outputsBefore=loopOutputs;
            ++runtimeDepth;emitRange(bodyStart,bodyStart+thenCount);
            if(returning){diag.error("raw conditional return markers require source return normalization before lowering",op.loc);return;}
            const auto thenOutputs=loopOutputs;loopOutputs=outputsBefore;
            if(elseCount){b.elseBranch(op.loc);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);
              if(returning){diag.error("raw conditional return markers require source return normalization before lowering",op.loc);return;}}
            --runtimeDepth;b.endIf(op.loc);
            for(const auto& [name,before]:outputsBefore){
              const auto yes=thenOutputs.at(name),no=loopOutputs.at(name);
              if(yes!=no)loopOutputs[name]=emitController(sd::OpKind::CSelect,pd::bitType(),{predicate,yes,no},op.loc);
            }
          } else if (condition == ctMap.end()) {
            auto predicate=op.operands.front();
            const auto& cmp=src.op(src.producerOf(predicate));
            pd::ValueId bit=predicate;double constant=1;std::string comparison="==";bool reverse=false;
            if(cmp.kind==pd::OpKind::Cmp){
              for(const auto& attr:cmp.attributes)if(attr.name=="op")comparison=std::get<std::string>(attr.value);
              auto left=ctMap.find(cmp.operands[0].v),right=ctMap.find(cmp.operands[1].v);
              if(right!=ctMap.end()){bit=cmp.operands[0];constant=right->second;}
              else if(left!=ctMap.end()){bit=cmp.operands[1];constant=left->second;reverse=true;}
              else {diag.error("runtime comparison requires one measured bit and one compile-time value",op.loc);return;}
            }
            auto mapped=vmap.find(bit.v);
            if(mapped==vmap.end()||out.typeOf(mapped->second)!=sd::bitType()){
              diag.error("runtime predicate is not a measured classical bit",op.loc);return;
            }
            auto evaluate=[&](double value){
              double lhs=reverse?constant:value,rhs=reverse?value:constant;
              if(comparison=="==")return lhs==rhs;if(comparison=="!=")return lhs!=rhs;
              if(comparison=="<")return lhs<rhs;if(comparison==">")return lhs>rhs;
              if(comparison=="<=")return lhs<=rhs;if(comparison==">=")return lhs>=rhs;
              throw std::runtime_error("unsupported classical predicate");
            };
            bool zero=evaluate(0),one=evaluate(1);
            if(zero==one){
              if(one){emitRange(bodyStart,bodyStart+thenCount);aliasSkippedRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
              else{aliasSkippedRange(bodyStart,bodyStart+thenCount);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
            }
            else {
              b.beginIf(sd::classicalIndex(out,mapped->second),one,op.loc);
              ++runtimeDepth;
              emitRange(bodyStart,bodyStart+thenCount);
              if(returning){diag.error("conditional return requires explicit control-flow return lowering",op.loc);return;}
              if(elseCount){b.elseBranch(op.loc);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
              if(returning){diag.error("conditional return requires explicit control-flow return lowering",op.loc);return;}
              --runtimeDepth;
              b.endIf(op.loc);
            }
          } else if (condition->second != 0) {
            emitRange(bodyStart, bodyStart + thenCount);
            aliasSkippedRange(bodyStart + thenCount, bodyStart + thenCount + elseCount);
          } else {
            aliasSkippedRange(bodyStart, bodyStart + thenCount);
            emitRange(bodyStart + thenCount, bodyStart + thenCount + elseCount);
          }
          i = bodyEnd + 1;
          break;
        }
        case pd::OpKind::EndIf:
          ++i; break;

        case pd::OpKind::While:
        case pd::OpKind::EndWhile: {
          diag.error("phonon.while is not supported by lowering "
                     "(use a bounded for-loop)");
          ++i; break;
        }

        case pd::OpKind::Return:
          if (!returnSink) {
            diag.error("return outside an inlined function is unsupported");
            return;
          }
          for (auto value : op.operands) returnSink->push_back(mapValue(value));
          returning = true;
          return;

        default:
          ++i; break;
      }
    }
  }
};

}  // namespace

Result lower(const pd::Module& m,
             const spinor::verify::TargetInfo* target) {
  Lowerer lo(m, target);
  lo.indexFunctions();
  try{lo.emitRange(0, m.numOps());}
  catch(const std::exception& error){lo.diag.error(std::string("classical lowering: ")+error.what());}
  std::vector<std::string> outputNames;for(const auto& [name,_]:lo.loopOutputs)outputNames.push_back(name);std::sort(outputNames.begin(),outputNames.end());
  for(const auto& name:outputNames)lo.out.classicalOutputs.push_back({name,lo.loopOutputs.at(name),"bool",1,"loop_exhausted"});
  Result r;
  r.diag = std::move(lo.diag);
  if (!r.diag.hasErrors()) r.module = std::move(lo.out);
  return r;
}

}  // namespace phonon::lower

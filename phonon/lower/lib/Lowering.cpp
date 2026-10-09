// phonon/lower/lib/Lowering.cpp

#include "phonon/lower/Lowering.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/dialect/ExactInteger.h"
#include "spinor/dialect/Classical.h"

#include <cmath>
#include <charconv>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
  bool structuredReturns = false;
  bool explicitResults = false;
  std::vector<pd::Type> declaredResults;
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
  bool transferring = false;
  struct FlowScope {
    bool function = false;
    bool mayTransfer = false;
    std::string done, breaking;
    std::vector<pd::Type> types;
    std::vector<std::string> values;
    std::vector<std::size_t> wires;
  };
  std::vector<FlowScope> flow;
  std::size_t expandedIterations = 0;
  std::size_t operationBudget = 100000;
  std::size_t nextAnonymousBit = 0;
  std::size_t runtimeDepth = 0;
  std::unordered_map<std::uint32_t, std::size_t> quantumWire;
  std::vector<sd::ValueId> wireValue;

  Lowerer(const pd::Module& m,
          const spinor::verify::TargetInfo* t)
      : src(m), b(out), target(t) {
    if(const char* budget=std::getenv("QSTACK_EXPANDED_OPERATION_BUDGET"))try{
      operationBudget=sd::parseExactInteger<std::size_t>(budget);
      if(!operationBudget)throw std::invalid_argument("budget must be positive");
    }catch(const std::exception& error){diag.error(std::string("invalid QSTACK_EXPANDED_OPERATION_BUDGET: ")+error.what());}
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
    std::size_t controlDepth=0;
    for(const auto& op:m.ops()){
      if(op.kind==pd::OpKind::If||op.kind==pd::OpKind::For||op.kind==pd::OpKind::LoopBody)++controlDepth;
      if(op.kind==pd::OpKind::Return&&(controlDepth||std::any_of(op.operands.begin(),op.operands.end(),[&](auto v){return m.typeOf(v).kind!=pd::TypeKind::Qubit;})))controller=true;
      if(op.kind==pd::OpKind::EndIf||op.kind==pd::OpKind::EndFor||op.kind==pd::OpKind::EndLoopBody){if(controlDepth)--controlDepth;}
    }
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
    if(out.numOps()>=operationBudget)throw std::invalid_argument("expanded operation budget exceeded after function/control-flow lowering; raise QSTACK_EXPANDED_OPERATION_BUDGET or reduce the program");
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
        for(const auto& attr:op.attributes){
          if(attr.name=="explicit_results")fr.explicitResults=true;
          if(attr.name=="result_type"){
            const auto& type=std::get<std::string>(attr.value);
            if(type==pd::typeName(pd::qubitType()))fr.declaredResults.push_back(pd::qubitType());
            else if(type==pd::typeName(pd::bitType()))fr.declaredResults.push_back(pd::bitType());
            else if(type==pd::typeName(pd::uintType(0)))fr.declaredResults.push_back(pd::uintType(0));
            else if(type==pd::typeName(pd::intType()))fr.declaredResults.push_back(pd::intType());
            else if(type==pd::typeName(pd::angleType()))fr.declaredResults.push_back(pd::angleType());
            else diag.error("unsupported function result type",op.loc);
          }
          if(attr.name=="result_width"&&!fr.declaredResults.empty())fr.declaredResults.back().width=static_cast<std::uint32_t>(std::get<std::uint64_t>(attr.value));
        }
        fr.structuredReturns=fr.explicitResults;
        for (pd::ValueId v : op.results) fr.paramTypes.push_back(src.typeOf(v));
        std::size_t controls=0;
        for(auto k=fr.bodyStart;k<fr.bodyEnd;++k){const auto& body=src.op(pd::OpId{k});
          if(body.kind==pd::OpKind::If||body.kind==pd::OpKind::For||body.kind==pd::OpKind::LoopBody)++controls;
          if(body.kind==pd::OpKind::Return&&(controls||std::any_of(body.operands.begin(),body.operands.end(),[&](auto v){return src.typeOf(v).kind!=pd::TypeKind::Qubit;})))fr.structuredReturns=true;
          if(body.kind==pd::OpKind::EndIf||body.kind==pd::OpKind::EndFor||body.kind==pd::OpKind::EndLoopBody){if(controls)--controls;}
        }
        funcs[name] = std::move(fr);
        i = j;  // skip past end_def
      }
    }
  }

  std::string boolean(bool value,const pd::Location& loc={}){
    return emitController(sd::OpKind::CConst,pd::bitType(),{},loc,value?"1":"0");
  }
  void bindInteger(std::uint32_t id,std::int64_t value){
    exactMap[id]=value;
    // The legacy numeric cache is used for angles and small constants only.
    // Wide integer data never travels through binary64 storage.
    if(value>=-9007199254740991LL&&value<=9007199254740991LL)ctMap[id]=static_cast<double>(value);
    else ctMap.erase(id);
  }
  std::string choose(const std::string& predicate,const std::string& yes,const std::string& no,pd::Type type,const pd::Location& loc){
    if(type.kind==pd::TypeKind::Int||type.kind==pd::TypeKind::Angle){
      if(yes=="#unset")return no;if(no=="#unset")return yes;if(yes==no)return yes;
      const auto bitValue=[&](const std::string& value)->std::string{
        if(value=="#i:0"||value=="#i:1")return boolean(value.back()=='1',loc);
        if(!value.empty()&&value[0]!='#'&&valueInfo(value).type=="bool")return value;
        throw std::invalid_argument("runtime-dependent int/angle function results require explicit uint[width]; int snapshots contain only 0 or 1");
      };
      if(type.kind==pd::TypeKind::Angle)throw std::invalid_argument("runtime-dependent angle function results are unsupported; gate angles must be compile-time");
      return emitController(sd::OpKind::CSelect,pd::bitType(),{predicate,bitValue(yes),bitValue(no)},loc);
    }
    return yes==no?yes:emitController(sd::OpKind::CSelect,type,{predicate,yes,no},loc);
  }
  void joinFlow(const std::string& predicate,const std::vector<FlowScope>& yes,const std::vector<FlowScope>& no,const pd::Location& loc){
    flow=no;
    for(std::size_t s=0;s<flow.size();++s){
      flow[s].done=choose(predicate,yes[s].done,no[s].done,pd::bitType(),loc);
      flow[s].mayTransfer=yes[s].mayTransfer||no[s].mayTransfer;
      if(!flow[s].function)flow[s].breaking=choose(predicate,yes[s].breaking,no[s].breaking,pd::bitType(),loc);
      for(std::size_t k=0;k<flow[s].values.size();++k)if(flow[s].types[k].kind!=pd::TypeKind::Qubit)
        flow[s].values[k]=choose(predicate,yes[s].values[k],no[s].values[k],flow[s].types[k],loc);
    }
  }
  void joinOutputs(const std::string& predicate,const std::unordered_map<std::string,std::string>& yes,const std::unordered_map<std::string,std::string>& no,const pd::Location& loc){
    loopOutputs=no;
    for(const auto& [name,value]:yes)loopOutputs[name]=choose(predicate,value,no.at(name),pd::bitType(),loc);
  }
  void beginControllerIf(const std::string& predicate,const pd::Location& loc){
    const auto marker=out.numOps();const auto bit=valueInfo(predicate).storage.at(0);
    b.beginIf(bit,true,loc);out.opMut(sd::OpId{static_cast<std::uint32_t>(marker)}).attributes.push_back({"condition",predicate});
    ++runtimeDepth;
  }
  // Guard the continuation, not the transfer itself. A fresh transfer inside
  // this continuation creates another guard at its own lexical join.
  bool guardContinuation(std::uint32_t lo,std::uint32_t hi){
    std::optional<std::string> predicate;
    const auto loc=src.op(pd::OpId{lo}).loc;
    for(const auto& scope:flow)if(scope.mayTransfer){
      auto active=emitController(sd::OpKind::CNot,pd::bitType(),{scope.done},loc);
      predicate=predicate?emitController(sd::OpKind::CAnd,pd::bitType(),{*predicate,active},loc):active;
    }
    if(!predicate)return false;
    const auto before=flow;const auto outputsBefore=loopOutputs;
    for(auto& scope:flow)scope.mayTransfer=false;
    beginControllerIf(*predicate,loc);emitRange(lo,hi);--runtimeDepth;b.endIf(loc);
    const auto yes=flow;const auto yesOutputs=loopOutputs;
    joinFlow(*predicate,yes,before,loc);joinOutputs(*predicate,yesOutputs,outputsBefore,loc);
    returning=false;transferring=false;return true;
  }
  bool allPathsReturn(std::uint32_t lo,std::uint32_t hi){
    for(auto i=lo;i<hi;++i){const auto& op=src.op(pd::OpId{i});
      if(op.kind==pd::OpKind::Return)return true;
      if(op.kind==pd::OpKind::If){
        const auto end=matchMarker(i,pd::OpKind::If,pd::OpKind::EndIf);auto count=end-i-1;std::uint32_t other=0;
        for(const auto& a:op.attributes){if(a.name=="then_count")count=static_cast<std::uint32_t>(std::get<double>(a.value));if(a.name=="else_count")other=static_cast<std::uint32_t>(std::get<double>(a.value));}
        if(other&&allPathsReturn(i+1,i+1+count)&&allPathsReturn(i+1+count,i+1+count+other))return true;i=end;
      }else if(op.kind==pd::OpKind::For)i=matchMarker(i,pd::OpKind::For,pd::OpKind::EndFor);
      else if(op.kind==pd::OpKind::LoopBody)i=matchMarker(i,pd::OpKind::LoopBody,pd::OpKind::EndLoopBody);
    }return false;
  }
  void transferQuantum(const std::vector<std::size_t>& destinations,const std::vector<std::size_t>& returned,const pd::Location& loc){
    auto locations=destinations;
    for(auto wire:returned)if(std::find(locations.begin(),locations.end(),wire)==locations.end())locations.push_back(wire);
    auto stateAt=locations;
    for(std::size_t k=0;k<destinations.size();++k){
      const auto found=std::find(stateAt.begin()+k,stateAt.end(),returned[k]);
      if(found==stateAt.end())throw std::invalid_argument("duplicate quantum return violates unique ownership");
      const auto j=static_cast<std::size_t>(found-stateAt.begin());if(k==j)continue;
      const auto a=locations[k],c=locations[j];const auto swap=b.swap(wireValue[a],wireValue[c],loc);
      quantumWire[swap.first.v]=a;quantumWire[swap.second.v]=c;wireValue[a]=swap.first;wireValue[c]=swap.second;
      for(auto& [_,value]:vmap)if(out.typeOf(value).kind==sd::TypeKind::Qubit){const auto wire=quantumWire.at(value.v);if(wire==a)value=swap.first;else if(wire==c)value=swap.second;}
      std::swap(stateAt[k],stateAt[j]);
    }
  }
  void emitTypedCall(const pd::Op& op,const FuncRange& fr,const std::string& name){
    if(fr.explicitResults){
      if(fr.declaredResults.size()!=op.results.size()){diag.error("call result arity differs from function signature: "+name,op.loc);return;}
      for(std::size_t k=0;k<op.results.size();++k)if(fr.declaredResults[k]!=src.typeOf(op.results[k])){diag.error("call result type or width differs from function signature: "+name,op.loc);return;}
    }
    FlowScope scope;scope.function=true;scope.done=boolean(false,op.loc);
    std::vector<std::size_t> arguments;
    for(std::size_t k=0;k<op.operands.size();++k)if(fr.paramTypes[k].kind==pd::TypeKind::Qubit){
      const auto wire=quantumWire.at(mapValue(op.operands[k]).v);
      if(std::find(arguments.begin(),arguments.end(),wire)!=arguments.end()){diag.error("function qubit arguments must refer to distinct slots",op.loc);return;}arguments.push_back(wire);
    }
    const auto quantumArguments=arguments.size();std::size_t quantum=0;bool classical=false;
    for(auto result:op.results){const auto type=src.typeOf(result);scope.types.push_back(type);
      if(type.kind==pd::TypeKind::Qubit){
        if(quantum>=arguments.size()){
          const auto fresh=b.allocQubit(op.loc);const auto wire=wireValue.size();quantumWire[fresh.v]=wire;wireValue.push_back(fresh);out.reservedPool.push_back(static_cast<int>(wire));arguments.push_back(wire);
        }scope.wires.push_back(arguments[quantum++]);scope.values.push_back({});
      }else if(type.kind==pd::TypeKind::Bit||type.kind==pd::TypeKind::UInt){classical=true;scope.values.push_back(emitController(sd::OpKind::CConst,type,{},op.loc,"0"));}
      else if(type.kind==pd::TypeKind::Int||type.kind==pd::TypeKind::Angle){classical=true;scope.values.push_back("#unset");}
      else{diag.error("function result type is not supported",op.loc);return;}
    }
    if((classical||fr.explicitResults||quantum!=quantumArguments)&&!allPathsReturn(fr.bodyStart,fr.bodyEnd)){diag.error("typed function must return its fixed results on every path, including bounded-loop exhaustion: "+name,op.loc);return;}
    for(auto i=fr.bodyStart;i<fr.bodyEnd;++i){const auto& ret=src.op(pd::OpId{i});if(ret.kind!=pd::OpKind::Return)continue;
      if(ret.operands.size()!=scope.types.size()){diag.error("function return arity differs between paths: "+name,ret.loc);return;}
      for(std::size_t k=0;k<ret.operands.size();++k)if(src.typeOf(ret.operands[k])!=scope.types[k]){diag.error("function return type or width differs from declared call results: "+name,ret.loc);return;}
    }
    const auto savedCt=ctMap;const auto savedExact=exactMap;const auto savedRuntime=runtimeValues;const auto savedV=vmap;
    for(std::size_t k=0;k<fr.paramValues.size();++k){const auto parameter=fr.paramValues[k],arg=op.operands[k];const auto type=fr.paramTypes[k];
      ctMap.erase(parameter.v);exactMap.erase(parameter.v);runtimeValues.erase(parameter.v);vmap.erase(parameter.v);
      if(type.kind==pd::TypeKind::Qubit){if(src.typeOf(arg)!=type){diag.error("function quantum argument type mismatch",op.loc);return;}vmap[parameter.v]=savedV.at(arg.v);}
      else if(type.kind==pd::TypeKind::Bit||type.kind==pd::TypeKind::UInt){if(src.typeOf(arg)!=type){diag.error("function classical argument type or width mismatch; cast explicitly",op.loc);return;}runtimeValues[parameter.v]=runtimeValue(arg,type);}
      else{
        if(type.kind==pd::TypeKind::Int){auto found=savedExact.find(arg.v);
          if(found!=savedExact.end())bindInteger(parameter.v,found->second);
          else if(const auto snapshot=savedRuntime.find(arg.v);snapshot!=savedRuntime.end()&&valueInfo(snapshot->second).type=="bool")runtimeValues[parameter.v]=snapshot->second;
          else{diag.error("int function parameter requires an exact compile-time integer or a measured 0/1 snapshot; use uint for runtime arithmetic",op.loc);return;}}
        else {auto found=savedCt.find(arg.v);if(found==savedCt.end()){diag.error("angle function parameter must be compile-time",op.loc);return;}ctMap[parameter.v]=found->second;}
      }
    }
    // Callee control never consumes a caller's break/continue context.
    const auto outerFlow=std::move(flow);flow.clear();flow.push_back(std::move(scope));callStack.push_back(name);
    emitRange(fr.bodyStart,fr.bodyEnd);callStack.pop_back();
    auto completed=flow.front();flow=outerFlow;returning=false;transferring=false;
    ctMap=savedCt;exactMap=savedExact;runtimeValues=savedRuntime;
    for(const auto& [id,value]:savedV){if(out.typeOf(value)==sd::qubitType())vmap[id]=wireValue[quantumWire.at(value.v)];else vmap[id]=value;}
    quantum=0;
    for(std::size_t k=0;k<op.results.size();++k){auto id=op.results[k].v;
      if(completed.types[k].kind==pd::TypeKind::Qubit)vmap[id]=wireValue[completed.wires[quantum++]];
      else if(completed.values[k].starts_with("#i:"))bindInteger(id,sd::parseExactInteger<std::int64_t>(completed.values[k].substr(3)));
      else if(completed.values[k].starts_with("#a:"))ctMap[id]=std::stod(completed.values[k].substr(3));
      else if(completed.values[k]=="#unset"){diag.error("function result is not definitely assigned",op.loc);return;}
      else runtimeValues[id]=completed.values[k];
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
  void aliasSkippedRange(std::uint32_t lo, std::uint32_t hi,bool reserveFresh=true) {
    for (std::uint32_t i = lo; i < hi && !diag.hasErrors(); ++i) {
      const auto& op = src.op(pd::OpId{i});
      if(op.kind==pd::OpKind::AllocQubit){
        if(reserveFresh)emitSpinorOp(pd::OpId{i});
        else for(auto result:op.results)vmap.erase(result.v);
        continue;
      }
      if (op.kind == pd::OpKind::AllocBit) {
        if(!reserveFresh){for(auto result:op.results){vmap.erase(result.v);runtimeValues.erase(result.v);}continue;}
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
      if(!reserveFresh&&std::any_of(operands.begin(),operands.end(),[&](auto operand){return !vmap.count(operand.v);})){for(auto result:op.results)if(src.typeOf(result).kind==pd::TypeKind::Qubit)vmap.erase(result.v);continue;}
      std::size_t slot = 0;
      for (auto result : op.results) if (src.typeOf(result).kind == pd::TypeKind::Qubit) {
        if (slot >= operands.size()) {
          if(!reserveFresh){vmap.erase(result.v);continue;}
          diag.error("untaken branch requires unsupported quantum value merging", op.loc); return;
        }
        vmap[result.v] = mapValue(operands[slot++]);
      }
    }
  }

  // -- Emit a range of ops, recursively unrolling control flow --------
  void emitRange(std::uint32_t lo, std::uint32_t hi) {
    std::uint32_t i = lo;
    while (i < hi && !returning && !transferring && !diag.hasErrors()) {
      if(out.numOps()>=operationBudget){diag.error("expanded operation budget exceeded after inlining; raise QSTACK_EXPANDED_OPERATION_BUDGET or reduce the program");return;}
      if(guardContinuation(i,hi))return;
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
          for (pd::ValueId r : op.results) bindInteger(r.v,*value);
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
                for(auto result:op.results)bindInteger(result.v,value);++i;break;
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
          const auto exactLeft=exactMap.find(op.operands.at(0).v),exactRight=exactMap.find(op.operands.at(1).v);
          if(exactLeft!=exactMap.end()&&exactRight!=exactMap.end()){
            std::string symbol;for(const auto& attr:op.attributes)if(attr.name=="op")symbol=std::get<std::string>(attr.value);
            const auto a=exactLeft->second,c=exactRight->second;bool value;
            if(symbol=="==")value=a==c;else if(symbol=="!=")value=a!=c;else if(symbol=="<")value=a<c;else if(symbol=="<=")value=a<=c;else if(symbol==">")value=a>c;else if(symbol==">=")value=a>=c;
            else{diag.error("unsupported comparison predicate",op.loc);return;}
            for(auto result:op.results)ctMap[result.v]=value?1.0:0.0;++i;break;
          }
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
              const auto known=ctMap.find(constant.v);const auto exact=exactMap.find(constant.v);const auto runtime=runtimeValues.find(bit.v);
              const bool isBit=src.typeOf(bit).kind==pd::TypeKind::Bit||(runtime!=runtimeValues.end()&&valueInfo(runtime->second).type=="bool");
              if(!isBit||(known==ctMap.end()&&exact==exactMap.end()))continue;
              auto truth=[&](double value){
                if(exact!=exactMap.end()){
                  const std::int64_t bitValue=value==0?0:1,a=side?exact->second:bitValue,b=side?bitValue:exact->second;
                  if(symbol=="==")return a==b;if(symbol=="!=")return a!=b;if(symbol=="<")return a<b;
                  if(symbol=="<=")return a<=b;if(symbol==">")return a>b;if(symbol==">=")return a>=b;
                  throw std::invalid_argument("unsupported comparison predicate");
                }
                const double a=side?known->second:value,b=side?value:known->second;
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
          if(callStack.size()>=128){diag.error("function inlining exceeds the maximum depth of 128",op.loc);return;}
          // Bind parameter values via vmap (qubit args) and ctMap
          // (classical args).
          const FuncRange& fr = it->second;
          if (fr.paramValues.size() != op.operands.size()) {
            diag.error("argument count mismatch for function '" + name + "'");
            return;
          }
          if(fr.structuredReturns||std::any_of(fr.paramTypes.begin(),fr.paramTypes.end(),[](auto type){return type.kind==pd::TypeKind::UInt;})||std::any_of(op.results.begin(),op.results.end(),[&](auto value){return src.typeOf(value).kind!=pd::TypeKind::Qubit;})){
            emitTypedCall(op,fr,name);++i;break;
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
              if(fr.paramTypes[k].kind==pd::TypeKind::Int){
                const auto exact=exactMap.find(op.operands[k].v);
                if(exact==exactMap.end()){diag.error("int function parameter requires an exact compile-time integer",op.loc);return;}
                bindInteger(pv,exact->second);continue;
              }
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
          const auto outerFlow=std::move(flow);flow.clear();
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
          flow=outerFlow;
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
          std::int64_t loInt=0,hiInt=0;
          if (op.operands.size() == 2) {
            auto a = exactMap.find(op.operands[0].v);
            auto b_ = exactMap.find(op.operands[1].v);
            if (a == exactMap.end() || b_ == exactMap.end()) {
              diag.error("for-loop bounds must be compile-time integers");
              return;
            }
            loInt = a->second; hiInt = b_->second;
          }else{diag.error("for-loop expects two exact integer bounds",op.loc);return;}
          std::string var;
          for (const auto& a : op.attributes) if (a.name == "var") var = std::get<std::string>(a.value);
          std::uint32_t bodyStart = i + 1;
          std::uint32_t bodyEnd   = matchMarker(i, pd::OpKind::For,
                                                 pd::OpKind::EndFor);
          // Unsigned subtraction represents the positive signed span exactly,
          // including ranges crossing zero; no signed subtraction can overflow.
          const auto count=hiInt>loInt?static_cast<std::uint64_t>(hiInt)-static_cast<std::uint64_t>(loInt):0;
          if (count>operationBudget) {
            diag.error("for-loop iteration count exceeds QSTACK_EXPANDED_OPERATION_BUDGET");
            return;
          }
          if(!count)aliasSkippedRange(bodyStart,bodyEnd,false);
          for (std::uint64_t iteration=0;iteration<count&&!returning&&!transferring;++iteration) {
            // Source-level induction indices were resolved before SSA construction.
            // A programmatically built For repeats its fixed body while the
            // value mapping threads each slot's latest value between iterations.
            if (++expandedIterations > operationBudget) { diag.error("static loop expansion exceeds QSTACK_EXPANDED_OPERATION_BUDGET"); return; }
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
          if(!op.operands.empty())if(const auto exact=exactMap.find(op.operands.front().v);exact!=exactMap.end()){
            if(exact->second){emitRange(bodyStart,bodyStart+thenCount);aliasSkippedRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
            else{aliasSkippedRange(bodyStart,bodyStart+thenCount);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);}
            i=bodyEnd+1;break;
          }
          auto condition = op.operands.empty() ? ctMap.end() : ctMap.find(op.operands.front().v);
          if(controller&&runtimeValues.count(op.operands.at(0).v)){
            const auto predicate=runtimeValues.at(op.operands[0].v);const auto info=valueInfo(predicate);
            if(info.width!=1){diag.error("if predicate must be Boolean; compare uint values explicitly",op.loc);return;}
            const auto marker=out.numOps();b.beginIf(info.storage.at(0),true,op.loc);
            out.opMut(sd::OpId{static_cast<std::uint32_t>(marker)}).attributes.push_back({"condition",predicate});
            const auto outputsBefore=loopOutputs;const auto flowBefore=flow;
            ++runtimeDepth;emitRange(bodyStart,bodyStart+thenCount);
            const auto thenReturning=returning,thenTransferring=transferring;returning=false;transferring=false;
            const auto thenFlow=flow;flow=flowBefore;
            const auto thenOutputs=loopOutputs;loopOutputs=outputsBefore;
            if(elseCount){b.elseBranch(op.loc);emitRange(bodyStart+thenCount,bodyStart+thenCount+elseCount);
            }
            --runtimeDepth;b.endIf(op.loc);
            const auto elseFlow=flow;joinFlow(predicate,thenFlow,elseFlow,op.loc);
            returning=thenReturning&&returning;transferring=thenTransferring&&transferring;
            for(const auto& [name,before]:outputsBefore){
              const auto yes=thenOutputs.at(name),no=loopOutputs.at(name);
              if(yes!=no)loopOutputs[name]=emitController(sd::OpKind::CSelect,pd::bitType(),{predicate,yes,no},op.loc);
            }
          } else if (condition == ctMap.end()) {
            auto predicate=op.operands.front();
            const auto& cmp=src.op(src.producerOf(predicate));
            pd::ValueId bit=predicate;double constant=1;std::optional<std::int64_t> exactConstant;std::string comparison="==";bool reverse=false;
            if(cmp.kind==pd::OpKind::Cmp){
              for(const auto& attr:cmp.attributes)if(attr.name=="op")comparison=std::get<std::string>(attr.value);
              auto left=ctMap.find(cmp.operands[0].v),right=ctMap.find(cmp.operands[1].v);
              const auto leftExact=exactMap.find(cmp.operands[0].v),rightExact=exactMap.find(cmp.operands[1].v);
              if(rightExact!=exactMap.end()){bit=cmp.operands[0];exactConstant=rightExact->second;}
              else if(leftExact!=exactMap.end()){bit=cmp.operands[1];exactConstant=leftExact->second;reverse=true;}
              else if(right!=ctMap.end()){bit=cmp.operands[0];constant=right->second;}
              else if(left!=ctMap.end()){bit=cmp.operands[1];constant=left->second;reverse=true;}
              else {diag.error("runtime comparison requires one measured bit and one compile-time value",op.loc);return;}
            }
            auto mapped=vmap.find(bit.v);
            if(mapped==vmap.end()||out.typeOf(mapped->second)!=sd::bitType()){
              diag.error("runtime predicate is not a measured classical bit",op.loc);return;
            }
            auto evaluate=[&](double value){
              if(exactConstant){
                const std::int64_t bitValue=value==0?0:1,lhs=reverse?*exactConstant:bitValue,rhs=reverse?bitValue:*exactConstant;
                if(comparison=="==")return lhs==rhs;if(comparison=="!=")return lhs!=rhs;
                if(comparison=="<")return lhs<rhs;if(comparison==">")return lhs>rhs;
                if(comparison=="<=")return lhs<=rhs;if(comparison==">=")return lhs>=rhs;
                throw std::runtime_error("unsupported classical predicate");
              }
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
          if(returning||transferring)aliasSkippedRange(bodyEnd+1,hi,false);
          i = bodyEnd + 1;
          break;
        }
        case pd::OpKind::EndIf:
          ++i; break;

        case pd::OpKind::LoopBody: {
          const auto end=matchMarker(i,pd::OpKind::LoopBody,pd::OpKind::EndLoopBody);
          if(end>=src.numOps()){diag.error("bounded loop body is missing its end marker",op.loc);return;}
          FlowScope scope;scope.done=boolean(false,op.loc);scope.breaking=boolean(false,op.loc);
          for(auto value:op.operands){const auto type=src.typeOf(value);scope.types.push_back(type);scope.values.push_back(runtimeValue(value,type));}
          flow.push_back(std::move(scope));emitRange(i+1,end+1);auto completed=flow.back();flow.pop_back();transferring=false;
          if(op.results.size()!=completed.values.size()+1){diag.error("bounded loop body result arity mismatch",op.loc);return;}
          for(std::size_t k=0;k<completed.values.size();++k)runtimeValues[op.results[k].v]=completed.values[k];
          runtimeValues[op.results.back().v]=completed.breaking;i=end+1;break;
        }
        case pd::OpKind::EndLoopBody: {
          if(flow.empty()||flow.back().function){diag.error("loop yield outside bounded loop",op.loc);return;}
          auto& scope=flow.back();bool hasBreak=false;for(const auto& attr:op.attributes)if(attr.name=="has_break")hasBreak=std::get<std::int64_t>(attr.value)!=0;
          if(op.operands.size()!=scope.types.size()+static_cast<std::size_t>(hasBreak)){diag.error("bounded loop yield arity mismatch",op.loc);return;}
          for(std::size_t k=0;k<scope.types.size();++k)scope.values[k]=runtimeValue(op.operands[k],scope.types[k]);
          if(hasBreak)scope.breaking=runtimeValue(op.operands.back(),pd::bitType());++i;break;
        }
        case pd::OpKind::Break:
        case pd::OpKind::Continue: {
          if(flow.empty()||flow.back().function){diag.error("break/continue requires a bounded loop in this function",op.loc);return;}
          auto& scope=flow.back();if(op.operands.size()!=scope.types.size()){diag.error("loop transfer changes carried arity",op.loc);return;}
          for(std::size_t k=0;k<scope.types.size();++k){if(src.typeOf(op.operands[k])!=scope.types[k]){diag.error("loop transfer changes carried type or width",op.loc);return;}scope.values[k]=runtimeValue(op.operands[k],scope.types[k]);}
          scope.done=boolean(true,op.loc);scope.breaking=boolean(op.kind==pd::OpKind::Break,op.loc);scope.mayTransfer=true;transferring=true;aliasSkippedRange(i+1,hi,false);return;
        }

        case pd::OpKind::While:
        case pd::OpKind::EndWhile: {
          diag.error("phonon.while is not supported by lowering "
                     "(use a bounded for-loop)");
          ++i; break;
        }

        case pd::OpKind::Return:
          if(!flow.empty()&&flow.front().function){
            auto& scope=flow.front();if(op.operands.size()!=scope.types.size()){diag.error("function return arity mismatch",op.loc);return;}
            std::vector<std::size_t> wires;
            for(std::size_t k=0;k<scope.types.size();++k){
              if(src.typeOf(op.operands[k])!=scope.types[k]){diag.error("function return type or width mismatch",op.loc);return;}
              if(scope.types[k].kind==pd::TypeKind::Qubit){const auto wire=quantumWire.at(mapValue(op.operands[k]).v);if(std::find(wires.begin(),wires.end(),wire)!=wires.end()){diag.error("function cannot return duplicate qubit aliases",op.loc);return;}wires.push_back(wire);}
              else if(scope.types[k].kind==pd::TypeKind::Int&&exactMap.count(op.operands[k].v))scope.values[k]="#i:"+std::to_string(exactMap.at(op.operands[k].v));
              else if(scope.types[k].kind==pd::TypeKind::Angle&&ctMap.count(op.operands[k].v)){
                char buffer[64];const auto formatted=std::to_chars(buffer,buffer+sizeof(buffer),ctMap.at(op.operands[k].v));scope.values[k]="#a:"+std::string(buffer,formatted.ptr);
              }else scope.values[k]=runtimeValue(op.operands[k],scope.types[k]);
            }
            transferQuantum(scope.wires,wires,op.loc);scope.done=boolean(true,op.loc);scope.mayTransfer=true;returning=true;aliasSkippedRange(i+1,hi,false);return;
          }
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
  try {
  Lowerer lo(m, target);
  lo.indexFunctions();
  try{lo.emitRange(0, m.numOps());}
  catch(const std::exception& error){lo.diag.error(std::string("classical lowering: ")+error.what());}
  if(lo.out.numOps()>lo.operationBudget)lo.diag.error("expanded operation budget exceeded after lowering");
  std::vector<std::string> outputNames;for(const auto& [name,_]:lo.loopOutputs)outputNames.push_back(name);std::sort(outputNames.begin(),outputNames.end());
  for(const auto& name:outputNames)lo.out.classicalOutputs.push_back({name,lo.loopOutputs.at(name),"bool",1,"loop_exhausted"});
  Result r;
  r.diag = std::move(lo.diag);
  if (!r.diag.hasErrors()) r.module = std::move(lo.out);
  return r;
  }catch(const std::exception& error){Result r;r.diag.error(std::string("Phonon lowering: ")+error.what());return r;}
}

}  // namespace phonon::lower

// photon/lang/cpp/lib/Lower.cpp
#include "photon/lang/Lower.h"
#include "spinor/dialect/ExactInteger.h"
#include "photon/lang/Library.h"
#include <cmath>
#include <charconv>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <numbers>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace photon::lang {
namespace pd = phonon::dialect;
namespace {

bool containsReturn(const std::vector<StmtPtr>& body) {
  for(const auto& statement:body)if(statement&&(statement->kind==StmtKind::ReturnStmt||
      (statement->kind==StmtKind::IfStmt&&(containsReturn(statement->then_body)||containsReturn(statement->else_body)))))return true;
  return false;
}
bool containsReturnDeep(const std::vector<StmtPtr>& body){
  for(const auto& s:body)if(s&&(s->kind==StmtKind::ReturnStmt||containsReturnDeep(s->body)||
      containsReturnDeep(s->then_body)||containsReturnDeep(s->else_body)))return true;
  return false;
}
bool boundedReturnPresent(const std::vector<StmtPtr>& body){
  for(const auto& s:body)if(s&&((s->kind==StmtKind::WhileLoop&&containsReturnDeep(s->body))||
      boundedReturnPresent(s->body)||boundedReturnPresent(s->then_body)||boundedReturnPresent(s->else_body)))return true;
  return false;
}
bool fallsThrough(const std::vector<StmtPtr>& body){
  for(const auto& s:body)if(s){
    if(s->kind==StmtKind::ReturnStmt)return false;
    if(s->kind==StmtKind::IfStmt&&!fallsThrough(s->then_body)&&!fallsThrough(s->else_body))return false;
  }
  return true;
}

// Continuations are copied into the surviving arm before building SSA. This
// makes a terminal return suppress every following operation on that path.
std::vector<StmtPtr> normalizeReturns(const std::vector<StmtPtr>& body) {
  std::vector<StmtPtr> result;
  for(std::size_t i=0;i<body.size();++i){
    if(!body[i])continue;
    auto statement=std::make_shared<Stmt>(*body[i]);result.push_back(statement);
    if(statement->kind==StmtKind::ReturnStmt)break;
    if(statement->kind==StmtKind::IfStmt&&(containsReturn(statement->then_body)||containsReturn(statement->else_body))){
      statement->then_body.insert(statement->then_body.end(),body.begin()+i+1,body.end());
      statement->else_body.insert(statement->else_body.end(),body.begin()+i+1,body.end());
      statement->then_body=normalizeReturns(statement->then_body);
      statement->else_body=normalizeReturns(statement->else_body);
      break;
    }
  }
  return result;
}

struct Lowerer {
  const Module& src;
  pd::Module out;
  pd::Builder b;
  Diagnostics diag;
  bool fatal = false;

  // Slot tables, scoped per-function. A QReg of size N maps to a
  // vector of N current SSA qubit ValueIds.
  std::unordered_map<std::string, std::vector<pd::ValueId>> qslots;
  std::unordered_map<std::string,int> quantumDeclarationDepth;
  std::unordered_map<std::string, std::vector<pd::ValueId>> bslots;
  std::unordered_map<std::string, pd::ValueId> classicals;
  std::unordered_map<std::string, std::int64_t> intConsts;
  std::unordered_map<std::string, double> angleConsts;
  std::unordered_map<std::string, pd::Type> scalarTypes;
  std::unordered_map<std::string, std::size_t> bitBase;
  std::size_t nextBit = 0;
  std::unordered_set<std::size_t> savedMeasurementBits;
  std::size_t expandedIterations = 0;
  std::size_t operationBudget = 100000;
  std::size_t boundedLoops = 0;
  bool returned = false;
  enum class ReturnForm { None, Void, Classical, Measurement };
  ReturnForm returnForm=ReturnForm::None;
  pd::ValueId returnValue=pd::kInvalidValue;
  std::pair<std::size_t,std::size_t> returnMeasurement{};
  std::size_t boundedLoopDepth=0;
  struct LoopFrame {std::string live,done;bool mayTransfer=false;};
  std::vector<LoopFrame> loopFrames;
  bool bypassNextStatementGuard=false;
  bool dynamicReturns=false,functionMayReturn=false;
  ReturnForm dynamicReturnForm=ReturnForm::None;
  std::optional<pd::Type> dynamicReturnType;
  std::optional<std::pair<std::size_t,std::size_t>> fixedMeasurementReturn;
  const std::string functionDone="__photon_function_returned",functionValue="__photon_function_result";
  int runtimeDepth=0;
  bool in_def_ = false;  // true while inside a phonon.def body.

  Lowerer(const Module& m) : src(m), b(out) {
    out.targetAttr = m.target;
    if(const auto* setting=std::getenv("QSTACK_EXPANDED_OPERATION_BUDGET")){
      const std::string text(setting);
      const auto parsed=std::from_chars(text.data(),text.data()+text.size(),operationBudget);
      if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||operationBudget==0)
        err("QSTACK_EXPANDED_OPERATION_BUDGET must be a positive integer",{});
    }
  }

  pd::Location loc(const Location& l) const {
    return {l.file, l.line, l.column};
  }
  void err(std::string msg, const Location& l) {
    diag.error(std::move(msg), loc(l));
    fatal = true;
  }

  // Compile-time integer evaluator for index expressions.
  std::optional<std::int64_t> foldInt(const ExprPtr& e) const {
    if (!e) return std::nullopt;
    switch (e->kind) {
      case ExprKind::IntLit: case ExprKind::BoolLit: return e->int_value;
      case ExprKind::RealLit: return std::nullopt;
      case ExprKind::Ident: {
        auto it = intConsts.find(e->text);
        if (it != intConsts.end()) return it->second;
        return std::nullopt;
      }
      case ExprKind::UnaryMinus: {
        if(e->children[0]->kind==ExprKind::UIntLit&&e->children[0]->uint_value==(std::uint64_t{1}<<63))return std::numeric_limits<std::int64_t>::min();
        auto v = foldInt(e->children[0]);
        if (!v || *v == std::numeric_limits<std::int64_t>::min()) return std::nullopt;
        return -*v;
      }
      case ExprKind::BinOp: {
        auto a = foldInt(e->children[0]);
        auto bv = foldInt(e->children[1]);
        if (!a || !bv) return std::nullopt;
        try { return spinor::dialect::checkedInteger(e->text,*a,*bv); }
        catch(const std::exception&) { return std::nullopt; }
      }
      default: return std::nullopt;
    }
  }

  // Compile-time real evaluator for angle expressions (handles pi).
  std::optional<double> foldReal(const ExprPtr& e) const {
    if (!e) return std::nullopt;
    switch (e->kind) {
      case ExprKind::IntLit:  return static_cast<double>(e->int_value);
      case ExprKind::RealLit: return e->real_value;
      case ExprKind::Pi:      return std::numbers::pi;
      case ExprKind::UnaryMinus: {
        auto v = foldReal(e->children[0]);
        if (!v) return std::nullopt;
        return -*v;
      }
      case ExprKind::BinOp: {
        auto a = foldReal(e->children[0]);
        auto bv = foldReal(e->children[1]);
        if (!a || !bv) return std::nullopt;
        if (e->text == "+") return *a + *bv;
        if (e->text == "-") return *a - *bv;
        if (e->text == "*") return *a * *bv;
        if (e->text == "/" && *bv != 0.0) return *a / *bv;
        return std::nullopt;
      }
      case ExprKind::Ident: {
        auto angle = angleConsts.find(e->text);
        if (angle != angleConsts.end()) return angle->second;
        auto it = intConsts.find(e->text);
        if (it != intConsts.end()) return static_cast<double>(it->second);
        return std::nullopt;
      }
      default: return std::nullopt;
    }
  }

  pd::ValueId emitConstInt(std::int64_t v, pd::Location l) {
    return b.constInt(v, std::move(l));
  }

  void prepareDynamicReturns(const Function& function){
    dynamicReturns=boundedReturnPresent(function.body);functionMayReturn=false;
    dynamicReturnForm=ReturnForm::None;dynamicReturnType.reset();fixedMeasurementReturn.reset();
    if(!dynamicReturns)return;
    std::unordered_map<std::string,Type> declarations;
    std::function<void(const std::vector<StmtPtr>&)> collect=[&](const auto& body){for(const auto& s:body)if(s){
      if(s->kind==StmtKind::VarDecl)declarations[s->name]=s->decl_type;
      collect(s->body);collect(s->then_body);collect(s->else_body);
    }};collect(function.body);
    std::function<std::optional<pd::Type>(const ExprPtr&)> infer=[&](const ExprPtr& e)->std::optional<pd::Type>{
      if(!e)return std::nullopt;
      if(e->kind==ExprKind::Ident&&declarations.count(e->text)){
        auto type=declarations.at(e->text);if(type.kind==TypeKind::UInt)return pd::uintType(type.qreg_size);
        if(type.kind==TypeKind::Bit)return pd::bitType();if(type.kind==TypeKind::Int)return pd::uintType(64);
      }
      if(e->kind==ExprKind::CastUInt)return pd::uintType(static_cast<std::uint32_t>(e->int_value));
      if(e->kind==ExprKind::BoolLit||e->kind==ExprKind::UnaryNot||e->kind==ExprKind::Index||
        (e->kind>=ExprKind::CmpEq&&e->kind<=ExprKind::CmpGe))return pd::bitType();
      if(e->kind==ExprKind::BinOp||e->kind==ExprKind::UnaryBitNot){auto type=infer(e->children[0]);if(type)return type;
        if(e->children.size()>1)return infer(e->children[1]);}
      if(e->kind==ExprKind::IntLit||e->kind==ExprKind::UIntLit)return pd::uintType(64);
      return std::nullopt;
    };
    std::function<void(const std::vector<StmtPtr>&)> inspect=[&](const auto& body){for(const auto& s:body)if(s){
      if(s->kind==StmtKind::ReturnStmt){
        ReturnForm form=s->init?ReturnForm::Classical:ReturnForm::Void;
        if(s->init&&s->init->kind==ExprKind::Call&&(s->init->text.ends_with(".measure")||s->init->text.ends_with(".measure_int")))form=ReturnForm::Measurement;
        if(dynamicReturnForm!=ReturnForm::None&&dynamicReturnForm!=form){err("bounded return paths require the same output representation",s->loc);return;}
        dynamicReturnForm=form;
        if(form==ReturnForm::Classical){
          auto type=function.return_type.kind==TypeKind::UInt?std::optional{pd::uintType(function.return_type.qreg_size)}:
            function.return_type.kind==TypeKind::Bit?std::optional{pd::bitType()}:infer(s->init);
          if(!type|| (dynamicReturnType&&*dynamicReturnType!=*type)){err("bounded scalar returns require matching fixed types and widths",s->loc);return;}
          dynamicReturnType=type;
        }
      }
      inspect(s->body);inspect(s->then_body);inspect(s->else_body);
    }};inspect(function.body);if(fatal)return;
    if(dynamicReturnForm!=ReturnForm::Void&&fallsThrough(function.body)){
      err("bounded value returns require a fallback return for the exhaustion path",function.loc);return;
    }
    classicals[functionDone]=b.copy(b.constInt(0),pd::bitType());scalarTypes[functionDone]=pd::bitType();
    if(dynamicReturnType){
      const auto type=*dynamicReturnType;
      classicals[functionValue]=type.kind==pd::TypeKind::UInt?b.constUInt(0,type.width):b.copy(b.constInt(0),pd::bitType());
      scalarTypes[functionValue]=type;
    }
  }

  void markDynamicReturn(const Location& where){
    classicals[functionDone]=b.copy(b.constInt(1,loc(where)),pd::bitType(),loc(where));functionMayReturn=true;
    for(auto& frame:loopFrames){
      classicals[frame.live]=b.copy(b.constInt(0,loc(where)),pd::bitType(),loc(where));
      classicals[frame.done]=b.copy(b.constInt(1,loc(where)),pd::bitType(),loc(where));frame.mayTransfer=true;
    }
  }

  std::optional<bool> foldBool(const ExprPtr& e) const {
    if (!e) return std::nullopt;
    if (auto v=foldInt(e)) return *v != 0;
    if (e->kind==ExprKind::UnaryNot) { auto v=foldBool(e->children[0]); if(v)return !*v; }
    if (e->children.size()==2) {
      auto compare=[&](auto a,auto c)->std::optional<bool>{
        switch(e->kind){
          case ExprKind::CmpEq:return a==c;case ExprKind::CmpNeq:return a!=c;
          case ExprKind::CmpLt:return a<c;case ExprKind::CmpGt:return a>c;
          case ExprKind::CmpLe:return a<=c;case ExprKind::CmpGe:return a>=c;
          default:return std::nullopt;
        }
      };
      auto a=foldInt(e->children[0]),c=foldInt(e->children[1]);
      if(a&&c)return compare(*a,*c);
      auto x=foldReal(e->children[0]),y=foldReal(e->children[1]);
      if(x&&y)return compare(*x,*y);
    }
    return std::nullopt;
  }

  std::optional<pd::Type> expressionType(const ExprPtr& e) const {
    if(!e)return std::nullopt;
    if(e->kind==ExprKind::Ident){auto i=classicals.find(e->text);if(i!=classicals.end())return out.typeOf(i->second);}
    if(e->kind==ExprKind::CastUInt)return pd::uintType(static_cast<std::uint32_t>(e->int_value));
    if(e->kind==ExprKind::UnaryBitNot)return expressionType(e->children[0]);
    if(e->kind==ExprKind::BoolLit||e->kind==ExprKind::UnaryNot||e->kind==ExprKind::Index||
        (e->kind>=ExprKind::CmpEq&&e->kind<=ExprKind::CmpGe))return pd::bitType();
    if(e->kind==ExprKind::BinOp){auto a=expressionType(e->children[0]),c=expressionType(e->children[1]);
      if(a&&(a->kind==pd::TypeKind::UInt||a->kind==pd::TypeKind::Bit))return a;
      return c?c:a;}
    return std::nullopt;
  }

  pd::ValueId expression(const ExprPtr& e,std::optional<pd::Type> expected=std::nullopt,bool allowMeasurement=false) {
    if(!e){err("missing classical expression",{});return pd::kInvalidValue;}
    const auto L=loc(e->loc);
    if(e->kind==ExprKind::RealLit){
      if(!std::isfinite(e->real_value)){err("classical literal must be finite",e->loc);return pd::kInvalidValue;}
      return b.constAngle(e->real_value,L);
    }
    if(e->kind==ExprKind::Call&&allowMeasurement){
      const auto dot=e->text.find('.');const auto reg=e->text.substr(0,dot);
      if(dot==std::string::npos||e->text.substr(dot+1)!="measure"||!qslots.count(reg)){
        err("measurement initializer must be q.measure(index)",e->loc);return pd::kInvalidValue;}
      auto& qs=qslots[reg];std::optional<std::int64_t> index;
      if(e->children.size()==1)index=foldInt(e->children[0]);else if(e->children.empty()&&qs.size()==1)index=0;
      if(!index||*index<0||static_cast<std::size_t>(*index)>=qs.size()){
        err("measurement requires an explicit valid bit index",e->loc);return pd::kInvalidValue;}
      if(qs[*index]==pd::kInvalidValue){err("measurement of discarded qubit",e->loc);return pd::kInvalidValue;}
      auto bit=b.measure(qs[*index],L);auto destination=bitBase[reg]+*index;
      if(savedMeasurementBits.contains(destination))destination=nextBit++;
      savedMeasurementBits.insert(destination);
      out.opMut(out.producerOf(bit)).attributes.push_back({"clbit",static_cast<double>(destination)});
      bslots["__c_"+reg][*index]=bit;return bit;
    }
    if(e->kind==ExprKind::Ident){
      auto i=classicals.find(e->text);if(i==classicals.end()){err("unknown classical variable '"+e->text+"'",e->loc);return pd::kInvalidValue;}
      return i->second;
    }
    if(e->kind==ExprKind::Index){
      auto bits=bslots.find(e->text);if(bits==bslots.end())bits=bslots.find("__c_"+e->text);
      auto index=foldInt(e->children.at(0));
      if(bits==bslots.end()||!index||*index<0||static_cast<std::size_t>(*index)>=bits->second.size()){
        err("saved bit index must name a measured register and static in-range index",e->loc);return pd::kInvalidValue;}
      auto result=bits->second[*index];
      if(result==pd::kInvalidValue||out.op(out.producerOf(result)).kind==pd::OpKind::AllocBit){err("read of unmeasured bit",e->loc);return pd::kInvalidValue;}
      return result;
    }
    if(e->kind==ExprKind::CastUInt){
      auto type=pd::uintType(static_cast<std::uint32_t>(e->int_value));
      if(e->children[0]->kind==ExprKind::IntLit||e->children[0]->kind==ExprKind::UIntLit)
        return expression(e->children[0],type);
      auto source=expression(e->children[0]);if(fatal)return source;
      const auto from=out.typeOf(source);
      if(from.kind==pd::TypeKind::Angle||from.kind==pd::TypeKind::Qubit){err("UInt cast requires a classical integer or Boolean value",e->loc);return pd::kInvalidValue;}
      return b.copy(source,type,L);
    }
    if(e->kind==ExprKind::UIntLit||e->kind==ExprKind::IntLit||e->kind==ExprKind::BoolLit){
      if(expected&&expected->kind==pd::TypeKind::UInt){
        if(e->kind!=ExprKind::UIntLit&&e->int_value<0){err("UInt literal must be nonnegative",e->loc);return pd::kInvalidValue;}
        const auto value=e->kind==ExprKind::UIntLit?e->uint_value:static_cast<std::uint64_t>(e->int_value);
        if(expected->width<64&&value>=(std::uint64_t{1}<<expected->width)){err("UInt literal does not fit its width",e->loc);return pd::kInvalidValue;}
        return b.constUInt(value,expected->width,L);
      }
      if(e->kind==ExprKind::UIntLit){err("large unsigned literals require explicit UInt<width> context",e->loc);return pd::kInvalidValue;}
      auto value=b.constInt(e->int_value,L);
      return e->kind==ExprKind::BoolLit?b.copy(value,pd::bitType(),L):value;
    }
    if(e->kind==ExprKind::UnaryMinus){
      auto v=foldInt(e);if(v)return b.constInt(*v,L);
      err("runtime unary minus requires explicit unsigned subtraction",e->loc);return pd::kInvalidValue;
    }
    if(e->kind==ExprKind::UnaryNot){
      if(auto constant=foldBool(e))return b.cmp("!=",b.constInt(*constant,L),b.constInt(0,L),L);
      auto value=expression(e->children[0]);if(fatal)return value;
      if(out.typeOf(value)!=pd::bitType()){err("Boolean not requires Bit",e->loc);return pd::kInvalidValue;}
      return b.cmp("==",value,b.constInt(0,L),L);
    }
    if(e->kind==ExprKind::UnaryBitNot){
      auto value=expression(e->children[0],expected);if(fatal)return value;
      if(out.typeOf(value).kind!=pd::TypeKind::UInt){err("bitwise complement requires UInt<width>",e->loc);return pd::kInvalidValue;}
      return b.bitNot(value,L);
    }
    if(e->kind==ExprKind::BinOp||(e->kind>=ExprKind::CmpEq&&e->kind<=ExprKind::CmpGe)){
      auto type=expressionType(e->children[0]);auto other=expressionType(e->children[1]);
      if(!type||type->kind==pd::TypeKind::Int)type=other?other:expected;
      auto left=expression(e->children[0],type),right=expression(e->children[1],type);if(fatal)return pd::kInvalidValue;
      const auto lt=out.typeOf(left),rt=out.typeOf(right);
      if(lt!=rt&&lt.kind!=pd::TypeKind::Int&&rt.kind!=pd::TypeKind::Int){err("classical expression widths differ; cast explicitly",e->loc);return pd::kInvalidValue;}
      if(e->kind==ExprKind::BinOp){
        if(e->text!="+"&&e->text!="-"&&e->text!="&"&&e->text!="|"&&e->text!="^"&&e->text!="<<"&&e->text!=">>"){
          err("runtime operator '"+e->text+"' is unsupported",e->loc);return pd::kInvalidValue;}
        if((e->text=="+"||e->text=="-"||e->text=="<<"||e->text==">>")&&(!type||type->kind!=pd::TypeKind::UInt)){
          err("runtime arithmetic requires explicit UInt<width>",e->loc);return pd::kInvalidValue;}
        if(type&&type->kind==pd::TypeKind::Bit){
          if(lt.kind==pd::TypeKind::Int)left=b.copy(left,pd::bitType(),L);
          if(rt.kind==pd::TypeKind::Int)right=b.copy(right,pd::bitType(),L);
        }
        return b.binOp(e->text,left,right,L);
      }
      std::string op=e->kind==ExprKind::CmpEq?"==":e->kind==ExprKind::CmpNeq?"!=":e->kind==ExprKind::CmpLt?"<":e->kind==ExprKind::CmpLe?"<=":e->kind==ExprKind::CmpGt?">":">=";
      return b.cmp(op,left,right,L);
    }
    err("unsupported runtime classical expression",e->loc);return pd::kInvalidValue;
  }

  pd::ValueId predicate(const ExprPtr& e,bool snapshot=false) {
    pd::ValueId value;
    if(auto constant=foldBool(e))value=b.cmp("!=",b.constInt(*constant),b.constInt(0));
    else value=expression(e);
    if(fatal)return pd::kInvalidValue;
    if(out.typeOf(value)!=pd::bitType()){err("runtime condition must be a Boolean comparison or Bit",e->loc);return pd::kInvalidValue;}
    return snapshot?b.copy(value,pd::bitType(),loc(e->loc)):value;
  }

  void runtimeBranch(pd::ValueId pred,const std::vector<StmtPtr>& yes,const std::vector<StmtPtr>& no,const Location& where){
    const auto entry=classicals;const auto entryTypes=scalarTypes;
    const auto entryInts=intConsts;const auto entryAngles=angleConsts;const auto entryBits=bslots;
    const auto entryQubits=qslots;const auto entryBases=bitBase;
    const auto entryQuantumDepth=quantumDeclarationDepth;
    auto removeLocalQubits=[&](){for(auto it=qslots.begin();it!=qslots.end();){if(!entryQubits.count(it->first))it=qslots.erase(it);else ++it;}};
    const auto id=b.beginIf(pred,loc(where));++runtimeDepth;lowerBlock(yes);
    const auto thenValues=classicals;const auto thenBits=bslots;
    const bool thenReturned=returned;const auto thenForm=returnForm;const auto thenReturn=returnValue;const auto thenMeasurement=returnMeasurement;
    returned=false;returnForm=ReturnForm::None;returnValue=pd::kInvalidValue;
    classicals=entry;scalarTypes=entryTypes;intConsts=entryInts;angleConsts=entryAngles;bslots=entryBits;
    bitBase=entryBases;quantumDeclarationDepth=entryQuantumDepth;removeLocalQubits();
    if(!no.empty()){b.elseIf(id);lowerBlock(no);}
    const auto elseValues=classicals;const auto elseBits=bslots;
    const bool elseReturned=returned;const auto elseForm=returnForm;const auto elseReturn=returnValue;const auto elseMeasurement=returnMeasurement;
    returned=false;returnForm=ReturnForm::None;returnValue=pd::kInvalidValue;
    --runtimeDepth;b.endIf(id,loc(where));
    classicals=entry;scalarTypes=entryTypes;intConsts=entryInts;angleConsts=entryAngles;bslots=entryBits;
    bitBase=entryBases;quantumDeclarationDepth=entryQuantumDepth;removeLocalQubits();
    if(fatal)return;
    if(thenReturned||elseReturned){
      if(!thenReturned||!elseReturned||thenForm!=elseForm){err("conditional return paths require the same output representation",where);return;}
      returned=true;returnForm=thenForm;
      if(thenForm==ReturnForm::Classical){
        if(out.typeOf(thenReturn)!=out.typeOf(elseReturn)){err("conditional return values require identical types and widths",where);return;}
        returnValue=b.select(pred,thenReturn,elseReturn,loc(where));
      }else if(thenForm==ReturnForm::Measurement){
        if(thenMeasurement!=elseMeasurement){err("conditional measurement returns require the same fixed register",where);return;}
        returnMeasurement=thenMeasurement;
      }
    }
    for(const auto& [name,value]:entry){
      const auto a=thenValues.at(name),c=elseValues.at(name);
      if(a!=c)classicals[name]=b.select(pred,a,c,loc(where));else classicals[name]=a;
    }
    for(const auto& [name,bits]:entryBits)for(std::size_t i=0;i<bits.size();++i){
      const auto a=thenBits.at(name)[i],c=elseBits.at(name)[i];
      if(a==c){bslots[name][i]=a;continue;}
      auto merged=b.select(pred,a,c,loc(where));
      auto reg=name.starts_with("__c_")?name.substr(4):name;
      if(bitBase.count(reg))out.opMut(out.producerOf(merged)).attributes.push_back({"mutable_clbit",static_cast<double>(bitBase[reg]+i)});
      bslots[name][i]=merged;
    }
  }

  // Apply a 1q gate by name to the slot value at qslots[name][idx].
  void apply1q(const std::string& reg, std::int64_t idx,
               const std::string& gate, const std::vector<ExprPtr>& args,
               const Location& aloc) {
    auto it = qslots.find(reg);
    if (it == qslots.end() || idx < 0 ||
        idx >= static_cast<std::int64_t>(it->second.size())) {
      err("unknown qubit slot " + reg + "[" + std::to_string(idx) + "]", aloc);
      return;
    }
    pd::ValueId q = it->second[idx];
    if(q==pd::kInvalidValue){err("use of discarded qubit",aloc);return;}
    pd::Location L = loc(aloc);
    pd::ValueId r;
    if      (gate == "h" || gate == "hadamard") r = b.h(q, L);
    else if (gate == "x")    r = b.x(q, L);
    else if (gate == "y")    r = b.y(q, L);
    else if (gate == "z")    r = b.z(q, L);
    else if (gate == "s" || gate == "phase") r = b.s(q, L);
    else if (gate == "sdg")  r = b.sdg(q, L);
    else if (gate == "t")    r = b.t(q, L);
    else if (gate == "tdg")  r = b.tdg(q, L);
    else if (gate == "sx")   r = b.sx(q, L);
    else if (gate == "sxdg") r = b.sxdg(q, L);
    else if (gate == "reset") r = b.reset(q, L);
    else if (gate == "rx" || gate == "ry" || gate == "rz" ||
             gate == "gpi" || gate == "gpi2") {
      if (args.empty()) {
        err(gate + " expects an angle as first argument", aloc); return;
      }
      auto angle = foldReal(args[0]);
      if (!angle || !std::isfinite(*angle)) {
        err("rotation angle must fold at compile time", aloc); return;
      }
      if (gate == "rx") r = b.rx(*angle, q, L);
      else if (gate == "ry") r = b.ry(*angle, q, L);
      else if (gate == "rz") r = b.rz(*angle, q, L);
      else if (gate == "gpi")  r = b.gpi(*angle, q, L);
      else if (gate == "gpi2") r = b.gpi2(*angle, q, L);
    } else if (gate == "u1q") {
      if (args.size() < 2) {
        err("u1q expects (theta, phi, idx)", aloc); return;
      }
      auto th = foldReal(args[0]);
      auto ph = foldReal(args[1]);
      if (!th || !ph || !std::isfinite(*th) || !std::isfinite(*ph)) {
        err("u1q angles must fold at compile time", aloc); return;
      }
      r = b.u1q(*th, *ph, q, L);
    } else {
      err("unsupported 1q gate '" + gate + "'", aloc);
      return;
    }
    it->second[idx] = r;
  }

  // Apply a 2q gate to slot pair (idxA, idxB). Both slots updated.
  void apply2q(const std::string& reg, std::int64_t a, std::int64_t b_,
               const std::string& gate, const std::vector<ExprPtr>& args,
               const Location& aloc) {
    auto it = qslots.find(reg);
    if (it == qslots.end()) {
      err("unknown qreg '" + reg + "'", aloc); return;
    }
    auto& slots = it->second;
    if (a == b_) { err("two-qubit gate requires distinct qubits", aloc); return; }
    if (a < 0 || b_ < 0 ||
        a >= static_cast<std::int64_t>(slots.size()) ||
        b_ >= static_cast<std::int64_t>(slots.size())) {
      err("qubit index out of range", aloc); return;
    }
    pd::Location L = loc(aloc);
    std::pair<pd::ValueId, pd::ValueId> r;
    pd::ValueId qa = slots[a], qb = slots[b_];
    if(qa==pd::kInvalidValue||qb==pd::kInvalidValue){err("use of discarded qubit",aloc);return;}
    if      (gate == "cx" || gate == "cnot") r = b.cx(qa, qb, L);
    else if (gate == "cz")   r = b.cz(qa, qb, L);
    else if (gate == "swap") r = b.swap(qa, qb, L);
    else if (gate == "ecr")  r = b.ecr(qa, qb, L);
    else if (gate == "ms")   r = b.ms(qa, qb, L);
    else if (gate == "rzz" || gate == "rxx") {
      if (args.empty()) { err("rzz expects angle", aloc); return; }
      auto ang = foldReal(args[0]);
      if (!ang || !std::isfinite(*ang)) { err("two-qubit angle must fold to a finite value", aloc); return; }
      r = gate == "rxx" ? b.rxx(*ang, qa, qb, L) : b.rzz(*ang, qa, qb, L);
    } else {
      err("unsupported 2q gate '" + gate + "'", aloc); return;
    }
    slots[a] = r.first;
    slots[b_] = r.second;
  }

  void lowerStmt(const Stmt& s);
  void lowerBlock(const std::vector<StmtPtr>& body) {
    for (const auto& s : body) {
      if (fatal || returned) break;
      if(!s)continue;
      const bool bypass=std::exchange(bypassNextStatementGuard,false);
      std::optional<pd::ValueId> guard;
      if(!bypass)for(const auto& frame:loopFrames)if(frame.mayTransfer){
        auto ready=b.cmp("==",classicals.at(frame.done),b.constInt(0));
        guard=guard?b.binOp("&",*guard,ready):ready;
      }
      if(!bypass&&dynamicReturns&&functionMayReturn){
        auto ready=b.cmp("==",classicals.at(functionDone),b.constInt(0));guard=guard?b.binOp("&",*guard,ready):ready;
      }
      if(guard){bypassNextStatementGuard=true;runtimeBranch(*guard,{s},{},s->loc);}
      else lowerStmt(*s);
      if(!fatal&&out.numOps()>operationBudget)
        err("Photon program exceeds QSTACK_EXPANDED_OPERATION_BUDGET",s->loc);
    }
  }
  void lowerFunction(const Function& f);
};

void Lowerer::lowerStmt(const Stmt& s) {
  if (fatal) return;
  pd::Location L = loc(s.loc);
  if(runtimeDepth && s.kind==StmtKind::OutputStmt){
    err("runtime branches cannot export branch-local outputs",s.loc);return;
  }
  switch (s.kind) {
    case StmtKind::VarDecl: {
      if (runtimeDepth && s.decl_type.kind != TypeKind::Bit && s.decl_type.kind != TypeKind::UInt && s.decl_type.kind != TypeKind::QReg) {
        err("runtime branches cannot declare compile-time scalars",s.loc);return;
      }
      if(qslots.count(s.name)||classicals.count(s.name)||bslots.count(s.name)){err("variable is already declared: "+s.name,s.loc);return;}
      if (s.decl_type.kind == TypeKind::QReg) {
        std::uint32_t n = s.decl_type.qreg_size;
        if (n == 0) { err("QReg requires a size", s.loc); return; }
        if(out.numOps()>operationBudget||n>(operationBudget-out.numOps())/(runtimeDepth?1:2)){
          err("QReg allocation exceeds QSTACK_EXPANDED_OPERATION_BUDGET",s.loc);return;
        }
        std::vector<pd::ValueId> qs;
        qs.reserve(n);
        for (std::uint32_t i = 0; i < n; ++i) qs.push_back(b.allocQubit(L));
        qslots[s.name] = std::move(qs);
        quantumDeclarationDepth[s.name]=runtimeDepth;
        // Allocate parallel classical bit register so q.measure_int /
        // q.measure can write into it. Sized to match the QReg.
        std::vector<pd::ValueId> cs;
        cs.reserve(n);
        std::string bname = "__c_" + s.name;
        bitBase[s.name] = nextBit;
        for (std::uint32_t i = 0; i < n; ++i) cs.push_back(runtimeDepth?pd::kInvalidValue:b.allocBit(L));
        nextBit += n;
        bslots[bname] = std::move(cs);
      } else if (s.decl_type.kind == TypeKind::Int) {
        if (s.init) {
          auto v = foldInt(s.init);
          if (v) {
            intConsts[s.name] = *v;
            classicals[s.name] = b.constInt(*v, L);
            scalarTypes[s.name] = pd::intType();
          } else {
            auto value=expression(s.init);
            if(!fatal&&out.typeOf(value)==pd::bitType()){
              classicals[s.name]=b.copy(value,pd::bitType(),L);scalarTypes[s.name]=pd::bitType();
            }else if(!fatal)err("int initializer must be static or a measured Bit snapshot; use UInt<width> for runtime arithmetic",s.loc);
          }
        }
      } else if (s.decl_type.kind == TypeKind::Angle) {
        if (s.init) {
          auto v = foldReal(s.init);
          if (v) {
            angleConsts[s.name] = *v;
            classicals[s.name] = b.constAngle(*v, L);
            scalarTypes[s.name] = pd::angleType();
          }
          else err("angle initializer must fold to a literal", s.loc);
        }
      } else if (s.decl_type.kind == TypeKind::UInt) {
        if(!s.init){err("UInt declaration requires an initializer",s.loc);return;}
        const auto type=pd::uintType(s.decl_type.qreg_size);auto value=expression(s.init,type);if(fatal)return;
        if(out.typeOf(value)!=type){err("UInt initializer type differs; cast explicitly",s.loc);return;}
        classicals[s.name]=value;scalarTypes[s.name]=type;
      } else if (s.decl_type.kind == TypeKind::Bit) {
        if(s.decl_type.qreg_size){
          if(s.init){err("Bit registers use indexed assignments",s.loc);return;}
          if(s.decl_type.qreg_size>operationBudget||(!runtimeDepth&&
              (out.numOps()>operationBudget||s.decl_type.qreg_size>operationBudget-out.numOps()))){
            err("Bit allocation exceeds QSTACK_EXPANDED_OPERATION_BUDGET",s.loc);return;
          }
          auto& bits=bslots[s.name];bitBase[s.name]=nextBit;
          for(std::size_t i=0;i<s.decl_type.qreg_size;++i)bits.push_back(runtimeDepth?pd::kInvalidValue:b.allocBit(L));
          nextBit+=s.decl_type.qreg_size;break;
        }
        if(!s.init){err("Bit declaration requires an initializer",s.loc);return;}
        scalarTypes[s.name]=pd::bitType();
        if(s.init->kind!=ExprKind::Call){
          auto value=expression(s.init);if(fatal)return;
          auto constant=foldInt(s.init);
          if(out.typeOf(value)!=pd::bitType()&&(!constant||(*constant!=0&&*constant!=1))){err("Bit initializer must be Boolean or a measured Bit",s.loc);return;}
          classicals[s.name]=b.copy(value,pd::bitType(),L);break;
        }
        auto dot=s.init->text.find('.');auto name=s.init->text.substr(0,dot);
        if(dot==std::string::npos||s.init->text.substr(dot+1)!="measure"||!qslots.count(name)){
          err("Bit initializer must be q.measure(index)",s.loc);return;
        }
        auto& qs=qslots[name];std::optional<std::int64_t> index;
        if(s.init->children.size()==1)index=foldInt(s.init->children[0]);
        else if(s.init->children.empty()&&qs.size()==1)index=0;
        if(!index||*index<0||static_cast<std::size_t>(*index)>=qs.size()){
          err("measurement of a register requires an explicit valid bit index",s.loc);return;
        }
        if(qs[*index]==pd::kInvalidValue){err("measurement of discarded qubit",s.loc);return;}
        auto bit=b.measure(qs[*index],L);
        auto destination = bitBase[name] + *index;
        if (savedMeasurementBits.contains(destination)) destination = nextBit++;
        savedMeasurementBits.insert(destination);
        out.opMut(out.producerOf(bit)).attributes.push_back({"clbit",static_cast<double>(destination)});
        classicals[s.name]=bit;
        bslots["__c_"+name][*index]=bit;
      }
      break;
    }
    case StmtKind::GateCall: {
      const std::string& reg = s.receiver;
      const std::string& gate = s.method;
      // Determine index args. 1q gates take one index; 2q gates take
      // two; rotations take (angle, idx) or (theta, phi, idx) for u1q.
      // Identify qubit-index args by skipping leading angle args.
      std::size_t skip = 0;
      if (gate == "rx" || gate == "ry" || gate == "rz" ||
          gate == "gpi" || gate == "gpi2") skip = 1;
      else if (gate == "u1q") skip = 2;
      else if (gate == "rzz" || gate == "rxx") skip = 1;  // 2q with angle.

      // Number of qubit indices follows the gate's arity:
      auto needed = [&](const std::string& g) -> int {
        if (g == "cx" || g == "cnot" || g == "cz" || g == "swap" ||
            g == "ecr" || g == "ms" || g == "rzz" || g == "rxx") return 2;
        return 1;
      };
      int qa = needed(gate);
      if (s.args.size() != skip + static_cast<std::size_t>(qa)) {
        err("gate '" + gate + "' has the wrong number of arguments", s.loc); return;
      }
      auto idx0 = foldInt(s.args[skip]);
      if (!idx0) { err("qubit index must fold to a literal int", s.loc); return; }
      if (qa == 1) {
        std::vector<ExprPtr> rotArgs(s.args.begin(), s.args.begin() + skip);
        apply1q(reg, *idx0, gate, rotArgs, s.loc);
      } else {
        auto idx1 = foldInt(s.args[skip + 1]);
        if (!idx1) { err("qubit index must fold to a literal int", s.loc); return; }
        std::vector<ExprPtr> rotArgs(s.args.begin(), s.args.begin() + skip);
        apply2q(reg, *idx0, *idx1, gate, rotArgs, s.loc);
      }
      break;
    }
    case StmtKind::MeasureAll:
    case StmtKind::MeasureInt: {
      auto qit = qslots.find(s.receiver);
      if (qit == qslots.end()) {
        err("measure on unknown qreg '" + s.receiver + "'", s.loc); return;
      }
      std::string bname = "__c_" + s.receiver;
      auto bit = bslots.find(bname);
      auto& bits = bit->second;
      for (std::size_t i = 0; i < qit->second.size(); ++i) {
        if(qit->second[i]==pd::kInvalidValue){err("measurement of discarded qubit",s.loc);return;}
        bits[i] = b.measure(qit->second[i], L);
        const auto destination = savedMeasurementBits.contains(bitBase[s.receiver] + i)
            ? nextBit++ : bitBase[s.receiver] + i;
        out.opMut(out.producerOf(bits[i])).attributes.push_back(
            {"clbit", static_cast<double>(destination)});
      }
      // Note: M2 will use this measurement to compute return values
      // for `q.measure_int()`. M1 leaves the measure ops in the IR
      // for the type checker to validate.
      break;
    }
    case StmtKind::ReturnStmt: {
      returned = !dynamicReturns;
      returnForm=ReturnForm::Void;
      // Special-case: `return q.measure_int()` — emit per-slot measures,
      // pack them into an int, return that. This is the canonical
      // Photon return path (see Deep-Dive Part 1 §3 worked example).
      auto isMeasureIntCall = [](const Expr& e) -> std::optional<std::string> {
        if (e.kind != ExprKind::Call) return std::nullopt;
        // text is "<recv>.<method>".
        auto dot = e.text.find('.');
        if (dot == std::string::npos) return std::nullopt;
        if (e.text.substr(dot + 1) != "measure_int" &&
            e.text.substr(dot + 1) != "measure") return std::nullopt;
        return e.text.substr(0, dot);
      };
      if (s.init) {
        if (auto recv = isMeasureIntCall(*s.init)) {
          if (!s.init->children.empty()) {
            err("measurement-valued return expects no arguments", s.loc); return;
          }
          auto qit = qslots.find(*recv);
          if (qit == qslots.end()) {
            err("measure on unknown qreg '" + *recv + "'", s.loc); return;
          }
          if(!fixedMeasurementReturn)fixedMeasurementReturn=std::pair{bitBase[*recv],qit->second.size()};
          if(fixedMeasurementReturn->second!=qit->second.size()){err("measurement return paths require the same fixed width",s.loc);return;}
          returnForm=ReturnForm::Measurement;returnMeasurement=*fixedMeasurementReturn;
          std::vector<pd::ValueId> bits;
          for (auto qv : qit->second) {
            if(qv==pd::kInvalidValue){err("measurement return uses a discarded qubit",s.loc);return;}
            auto bit = b.measure(qv, L);
            out.opMut(out.producerOf(bit)).attributes.push_back(
                {"clbit", static_cast<double>(fixedMeasurementReturn->first + bits.size())});
            bits.push_back(bit);
          }
          // Skip phonon.return at top-level (flatten mode); it's only
          // valid inside a phonon.def body.
          if (in_def_) {
            err("measurement-valued returns from parameterized functions require bit-packing support", s.loc);
          }
          if(dynamicReturns)markDynamicReturn(s.loc);
          return;
        }
        auto value=expression(s.init,dynamicReturns?dynamicReturnType:std::nullopt);
        if(!fatal){
          if(dynamicReturns){
            if(!dynamicReturnType||out.typeOf(value)!=*dynamicReturnType){err("returned controller value requires the declared type; cast explicitly",s.loc);return;}
            classicals[functionValue]=b.copy(value,*dynamicReturnType,L);markDynamicReturn(s.loc);break;
          }
          if(out.typeOf(value)==pd::intType()){
            auto literal=foldInt(s.init);
            if(!literal||*literal<0){err("returned runtime integers require explicit UInt<width>",s.loc);return;}
            if(!in_def_)value=b.constUInt(static_cast<std::uint64_t>(*literal),64,L);
          }
          returnForm=ReturnForm::Classical;returnValue=value;
          if(in_def_&&!runtimeDepth)b.returnOp(std::span<const pd::ValueId>(&value,1),L);
        }
      } else if (in_def_) {
        b.returnOp({}, L);
      }
      if(dynamicReturns&&!s.init)markDynamicReturn(s.loc);
      break;
    }
    case StmtKind::ForLoop: {
      auto lo = foldInt(s.for_lo);
      auto save = intConsts.find(s.for_var) != intConsts.end()
                      ? std::make_optional(intConsts[s.for_var])
                      : std::nullopt;
      if (s.for_step && lo) intConsts[s.for_var] = *lo;
      auto hi = foldInt(s.for_hi);
      auto step = s.for_step ? foldInt(s.for_step) : std::optional<std::int64_t>{1};
      if (!lo || !hi || !step || *step == 0) {
        err("for-loop bounds and nonzero step must fold to literal ints", s.loc); return;
      }
      const bool descending = s.for_comparison == ">" || s.for_comparison == ">=";
      const bool inclusive = s.for_comparison == "<=" || s.for_comparison == ">=";
      if ((descending && *step > 0) || (!descending && *step < 0)) {
        err("for-loop step must progress toward its bound", s.loc); return;
      }
      auto continues = [&](std::int64_t value) {
        return descending ? (inclusive ? value >= *hi : value > *hi)
                          : (inclusive ? value <= *hi : value < *hi);
      };
      // Expand before building SSA: every iteration resolves its own indices
      // and angles, and consumes the previous iteration's qubit values.
      for (auto i = *lo; !fatal && !returned;) {
        intConsts[s.for_var] = i;
        if (s.for_step) {
          hi = foldInt(s.for_hi);
          if (!hi) { err("for-loop continuation must remain compile-time integral", s.loc); break; }
        }
        if (!continues(i)) break;
        if (++expandedIterations > 100000) {
          err("static loop expansion exceeds 100000 iterations", s.loc);
          break;
        }
        lowerBlock(s.body);
        if (fatal || returned) break;
        // The C++ counted-loop form may update its induction variable in the
        // body; the increment applies to that current value.
        const auto current = s.for_step ? intConsts.at(s.for_var) : i;
        if (s.for_step) {
          step = foldInt(s.for_step);
          if (!step || *step == 0 || (descending ? *step > 0 : *step < 0)) {
            err("for-loop increment must remain a nonzero progressing compile-time integer", s.loc); break;
          }
        }
        if ((*step > 0 && current > std::numeric_limits<std::int64_t>::max() - *step) ||
            (*step < 0 && current < std::numeric_limits<std::int64_t>::min() - *step)) {
          err("for-loop induction variable overflow", s.loc); break;
        }
        i = current + *step;
      }
      if (save) intConsts[s.for_var] = *save;
      else intConsts.erase(s.for_var);
      break;
    }
    case StmtKind::WhileLoop: {
      const auto bound=foldInt(s.for_hi);
      const auto budget=operationBudget;
      if(!bound||*bound<=0||static_cast<std::uint64_t>(*bound)>budget){err("max_iterations must be positive and within the expanded operation budget",s.loc);return;}
      const auto loop=boundedLoops++;
      const auto live="__photon_loop_live_"+std::to_string(loop),done="__photon_loop_done_"+std::to_string(loop);
      classicals[live]=b.copy(b.constInt(1,L),pd::bitType(),L);scalarTypes[live]=pd::bitType();
      classicals[done]=b.copy(b.constInt(0,L),pd::bitType(),L);scalarTypes[done]=pd::bitType();
      loopFrames.push_back({live,done,false});
      for(std::int64_t i=0;i<*bound&&!fatal;++i){
        if(out.numOps()>budget){err("bounded while exceeds QSTACK_EXPANDED_OPERATION_BUDGET",s.loc);return;}
        auto pred=predicate(s.predicate,true);if(fatal)return;
        pred=b.binOp("&",pred,classicals.at(live),L);
        classicals[done]=b.copy(b.constInt(0,L),pd::bitType(),L);loopFrames.back().mayTransfer=false;
        ++boundedLoopDepth;runtimeBranch(pred,s.body,{},s.loc);--boundedLoopDepth;
      }
      if(fatal)return;
      auto exhausted=predicate(s.predicate,true);if(fatal)return;
      exhausted=b.binOp("&",exhausted,classicals.at(live),L);
      b.output("loop_exhausted_"+std::to_string(loop),exhausted,"loop_exhausted",L);
      loopFrames.pop_back();classicals.erase(live);classicals.erase(done);scalarTypes.erase(live);scalarTypes.erase(done);
      if(out.numOps()>budget)err("bounded while exceeds QSTACK_EXPANDED_OPERATION_BUDGET",s.loc);
      break;
    }
    case StmtKind::BreakStmt:case StmtKind::ContinueStmt: {
      if(loopFrames.empty()){err("break/continue requires a bounded runtime loop",s.loc);return;}
      auto& frame=loopFrames.back();
      if(s.kind==StmtKind::BreakStmt)classicals[frame.live]=b.copy(b.constInt(0,L),pd::bitType(),L);
      classicals[frame.done]=b.copy(b.constInt(1,L),pd::bitType(),L);frame.mayTransfer=true;break;
    }
    case StmtKind::DiscardStmt: {
      auto reg=qslots.find(s.receiver);
      if(reg==qslots.end()){err("discard requires a live quantum register",s.loc);return;}
      if(runtimeDepth&&quantumDeclarationDepth[s.receiver]<runtimeDepth){err("conditional discard cannot change outer-scope quantum ownership",s.loc);return;}
      std::vector<std::size_t> indices;
      if(s.args.empty()){for(std::size_t i=0;i<reg->second.size();++i)indices.push_back(i);}
      else {auto index=foldInt(s.args[0]);if(!index||*index<0||static_cast<std::size_t>(*index)>=reg->second.size()){err("discard index must be static and in range",s.loc);return;}indices.push_back(*index);}
      for(auto index:indices){auto& value=reg->second[index];if(value==pd::kInvalidValue){err("qubit was already discarded",s.loc);return;}b.discard(value,L);value=pd::kInvalidValue;}
      if(std::all_of(reg->second.begin(),reg->second.end(),[](auto value){return value==pd::kInvalidValue;})){
        qslots.erase(reg);bslots.erase("__c_"+s.receiver);bitBase.erase(s.receiver);quantumDeclarationDepth.erase(s.receiver);
      }
      break;
    }
    case StmtKind::IfStmt: {
      if (!s.predicate) { err("if missing predicate", s.loc); return; }
      if(auto taken=foldBool(s.predicate))lowerBlock(*taken?s.then_body:s.else_body);
      else {auto pred=predicate(s.predicate);if(!fatal)runtimeBranch(pred,s.then_body,s.else_body,s.loc);}
      break;
    }
    case StmtKind::Assign: {
      if (s.init) {
        if(!s.args.empty()){
          auto i=foldInt(s.args[0]);auto entry=bslots.find(s.name);
          if(!i||*i<0||entry==bslots.end()||static_cast<std::size_t>(*i)>=entry->second.size()){
            err("bit assignment requires a declared register and static in-range index",s.loc);return;}
          auto value=expression(s.init,pd::bitType(),true);if(fatal)return;
          if(out.typeOf(value)!=pd::bitType()){err("bit register assignment requires a Boolean value",s.loc);return;}
          auto& operation=out.opMut(out.producerOf(value));
          if(operation.kind==pd::OpKind::Measure){
            for(auto& attr:operation.attributes)if(attr.name=="clbit")attr.value=static_cast<double>(bitBase[s.name]+*i);
            entry->second[*i]=value;
          }else{err("Bit register writes require a measurement; use scalar Bit for controller assignments",s.loc);return;}
          break;
        }
        if(scalarTypes.count(s.name)&&(scalarTypes[s.name].kind==pd::TypeKind::UInt||scalarTypes[s.name].kind==pd::TypeKind::Bit)){
          const auto type=scalarTypes[s.name];auto value=expression(s.init,type,true);if(fatal)return;
          auto constant=foldInt(s.init);
          if(out.typeOf(value)!=type&&!(type==pd::bitType()&&constant&&(*constant==0||*constant==1))){err("assignment changes controller type; cast explicitly",s.loc);return;}
          classicals[s.name]=b.copy(value,type,L);break;
        }
        if(runtimeDepth){err("runtime branch cannot change a compile-time scalar",s.loc);return;}
        auto v = foldInt(s.init);
        if (angleConsts.count(s.name)) {
          auto angle = foldReal(s.init);
          if (!angle) { err("angle assignment must fold at compile time", s.loc); return; }
          angleConsts[s.name] = *angle;
          classicals[s.name] = b.constAngle(*angle, L);
        } else if (v && intConsts.count(s.name)) {
          intConsts[s.name] = *v;
          classicals[s.name] = b.constInt(*v, L);
        } else {
          err("assignment requires a declared compile-time scalar", s.loc);
        }
      }
      break;
    }
    case StmtKind::LibCall: {
      if(qslots.count(s.receiver)&&std::find(qslots[s.receiver].begin(),qslots[s.receiver].end(),pd::kInvalidValue)!=qslots[s.receiver].end()){
        err("library call requires a fully live quantum register",s.loc);return;
      }
      // Try the library expander first. If the routine matches, the
      // expander emits the right Phonon ops on `s.receiver`'s slots.
      ExpandCtx ctx;
      ctx.qslots = &qslots;
      ctx.builder = &b;
      ctx.loc = L;
      ctx.foldInt = [this](const ExprPtr& e) { return foldInt(e); };
      ctx.foldReal = [this](const ExprPtr& e) { return foldReal(e); };
      ctx.diag = &diag;
      if (expandLibrary(s.method, s.receiver, s.args, s.loc, ctx)) break;
      err("unsupported library routine '" + s.method + "'", s.loc);
      break;
    }
    case StmtKind::ExprStmt:
      err("unsupported expression statement", s.loc);
      break;
    case StmtKind::OutputStmt: {
      auto value=expression(s.init);if(!fatal)b.output(s.name,value,"value",L);break;
    }
  }
}

void Lowerer::lowerFunction(const Function& f) {
  returned = false;
  returnForm=ReturnForm::None;returnValue=pd::kInvalidValue;
  fixedMeasurementReturn.reset();dynamicReturns=false;functionMayReturn=false;
  pd::Location L = loc(f.loc);
  bool flatten = f.is_kernel && f.params.empty();
  if (flatten) {
    qslots.clear();
    quantumDeclarationDepth.clear();
    bslots.clear();
    classicals.clear();
    scalarTypes.clear();
    intConsts.clear();
    angleConsts.clear();
    in_def_ = false;
    prepareDynamicReturns(f);if(fatal)return;
    if(dynamicReturns){
      lowerBlock(f.body);
      if(!fatal&&dynamicReturnType)b.output("return",classicals.at(functionValue),"value",L);
      return;
    }
    auto sourceBody=f.body;
    auto implicit=std::make_shared<Stmt>();implicit->kind=StmtKind::ReturnStmt;implicit->loc=f.loc;sourceBody.push_back(implicit);
    lowerBlock(normalizeReturns(sourceBody));
    if(!fatal&&returnForm==ReturnForm::Classical)b.output("return",returnValue,"value",L);
    return;
  }
  in_def_ = true;
  // Translate Photon parameters into Phonon Builder::Param. QReg
  // parameters become a phonon `qubit` parameter (size lost; the
  // caller's QReg slot table threads the values).
  std::vector<pd::Builder::Param> params;
  for (const auto& p : f.params) {
    pd::Builder::Param pp;
    pp.name = p.name;
    switch (p.type.kind) {
      case TypeKind::Int:    pp.type = pd::intType();   break;
      case TypeKind::Angle:  pp.type = pd::angleType(); break;
      case TypeKind::Bit:    pp.type = pd::bitType();   break;
      case TypeKind::UInt:   pp.type = pd::uintType(p.type.qreg_size); break;
      case TypeKind::QReg:   pp.type = pd::qubitType(); break;
      case TypeKind::Oracle: pp.type = pd::funcType();  break;
      default:               pp.type = pd::funcType();  break;
    }
    params.push_back(std::move(pp));
  }
  auto def = b.beginDef(f.name, params, L);
  // Bind parameter values into the local tables.
  qslots.clear();
  quantumDeclarationDepth.clear();
  bslots.clear();
  classicals.clear();
  scalarTypes.clear();
  intConsts.clear();
  angleConsts.clear();
  for (std::size_t i = 0; i < f.params.size(); ++i) {
    pd::ValueId v = b.paramValue(def, i);
    if (f.params[i].type.kind == TypeKind::QReg) {
      qslots[f.params[i].name] = {v};
    } else {
      classicals[f.params[i].name] = v;
      scalarTypes[f.params[i].name] = params[i].type;
    }
  }
  lowerBlock(normalizeReturns(f.body));
  if(!fatal&&returnForm==ReturnForm::Classical&&out.op(out.producerOf(returnValue)).kind==pd::OpKind::Select)
    b.returnOp(std::span<const pd::ValueId>(&returnValue,1),L);
  b.endDef(def, L);
  in_def_ = false;
}

}  // namespace (anonymous)

LowerResult lowerToPhonon(const Module& m) {
  Lowerer L(m);
  for (const auto& f : m.functions) {
    if (L.fatal) break;
    L.lowerFunction(f);
    if(!L.fatal&&L.out.numOps()>L.operationBudget)
      L.err("Photon program exceeds QSTACK_EXPANDED_OPERATION_BUDGET",f.loc);
    if (L.fatal) break;
  }
  LowerResult r;
  r.diag = std::move(L.diag);
  bool ok = true;
  for (const auto& d : r.diag.items()) {
    if (d.severity == DiagSeverity::Error) { ok = false; break; }
  }
  if (ok) r.module = std::move(L.out);
  return r;
}

}  // namespace photon::lang

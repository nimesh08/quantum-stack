// phonon/parser/cpp/lib/Parser.cpp
//
// Hand-written recursive-descent Phonon parser.
//
// Strict superset of the Spinor grammar — every legal `.spn` file
// is a legal Phonon program. Adds: int/angle declarations, classical
// expressions, if/else, for, while, def/call/return, assign.
//
// The parser threads quantum-register slot bindings through the
// statement stream Spinor-style (each gate write updates the slot's
// current ValueId), so the produced Phonon module is already in SSA
// shape and ready for the linear type checker (M3).

#include "phonon/parser/Parser.h"
#include "Lexer.h"
#include "spinor/dialect/ExactInteger.h"

#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <numbers>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace phonon::parser {

namespace pd = phonon::dialect;

namespace {

struct Parser {
  std::vector<Token> toks;
  std::size_t pos = 0;
  std::string filename;

  pd::Module mod;
  pd::Builder b;
  pd::Diagnostics diag;
  bool fatal = false;

  // Captures and scalar register parameters refer to the same mutable SSA
  // slot. Copying a scope keeps those references, while rebinding a local
  // name creates a fresh register and cannot replace the caller's binding.
  struct Slot {
    std::shared_ptr<pd::ValueId> value;
    explicit Slot(pd::ValueId v) : value(std::make_shared<pd::ValueId>(v)) {}
    operator pd::ValueId() const { return *value; }
    Slot& operator=(pd::ValueId v) { *value = v; return *this; }
  };
  struct Register {
    std::vector<Slot> slots;
    Register() = default;
    Register(std::initializer_list<pd::ValueId> values) {
      for (auto value : values) slots.emplace_back(value);
    }
    Register(std::vector<pd::ValueId> values) {
      for (auto value : values) slots.emplace_back(value);
    }
    std::size_t size() const { return slots.size(); }
    Slot& operator[](std::size_t i) { return slots[i]; }
    pd::ValueId operator[](std::size_t i) const { return slots[i]; }
    auto begin() const { return slots.begin(); }
    auto end() const { return slots.end(); }
    Register clone() const {
      Register copy;
      for (const auto& slot : slots) copy.slots.emplace_back(pd::ValueId(slot));
      return copy;
    }
    static Register alias(const Register& source, std::size_t i) {
      Register result;
      result.slots.push_back(source.slots[i]);
      return result;
    }
  };
  using Registers = std::unordered_map<std::string, Register>;
  Registers qreg;
  std::unordered_map<std::string,std::size_t> quantumScope;
  std::vector<pd::ValueId> reusableQubits;
  Registers creg;
  std::unordered_map<std::string, pd::ValueId> classicals;  // int/angle
  std::unordered_map<std::string,pd::Type> scalarTypes;
  std::size_t operationBudget=100000;
  std::optional<unsigned> unsignedLiteralWidth;
  // Classical scalars whose compile-time value is known (used for
  // for-loop bound resolution and qubit register sizes).
  std::unordered_map<std::string, double> ctConst;
  std::optional<std::int64_t> integerValue(pd::ValueId value) const {
    const auto& op=mod.op(mod.producerOf(value));
    if(op.kind==pd::OpKind::ConstInt){
      for(const auto& attr:op.attributes)if(attr.name=="value"){
        if(const auto* exact=std::get_if<std::int64_t>(&attr.value))return *exact;
        if(const auto* old=std::get_if<double>(&attr.value);old&&std::isfinite(*old)&&std::floor(*old)==*old&&std::abs(*old)<=9007199254740991.0)return static_cast<std::int64_t>(*old);
      }
    }
    if(op.kind==pd::OpKind::BinOp&&op.operands.size()==2){
      const auto a=integerValue(op.operands[0]),b_=integerValue(op.operands[1]);
      if(a&&b_)for(const auto& attr:op.attributes)if(attr.name=="op"){
        const auto& symbol=std::get<std::string>(attr.value);
        if(symbol=="/"&&*b_&& !(*a==std::numeric_limits<std::int64_t>::min()&&*b_==-1)&&*a%*b_!=0)return std::nullopt;
        return spinor::dialect::checkedInteger(symbol,*a,*b_);
      }
    }
    return std::nullopt;
  }
  struct ScalarBinding {
    pd::ValueId value;
    std::optional<double> constant;
  };
  using ScalarBindings = std::unordered_map<std::string, std::shared_ptr<ScalarBinding>>;
  ScalarBindings scalarBindings;
  void recordScalar(const std::string& name, bool declaration = false) {
    if (!classicals.count(name)) { scalarBindings.erase(name); return; }
    if (declaration || !scalarBindings.count(name))
      scalarBindings[name] = std::make_shared<ScalarBinding>();
    auto& binding = *scalarBindings.at(name);
    binding.value = classicals.at(name);
    binding.constant = ctConst.count(name) ? std::optional<double>{ctConst.at(name)} : std::nullopt;
  }
  std::unordered_map<std::string, std::vector<std::size_t>> bitTargets;
  std::size_t nextBit = 0;
  std::size_t expandedIterations = 0;
  std::size_t expandedCalls = 0;
  std::size_t runtimeDepth = 0;
  struct LoopFrame {std::string live,done;bool mayTransfer=false;std::size_t functionDepth=0;};
  std::vector<LoopFrame> loopFrames;
  std::size_t generatedName=0;
  std::size_t flowEpoch=0;
  std::string freshName(std::string stem){std::string name;do{name="__qstack_"+stem+"_"+std::to_string(generatedName++);}while(classicals.count(name)||qreg.count(name)||creg.count(name));return name;}

  // Numeric parameters must be bound before register indices and static
  // loops are resolved. Retain these bodies as lexical source templates;
  // qubit-only helpers without captures keep structured Def/Call IR.
  struct FuncDecl {
    std::string name;
    std::vector<pd::Builder::Param> params;
    std::size_t body_start = 0;  // index of `{`
    std::size_t body_end = 0;    // index of `}`
    bool specialize = false;
    bool normalizeReturns = false;
    Registers capturedQreg;
    Registers capturedCreg;
    ScalarBindings capturedScalars;
    std::unordered_map<std::string, std::vector<std::size_t>> capturedBitTargets;
  };
  std::unordered_map<std::string, FuncDecl> funcs;
  struct InlineFrame {
    std::string name;
    std::size_t runtimeDepth;
    bool returned = false;
    std::vector<pd::ValueId> values;
    bool normalizeReturns = false;
    bool mayReturn = false;
    std::string done;
    std::vector<std::string> quantumParams;
  };
  std::vector<InlineFrame> inlineFrames;
  bool inlineReturned() const { return !inlineFrames.empty() && inlineFrames.back().returned; }

  Parser(std::vector<Token> ts, std::string fn)
      : toks(std::move(ts)), filename(std::move(fn)), b(mod) {
    if(const char* budget=std::getenv("QSTACK_EXPANDED_OPERATION_BUDGET")){
      try{
        operationBudget=spinor::dialect::parseExactInteger<std::size_t>(budget);
        if(!operationBudget)throw std::invalid_argument("expanded operation budget must be positive");
      }catch(const std::exception& error){err(std::string("invalid expanded operation budget: ")+error.what());fatal=true;}
    }
  }

  // --- Token helpers -----------------------------------------------------

  const Token& peek(std::size_t k = 0) const {
    return toks[std::min(pos + k, toks.size() - 1)];
  }
  const Token& cur() const { return peek(0); }
  Token consume() {
    Token t = toks[pos];
    if (pos + 1 < toks.size()) ++pos;
    return t;
  }
  bool accept(Tok k) {
    if (cur().kind == k) { consume(); return true; }
    return false;
  }
  bool expect(Tok k, const char* what) {
    if (cur().kind == k) { consume(); return true; }
    err(std::string("expected ") + what + ", got '" + cur().text + "'");
    return false;
  }
  void skipNewlines() {
    while (cur().kind == Tok::Newline) consume();
  }
  void err(std::string msg) {
    pd::Location loc{filename, cur().line, cur().column};
    diag.error(std::move(msg), loc);
  }

  // --- Compile-time integer evaluator ------------------------------------
  // Returns nullopt if the expression cannot be folded.
  std::optional<double> foldExpr();
  std::optional<double> foldTerm();
  std::optional<double> foldFactor();
  std::optional<std::int64_t> foldIntegerExpr();
  std::optional<std::int64_t> foldIntegerTerm();
  std::optional<std::int64_t> foldIntegerFactor();

  // --- Parse-time expression to ValueId ----------------------------------
  pd::ValueId parseExpr();
  pd::ValueId parseBinary(int minimum);
  pd::ValueId parseTerm();
  pd::ValueId parseFactor();
  pd::ValueId parsePredicate();
  pd::ValueId coerce(pd::ValueId value,pd::Type type);
  void parseTypedDecl();
  void joinClassicals(pd::ValueId predicate,const std::unordered_map<std::string,pd::ValueId>& before,const std::unordered_map<std::string,pd::ValueId>& thenValues,const std::unordered_map<std::string,pd::ValueId>& elseValues);
  using BitBindings=std::unordered_map<std::string,std::vector<pd::ValueId>>;
  BitBindings snapshotBits()const {BitBindings result;for(const auto& [name,reg]:creg)for(auto value:reg)result[name].push_back(value);return result;}
  void restoreBits(const BitBindings& values){for(const auto& [name,bits]:values)for(std::size_t bit=0;bit<bits.size();++bit)creg.at(name)[bit]=bits[bit];}
  void joinBits(pd::ValueId predicate,const BitBindings& before,const BitBindings& yes,const BitBindings& no){
    for(const auto& [name,bits]:before)for(std::size_t bit=0;bit<bits.size();++bit){
      auto a=yes.at(name).at(bit),b_=no.at(name).at(bit);
      if(a==b_)creg.at(name)[bit]=a;
      else{auto joined=b.select(predicate,a,b_);mod.opMut(mod.producerOf(joined)).attributes.push_back({"mutable_clbit",static_cast<double>(bitTargets.at(name).at(bit))});creg.at(name)[bit]=joined;}
    }
  }
  void parseBoundedWhile();
  void parseTransfer(bool breaking){
    consume();if(loopFrames.empty()||loopFrames.back().functionDepth!=inlineFrames.size()){err("break/continue requires a bounded runtime loop in this function");return;}
    auto& loop=loopFrames.back();
    if(breaking){classicals[loop.live]=b.copy(b.constInt(0),pd::bitType());recordScalar(loop.live);}
    classicals[loop.done]=b.copy(b.constInt(1),pd::bitType());recordScalar(loop.done);loop.mayTransfer=true;++flowEpoch;
  }
  void parseDiscard(){
    consume();const auto reference=parseQubitRef();if(!reference)return;
    const auto& [name,index]=*reference;
    if(!qreg.count(name)){err("discard requires a live quantum register");return;}
    if(runtimeDepth&&quantumScope[name]<runtimeDepth){err("conditional discard of an outer quantum binding requires matching ownership on every branch; discard a branch-local register or reset explicitly");return;}
    const auto count=qreg.at(name).size();
    for(std::size_t slot=0;slot<count;++slot)if(index<0||slot==static_cast<std::size_t>(index)){
      auto value=getQubitSlot(name,static_cast<int>(slot),cur());if(value==pd::kInvalidValue)return;
      reusableQubits.push_back(b.reset(value));qreg.at(name)[slot]=pd::kInvalidValue;
    }
  }

  // --- Top-level ---------------------------------------------------------
  void parseProgram();

  // header
  void parseHeader();

  // statements
  void parseStmt();
  void parseDeclQubit();
  void parseDeclBit();
  void parseDeclClassical(bool isAngle);
  void parseGateStmt();
  void parseMeasureAssign(const std::string& lhsName,
                          std::optional<int> lhsIdx);
  void parseResetStmt();
  void parseBarrierStmt();
  void parseIfStmt();
  void parseForStmt();
  void parseWhileStmt();
  void parseDefStmt();
  void parseCallStmt(const std::string& name);
  void parseAssignStmt(const std::string& name);
  void parseReturnStmt();
  void parseBlock();  // expects '{', parses stmts, expects '}'
  void skipBlock();

  // helpers
  std::optional<std::pair<std::string, int>> parseQubitRef();
  pd::ValueId getQubitSlot(const std::string& name, int idx,
                           const Token& at);
  void setQubitSlot(const std::string& name, int idx, pd::ValueId v);
  pd::ValueId getBitSlot(const std::string& name, int idx,
                         const Token& at);
  void setBitSlot(const std::string& name, int idx, pd::ValueId v);
};

// --- Compile-time fold ----------------------------------------------------

std::optional<std::int64_t> Parser::foldIntegerFactor() {
  const auto start=pos;
  if(accept(Tok::Minus)){
    if(cur().kind==Tok::Integer&&cur().text=="9223372036854775808"){
      consume();return std::numeric_limits<std::int64_t>::min();
    }
    if(auto value=foldIntegerFactor())return spinor::dialect::checkedInteger("-",0,*value);
  }else if(cur().kind==Tok::Integer){
    const auto value=spinor::dialect::parseExactInteger<std::uint64_t>(cur().text);
    if(value<=static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())){consume();return static_cast<std::int64_t>(value);}
  }else if(accept(Tok::LParen)){
    const auto value=foldIntegerExpr();if(value&&accept(Tok::RParen))return value;
  }else if(cur().kind==Tok::Identifier){
    const auto found=classicals.find(cur().text);
    if(found!=classicals.end())if(auto value=integerValue(found->second)){consume();return value;}
  }
  pos=start;return std::nullopt;
}
std::optional<std::int64_t> Parser::foldIntegerTerm() {
  const auto start=pos;auto value=foldIntegerFactor();
  if(!value)return std::nullopt;
  while(cur().kind==Tok::Star||cur().kind==Tok::Slash){
    const auto operation=consume().text;const auto rhs=foldIntegerFactor();
    if(!rhs){pos=start;return std::nullopt;}
    // Nonintegral division belongs to the angle evaluator; exact integer
    // overflow and division by zero remain diagnostics, never float fallback.
    if(operation=="/"&&*rhs&& !(*value==std::numeric_limits<std::int64_t>::min()&&*rhs==-1)&&*value%*rhs!=0){pos=start;return std::nullopt;}
    value=spinor::dialect::checkedInteger(operation,*value,*rhs);
  }
  return value;
}
std::optional<std::int64_t> Parser::foldIntegerExpr() {
  const auto start=pos;auto value=foldIntegerTerm();
  if(!value)return std::nullopt;
  while(cur().kind==Tok::Plus||cur().kind==Tok::Minus){
    const auto operation=consume().text;const auto rhs=foldIntegerTerm();
    if(!rhs){pos=start;return std::nullopt;}
    value=spinor::dialect::checkedInteger(operation,*value,*rhs);
  }
  return value;
}

std::optional<double> Parser::foldFactor() {
  // Snapshot pos so we can rewind if we cannot fold.
  std::size_t save = pos;
  if (cur().kind == Tok::Minus) {
    consume();
    auto v = foldFactor();
    if (!v) { pos = save; return std::nullopt; }
    return -*v;
  }
  if (cur().kind == Tok::Pi) {
    consume();
    return std::numbers::pi;
  }
  if (cur().kind == Tok::Integer) {
    return std::stod(consume().text);
  }
  if (cur().kind == Tok::Real) {
    return std::stod(consume().text);
  }
  if (cur().kind == Tok::LParen) {
    consume();
    auto v = foldExpr();
    if (!v || !accept(Tok::RParen)) { pos = save; return std::nullopt; }
    return v;
  }
  if (cur().kind == Tok::Identifier) {
    auto it = ctConst.find(cur().text);
    if (it != ctConst.end()) {
      consume();
      return it->second;
    }
  }
  pos = save;
  return std::nullopt;
}

std::optional<double> Parser::foldTerm() {
  auto a = foldFactor();
  if (!a) return std::nullopt;
  while (cur().kind == Tok::Star || cur().kind == Tok::Slash) {
    Tok op = cur().kind; consume();
    auto b_ = foldFactor();
    if (!b_) return std::nullopt;
    if (op == Tok::Slash && *b_ == 0) { err("division by zero"); return std::nullopt; }
    if (op == Tok::Star) a = *a * *b_; else a = *a / *b_;
  }
  return a;
}

std::optional<double> Parser::foldExpr() {
  const auto start=pos;
  if(auto exact=foldIntegerExpr())return static_cast<double>(*exact);
  pos=start;
  auto a = foldTerm();
  if (!a) return std::nullopt;
  while (cur().kind == Tok::Plus || cur().kind == Tok::Minus) {
    Tok op = cur().kind; consume();
    auto b_ = foldTerm();
    if (!b_) return std::nullopt;
    if (op == Tok::Plus) a = *a + *b_; else a = *a - *b_;
  }
  return a;
}

// --- Parse-time expression -----------------------------------------------
//
// Returns a ValueId of !phonon.int, !phonon.angle, or !spinor.bit.
// A literal int produces phonon.const_int; identifiers are looked up
// in `classicals` or quantum-register slots (the latter is only valid
// in a `cmp` predicate context, but the parser is permissive — the
// type checker will reject misuse).
pd::ValueId Parser::parseFactor() {
  if(accept(Tok::Tilde)){
    auto value=parseFactor();if(mod.typeOf(value).kind!=pd::TypeKind::UInt){err("bitwise complement requires an explicit uint[width] value");return value;}return b.bitNot(value);
  }
  if(accept(Tok::Bang)){
    auto value=parseFactor();return b.cmp("==",value,b.constInt(0));
  }
  if(cur().kind==Tok::UInt){
    consume();expect(Tok::LBracket,"'['");const auto width=foldExpr();expect(Tok::RBracket,"']'");expect(Tok::LParen,"'('");
    if(!width||*width<1||*width>64||std::floor(*width)!=*width){err("uint width must be an integer in [1,64]");return b.constInt(0);}
    pd::ValueId value;
    if(cur().kind==Tok::Integer&&peek(1).kind==Tok::RParen)value=b.constUInt(spinor::dialect::parseExactInteger<std::uint64_t>(consume().text),static_cast<unsigned>(*width));
    else value=coerce(parsePredicate(),pd::uintType(static_cast<unsigned>(*width)));
    expect(Tok::RParen,"')'");return value;
  }
  if (cur().kind == Tok::Minus) {
    consume();
    if(cur().kind==Tok::Integer&&cur().text=="9223372036854775808"){
      consume();return b.constInt(std::numeric_limits<std::int64_t>::min());
    }
    pd::ValueId v = parseFactor();
    pd::ValueId zero = b.constInt(0);
    return b.binOp("-", zero, v);
  }
  if (cur().kind == Tok::Pi) {
    consume();
    return b.constAngle(std::numbers::pi);
  }
  if (cur().kind == Tok::Integer) {
    if(unsignedLiteralWidth)return b.constUInt(spinor::dialect::parseExactInteger<std::uint64_t>(consume().text),*unsignedLiteralWidth);
    return b.constInt(spinor::dialect::parseExactInteger<std::int64_t>(consume().text));
  }
  if (cur().kind == Tok::Real) {
    return b.constAngle(std::stod(consume().text));
  }
  if (cur().kind == Tok::LParen) {
    consume();
    pd::ValueId v = parsePredicate();
    expect(Tok::RParen, "')'");
    return v;
  }
  if (cur().kind == Tok::Identifier) {
    std::string name = consume().text;
    // Indexed reference: name[expr]
    if (cur().kind == Tok::LBracket) {
      consume();
      auto idxOpt = foldExpr();
      expect(Tok::RBracket, "']'");
      if (!idxOpt || !std::isfinite(*idxOpt) || std::floor(*idxOpt) != *idxOpt ||
          *idxOpt < 0 || *idxOpt > std::numeric_limits<int>::max()) {
        err("array index must be a compile-time integer");
        return b.constInt(0);
      }
      int idx = static_cast<int>(*idxOpt);
      auto qit = qreg.find(name);
      if (qit != qreg.end()) {
        return getQubitSlot(name, idx, cur());
      }
      auto cit = creg.find(name);
      if (cit != creg.end()) {
        return getBitSlot(name, idx, cur());
      }
      err("unknown indexed identifier: " + name);
      return b.constInt(0);
    }
    auto it = classicals.find(name);
    if (it != classicals.end()) return it->second;
    // Try quantum register single-slot (size 1).
    auto qit = qreg.find(name);
    if (qit != qreg.end() && qit->second.size() == 1) {
      return qit->second[0];
    }
    auto cit = creg.find(name);
    if (cit != creg.end() && cit->second.size() == 1) return cit->second[0];
    err("unknown identifier: " + name);
    return b.constInt(0);
  }
  err("expected expression");
  return b.constInt(0);
}

pd::ValueId Parser::parseTerm() {
  pd::ValueId a = parseFactor();
  while (cur().kind == Tok::Star || cur().kind == Tok::Slash) {
    std::string op = cur().kind == Tok::Star ? "*" : "/";
    consume();
    pd::ValueId b_ = parseFactor();
    a = b.binOp(op, a, b_);
  }
  return a;
}

pd::ValueId Parser::parseExpr(){return parseBinary(1);}
pd::ValueId Parser::parseBinary(int minimum){
  auto lhs=parseFactor();
  auto precedence=[](Tok token){switch(token){case Tok::Pipe:return 1;case Tok::Caret:return 2;case Tok::Amp:return 3;case Tok::Shl:case Tok::Shr:return 4;case Tok::Plus:case Tok::Minus:return 5;case Tok::Star:case Tok::Slash:return 6;default:return 0;}};
  while(precedence(cur().kind)>=minimum){
    const auto level=precedence(cur().kind);if(!level)break;const auto symbol=consume().text;
    const auto oldWidth=unsignedLiteralWidth;if(mod.typeOf(lhs).kind==pd::TypeKind::UInt)unsignedLiteralWidth=mod.typeOf(lhs).width;
    auto rhs=parseBinary(level+1);unsignedLiteralWidth=oldWidth;
    lhs=b.binOp(symbol,lhs,rhs);
  }
  return lhs;
}

pd::ValueId Parser::parsePredicate(){
  auto value=parseExpr();
  if(cur().kind==Tok::EqEq||cur().kind==Tok::NotEq||cur().kind==Tok::Lt||cur().kind==Tok::Gt||cur().kind==Tok::Le||cur().kind==Tok::Ge){
    auto symbol=consume().text;const auto oldWidth=unsignedLiteralWidth;if(mod.typeOf(value).kind==pd::TypeKind::UInt)unsignedLiteralWidth=mod.typeOf(value).width;
    auto rhs=parseExpr();unsignedLiteralWidth=oldWidth;
    if(mod.typeOf(value).kind==pd::TypeKind::UInt&&mod.typeOf(rhs).kind==pd::TypeKind::Int)rhs=coerce(rhs,mod.typeOf(value));
    value=b.cmp(symbol,value,rhs);
  }
  return value;
}
pd::ValueId Parser::coerce(pd::ValueId value,pd::Type type){
  if(type.kind==pd::TypeKind::Bit&&mod.typeOf(value).kind==pd::TypeKind::UInt){err("bool requires a Boolean value; compare uint explicitly with zero instead of implicitly truncating it");return value;}
  if(type.kind==pd::TypeKind::Angle){err("runtime floating-point values are unsupported; angles must be compile-time constants");return value;}
  if(type.kind==pd::TypeKind::Int){
    if(mod.typeOf(value).kind!=pd::TypeKind::Bit&&mod.typeOf(value).kind!=pd::TypeKind::Int){err("runtime int accepts only a measured-bit snapshot; use explicit uint[width] for arithmetic");return value;}
  }
  if(auto exact=integerValue(value)){
    if(type.kind==pd::TypeKind::UInt){
      if(*exact<0|| (type.width<64&&static_cast<std::uint64_t>(*exact)>((std::uint64_t(1)<<type.width)-1))){err("uint literal does not fit its declared width");return value;}
      return b.constUInt(static_cast<std::uint64_t>(*exact),type.width);
    }
    if(type.kind==pd::TypeKind::Bit&&*exact!=0&&*exact!=1){err("bool initializer must be 0 or 1");return value;}
  }
  return b.copy(value,type);
}
void Parser::parseTypedDecl(){
  const bool uint=consume().kind==Tok::UInt;pd::Type type=pd::bitType();
  if(uint){expect(Tok::LBracket,"'['");auto width=foldExpr();expect(Tok::RBracket,"']'");
    if(!width||*width<1||*width>64||std::floor(*width)!=*width){err("uint width must be an integer in [1,64]");return;}type=pd::uintType(static_cast<unsigned>(*width));}
  if(cur().kind!=Tok::Identifier){err("expected scalar name");return;}const auto name=consume().text;
  expect(Tok::Equals,"'='");const auto oldWidth=unsignedLiteralWidth;if(uint)unsignedLiteralWidth=type.width;
  classicals[name]=coerce(parsePredicate(),type);unsignedLiteralWidth=oldWidth;scalarTypes[name]=type;ctConst.erase(name);recordScalar(name,true);
}
void Parser::joinClassicals(pd::ValueId predicate,const std::unordered_map<std::string,pd::ValueId>& before,const std::unordered_map<std::string,pd::ValueId>& thenValues,const std::unordered_map<std::string,pd::ValueId>& elseValues){
  classicals=before;
  for(const auto& [name,value]:before){
    const auto yes=thenValues.at(name),no=elseValues.at(name);
    if(yes==no){classicals[name]=yes;continue;}
    const auto type=scalarTypes.count(name)?scalarTypes.at(name):mod.typeOf(value);
    if(type.kind==pd::TypeKind::Angle||(type.kind==pd::TypeKind::Int&&integerValue(value))){err("runtime branches cannot change compile-time scalar bindings; declare uint[width] for controller arithmetic");continue;}
    if(mod.typeOf(yes)!=mod.typeOf(no)){err("runtime branch values must have identical types and widths");continue;}
    classicals[name]=b.select(predicate,yes,no);ctConst.erase(name);recordScalar(name);
  }
}

// --- Slot accessors ------------------------------------------------------

pd::ValueId Parser::getQubitSlot(const std::string& name, int idx,
                                 const Token& at) {
  auto it = qreg.find(name);
  if (it == qreg.end()) {
    diag.error("unknown qubit register: " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  if (idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    diag.error("qubit index out of range for " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  if(pd::ValueId(it->second[idx])==pd::kInvalidValue){err("quantum value was discarded and cannot be used again: "+name);return pd::kInvalidValue;}
  return it->second[idx];
}
void Parser::setQubitSlot(const std::string& name, int idx, pd::ValueId v) {
  auto it = qreg.find(name);
  if (it != qreg.end() && idx >= 0 && static_cast<std::size_t>(idx) < it->second.size())
    it->second[idx] = v;
}

pd::ValueId Parser::getBitSlot(const std::string& name, int idx,
                               const Token& at) {
  auto it = creg.find(name);
  if (it == creg.end()) {
    diag.error("unknown bit register: " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  if (idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    diag.error("bit index out of range for " + name,
               pd::Location{filename, at.line, at.column});
    return pd::kInvalidValue;
  }
  return it->second[idx];
}
void Parser::setBitSlot(const std::string& name, int idx, pd::ValueId v) {
  auto it = creg.find(name);
  if (it == creg.end() || idx < 0 || static_cast<std::size_t>(idx) >= it->second.size()) {
    err("bit index out of range for " + name);
    return;
  }
  it->second[idx] = v;
  auto target = bitTargets.find(name);
  if (target != bitTargets.end()) {
    mod.opMut(mod.producerOf(v)).attributes.push_back(
        {"clbit", static_cast<double>(target->second[idx])});
  }
}

// --- Header --------------------------------------------------------------

void Parser::parseHeader() {
  skipNewlines();
  if (!expect(Tok::Target, "'target'")) { fatal = true; return; }
  if (cur().kind == Tok::Generic) {
    mod.targetAttr = "generic";
    consume();
  } else if (cur().kind == Tok::Identifier || cur().kind == Tok::String) {
    mod.targetAttr = consume().text;
  } else {
    err("expected 'generic' or device id after 'target'");
    fatal = true; return;
  }
  // Newline is optional after header (eof terminates fine too).
  if (cur().kind == Tok::Newline) consume();
}

// --- Declarations --------------------------------------------------------

void Parser::parseDeclQubit() {
  consume();  // 'qubit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if(qreg.count(name)&&std::all_of(qreg.at(name).begin(),qreg.at(name).end(),[](const auto& value){return pd::ValueId(value)==pd::kInvalidValue;}))qreg.erase(name);
  if(runtimeDepth&&(qreg.count(name)||creg.count(name)||classicals.count(name))){err("runtime branches cannot redeclare or shadow an existing register or scalar");fatal=true;return;}
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt || !std::isfinite(*sizeOpt) || std::floor(*sizeOpt) != *sizeOpt ||
      *sizeOpt <= 0 || *sizeOpt > 1000000) {
    err("qubit register size must be a positive compile-time integer up to 1000000"); return;
  }
  int n = static_cast<int>(*sizeOpt);
  std::vector<pd::ValueId> slots;
  slots.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    pd::ValueId v;
    if(reusableQubits.empty())v=b.allocQubit();
    else{v=b.reset(reusableQubits.back());reusableQubits.pop_back();}
    if(runtimeDepth||!inlineFrames.empty())mod.opMut(mod.producerOf(v)).attributes.push_back({"fresh",1.0});
    mod.setName(v, name + std::to_string(i));
    slots.push_back(v);
  }
  qreg[name] = std::move(slots);
  quantumScope[name]=runtimeDepth;
}

void Parser::parseDeclBit() {
  if (runtimeDepth) {
    err("runtime branches cannot allocate or redeclare classical registers; declare the register before the branch");
    fatal = true; return;
  }
  consume();  // 'bit'
  if (cur().kind != Tok::Identifier) { err("expected register name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::LBracket, "'['")) return;
  auto sizeOpt = foldExpr();
  if (!expect(Tok::RBracket, "']'")) return;
  if (!sizeOpt || !std::isfinite(*sizeOpt) || std::floor(*sizeOpt) != *sizeOpt ||
      *sizeOpt <= 0 || *sizeOpt > 1000000) {
    err("bit register size must be a positive compile-time integer up to 1000000"); return;
  }
  int n = static_cast<int>(*sizeOpt);
  std::vector<pd::ValueId> slots;
  slots.reserve(static_cast<std::size_t>(n));
  std::vector<std::size_t> targets;
  for (int i = 0; i < n; ++i) {
    pd::ValueId v = b.allocBit();
    targets.push_back(nextBit++);
    mod.opMut(mod.producerOf(v)).attributes.push_back({"clbit", static_cast<double>(targets.back())});
    mod.setName(v, name + std::to_string(i));
    slots.push_back(v);
  }
  creg[name] = std::move(slots);
  bitTargets[name] = std::move(targets);
}

void Parser::parseDeclClassical(bool isAngle) {
  consume();  // 'int' or 'angle'
  if (cur().kind != Tok::Identifier) { err("expected name"); return; }
  std::string name = consume().text;
  if (!expect(Tok::Equals, "'='")) return;
  if(!isAngle){
    const auto start=pos;auto value=parseExpr();
    if(auto exact=integerValue(value)){
      classicals[name]=b.constInt(*exact);ctConst[name]=static_cast<double>(*exact);
      recordScalar(name,true);return;
    }
    pos=start;
  }
  // Try to compile-time fold first (so we can use it as a `for` bound).
  std::size_t save = pos;
  auto folded = foldExpr();
  if (folded && (cur().kind == Tok::Newline || cur().kind == Tok::Eof)) {
    if (!std::isfinite(*folded) || (!isAngle && (std::floor(*folded) != *folded||std::abs(*folded)>9007199254740991.0))) {
      err("classical initializer must match its finite numeric type"); return;
    }
    ctConst[name] = *folded;
    classicals[name] = isAngle ? b.constAngle(*folded)
                                : b.constInt(static_cast<int64_t>(*folded));
  } else {
    ctConst.erase(name);
    pos = save;
    pd::ValueId v = parseExpr();
    if (mod.typeOf(v).kind == pd::TypeKind::Bit) {
      if(isAngle){err("runtime floating-point angles are unsupported; use bool or uint for measured data");return;}
      classicals[name]=b.copy(v,pd::intType());scalarTypes[name]=pd::intType();recordScalar(name,true);return;
    }
    classicals[name] = v;
  }
  recordScalar(name, true);
}

// --- Qubit reference helper ----------------------------------------------

std::optional<std::pair<std::string, int>> Parser::parseQubitRef() {
  if (cur().kind != Tok::Identifier) {
    err("expected qubit reference");
    return std::nullopt;
  }
  std::string name = consume().text;
  int idx = -1;
  if (cur().kind == Tok::LBracket) {
    consume();
    auto v = foldExpr();
    if(!v){err("dynamic qubit indexing is unsupported; qubit index must be a compile-time integer");return std::nullopt;}
    if (!expect(Tok::RBracket, "']'")) return std::nullopt;
    if (!v || !std::isfinite(*v) || std::floor(*v) != *v ||
        *v < 0 || *v > std::numeric_limits<int>::max()) {
      err("qubit index must be a non-negative compile-time integer"); return std::nullopt;
    }
    idx = static_cast<int>(*v);
  }
  return std::make_pair(std::move(name), idx);
}

// --- Gate / measure / reset / barrier ------------------------------------

void Parser::parseGateStmt() {
  Token gateTok = consume();  // GateName
  std::string g = gateTok.text;
  // Optional angle list: "(angle expr [, angle expr])"
  std::vector<std::optional<pd::ValueId>> angleVals;
  std::vector<double>      angleConsts;   // resolved compile-time
  if (cur().kind == Tok::LParen) {
    consume();
    while (cur().kind != Tok::RParen) {
      std::size_t save = pos;
      auto folded = foldExpr();
      if (folded && (cur().kind == Tok::Comma || cur().kind == Tok::RParen)) {
        angleConsts.push_back(*folded);
        angleVals.push_back(std::nullopt);
      } else {
        pos = save;
        pd::ValueId v = parseExpr();
        angleVals.push_back(v);
        angleConsts.push_back(0.0);
        const auto type = mod.typeOf(v).kind;
        if (type != pd::TypeKind::Int && type != pd::TypeKind::Angle)
          err("gate parameter must be a compile-time numeric expression");
      }
      if (!accept(Tok::Comma)) break;
    }
    expect(Tok::RParen, "')'");
  }
  const std::size_t expectedAngles = g == "u1q" ? 2 :
      (g == "rx" || g == "ry" || g == "rz" || g == "gpi" ||
       g == "gpi2" || g == "rzz" || g == "rxx" || g == "gphase") ? 1 : 0;
  if (angleConsts.size() != expectedAngles) {
    err("gate '" + g + "' has missing, extra, or unbound angle parameters");
    return;
  }
  for (double angle : angleConsts) if (!std::isfinite(angle)) {
    err("gate angle must be finite"); return;
  }
  auto bindAngles = [&](pd::OpId id) {
    auto& operation = mod.opMut(id);
    for (std::size_t i = 0; i < angleVals.size(); ++i) if (angleVals[i]) {
      const std::string name = g == "u1q" ? (i == 0 ? "theta" : "phi") : "angle";
      std::erase_if(operation.attributes, [&](const auto& a) { return a.name == name; });
      operation.attributes.push_back({name + "_operand", static_cast<double>(operation.operands.size())});
      operation.operands.push_back(*angleVals[i]);
    }
  };
  if (g == "gphase") {
    if (cur().kind != Tok::Newline && cur().kind != Tok::Eof && cur().kind != Tok::RBrace) {
      err("gphase takes no qubit operands"); return;
    }
    b.globalPhase(angleConsts.at(0));
    bindAngles(pd::OpId{static_cast<std::uint32_t>(mod.numOps() - 1)});
    return;
  }
  // Operands: 1 or 2 qubit references, comma-separated.
  std::vector<std::pair<std::string, int>> ops;
  while (true) {
    auto qref = parseQubitRef();
    if (!qref) return;
    ops.push_back(*qref);
    if (!accept(Tok::Comma)) break;
  }

  auto applyGate = [&](const std::string& reg, int idx) -> pd::ValueId {
    pd::ValueId v = getQubitSlot(reg, idx, gateTok);
    pd::ValueId r;
    if (g == "h")        r = b.h(v);
    else if (g == "x")   r = b.x(v);
    else if (g == "y")   r = b.y(v);
    else if (g == "z")   r = b.z(v);
    else if (g == "s")   r = b.s(v);
    else if (g == "sdg") r = b.sdg(v);
    else if (g == "t")   r = b.t(v);
    else if (g == "tdg") r = b.tdg(v);
    else if (g == "sx")  r = b.sx(v);
    else if (g == "sxdg") r = b.sxdg(v);
    else if (g == "rx")  r = b.rx(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "ry")  r = b.ry(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "rz")  r = b.rz(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "gpi") r = b.gpi(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "gpi2") r = b.gpi2(angleConsts.empty() ? 0.0 : angleConsts[0], v);
    else if (g == "u1q") r = b.u1q(angleConsts.size()>0?angleConsts[0]:0.0,
                                   angleConsts.size()>1?angleConsts[1]:0.0, v);
    else { err("unknown 1q gate: " + g); r = v; }
    if (!diag.hasErrors()) bindAngles(mod.producerOf(r));
    return r;
  };

  if (ops.size() == 1) {
    auto& [reg, idx] = ops[0];
    if (idx == -1) {
      // whole register
      if (!qreg.count(reg)) { err("unknown qubit register: " + reg); return; }
      auto& slots = qreg[reg];
      for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i] = applyGate(reg, static_cast<int>(i));
      }
    } else {
      pd::ValueId nv = applyGate(reg, idx);
      setQubitSlot(reg, idx, nv);
    }
  } else if (ops.size() == 2) {
    auto& [ra, ia] = ops[0];
    auto& [rb, ib] = ops[1];
    // If a register has size 1 and no index was given, default to 0.
    if (ia == -1 && qreg.count(ra) && qreg[ra].size() == 1) ia = 0;
    if (ib == -1 && qreg.count(rb) && qreg[rb].size() == 1) ib = 0;
    if (ia == -1 || ib == -1) {
      err("two-qubit gate requires explicit indices on register operands");
      return;
    }
    pd::ValueId va = getQubitSlot(ra, ia, gateTok);
    pd::ValueId vb = getQubitSlot(rb, ib, gateTok);
    if (ra == rb && ia == ib) { err("two-qubit gate requires distinct qubits"); return; }
    std::pair<pd::ValueId, pd::ValueId> r{va, vb};
    if      (g == "cx")   r = b.cx(va, vb);
    else if (g == "cz")   r = b.cz(va, vb);
    else if (g == "swap") r = b.swap(va, vb);
    else if (g == "ecr")  r = b.ecr(va, vb);
    else if (g == "ms")   r = b.ms(va, vb);
    else if (g == "rzz")  r = b.rzz(angleConsts[0], va, vb);
    else if (g == "rxx")  r = b.rxx(angleConsts[0], va, vb);
    else { err("unknown 2q gate: " + g); }
    if (!diag.hasErrors()) bindAngles(mod.producerOf(r.first));
    setQubitSlot(ra, ia, r.first);
    setQubitSlot(rb, ib, r.second);
  } else {
    err("gate with " + std::to_string(ops.size()) + " operands");
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseMeasureAssign(const std::string& lhsName,
                                std::optional<int> lhsIdx) {
  // Already consumed: lhsName [`[` lhsIdx `]`] '='
  expect(Tok::Measure, "'measure'");
  auto qref = parseQubitRef();
  if (!qref) return;
  auto& [reg, idx] = *qref;
  if (idx == -1) {
    // c = measure q  → for each slot pair
    auto qit = qreg.find(reg);
    auto cit = creg.find(lhsName);
    if (qit == qreg.end() || cit == creg.end()) {
      err("unknown register in 'measure' statement");
      return;
    }
    if (qit->second.size() != cit->second.size()) {
      err("register size mismatch in 'measure' assignment");
      return;
    }
    for (std::size_t i = 0; i < qit->second.size(); ++i) {
      pd::ValueId vbit = b.measure(qit->second[i]);
      setBitSlot(lhsName, static_cast<int>(i), vbit);
    }
  } else {
    pd::ValueId vq = getQubitSlot(reg, idx, cur());
    pd::ValueId vbit = b.measure(vq);
    if (lhsIdx) {
      setBitSlot(lhsName, *lhsIdx, vbit);
    } else {
      // single-bit register? only valid if creg[lhsName].size() == 1
      auto cit = creg.find(lhsName);
      if (cit == creg.end() || cit->second.size() != 1) {
        err("expected indexed lhs in 'measure' assignment");
        return;
      }
      setBitSlot(lhsName, 0, vbit);
    }
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseResetStmt() {
  consume();  // 'reset'
  auto qref = parseQubitRef();
  if (!qref) return;
  auto& [reg, idx] = *qref;
  if (idx == -1) {
    if (!qreg.count(reg)) { err("reset on unknown qubit register: " + reg); return; }
    auto& slots = qreg[reg];
    for (std::size_t i = 0; i < slots.size(); ++i) {
      slots[i] = b.reset(slots[i]);
    }
  } else {
    pd::ValueId nv = b.reset(getQubitSlot(reg, idx, cur()));
    setQubitSlot(reg, idx, nv);
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseBarrierStmt() {
  consume();  // 'barrier'
  std::vector<pd::ValueId> qs;
  while (true) {
    auto qref = parseQubitRef();
    if (!qref) return;
    auto& [reg, idx] = *qref;
    if (idx == -1) {
      if (!qreg.count(reg)) { err("barrier on unknown qubit register: " + reg); return; }
      for (auto v : qreg[reg]) qs.push_back(v);
    } else {
      qs.push_back(getQubitSlot(reg, idx, cur()));
    }
    if (!accept(Tok::Comma)) break;
  }
  b.barrier(std::span<const pd::ValueId>(qs.data(), qs.size()));
  if (cur().kind == Tok::Newline) consume();
}

// --- Control flow --------------------------------------------------------

void Parser::parseIfStmt() {
  consume();
  expect(Tok::LParen, "'('");
  // Resolve static conditions before constructing SSA so the untaken branch
  // cannot change qubit bindings or leak a value into the following code.
  const auto conditionStart = pos;
  // Integer comparisons are resolved without the angle evaluator's double
  // conversion. This also preserves distinctions above 2^53.
  {
    const auto left=parseExpr();const auto comparison=cur().kind;
    if(comparison==Tok::EqEq||comparison==Tok::NotEq||comparison==Tok::Lt||comparison==Tok::Gt||comparison==Tok::Le||comparison==Tok::Ge){
      consume();const auto oldWidth=unsignedLiteralWidth;if(mod.typeOf(left).kind==pd::TypeKind::UInt)unsignedLiteralWidth=mod.typeOf(left).width;
      const auto right=parseExpr();unsignedLiteralWidth=oldWidth;
      const auto a=integerValue(left),b_=integerValue(right);
      if(a&&b_&&accept(Tok::RParen)){
        const bool take=comparison==Tok::EqEq?*a==*b_:comparison==Tok::NotEq?*a!=*b_:comparison==Tok::Lt?*a<*b_:comparison==Tok::Gt?*a>*b_:comparison==Tok::Le?*a<=*b_:*a>=*b_;
        if(take)parseBlock();else skipBlock();skipNewlines();
        if(accept(Tok::Else)){if(take)skipBlock();else parseBlock();}return;
      }
    }
    pos=conditionStart;
  }
  auto foldedLeft = foldExpr();
  const auto comparison = cur().kind;
  if (foldedLeft && (comparison == Tok::EqEq || comparison == Tok::NotEq ||
      comparison == Tok::Lt || comparison == Tok::Gt ||
      comparison == Tok::Le || comparison == Tok::Ge)) {
    consume();
    auto foldedRight = foldExpr();
    if (foldedRight && accept(Tok::RParen)) {
      bool takeThen = comparison == Tok::EqEq ? *foldedLeft == *foldedRight :
          comparison == Tok::NotEq ? *foldedLeft != *foldedRight :
          comparison == Tok::Lt ? *foldedLeft < *foldedRight :
          comparison == Tok::Gt ? *foldedLeft > *foldedRight :
          comparison == Tok::Le ? *foldedLeft <= *foldedRight : *foldedLeft >= *foldedRight;
      if (takeThen) parseBlock(); else skipBlock();
      skipNewlines();
      if (accept(Tok::Else)) {
        if (takeThen) skipBlock(); else parseBlock();
      }
      return;
    }
  }
  pos = conditionStart;
  pd::ValueId pred=parsePredicate();
  expect(Tok::RParen,"')'");
  if(mod.typeOf(pred).kind!=pd::TypeKind::Bit)pred=b.cmp("!=",pred,b.constInt(0));
  pd::OpId ifId=b.beginIf(pred);
  const auto before=classicals;const auto constantsBefore=ctConst;
  const auto bitsBefore=snapshotBits();
  const auto registersBefore=qreg;
  const auto typesBefore=scalarTypes;
  ++runtimeDepth;parseBlock();skipNewlines();
  const auto thenValues=classicals;
  const auto thenBits=snapshotBits();restoreBits(bitsBefore);
  classicals=before;ctConst=constantsBefore;scalarTypes=typesBefore;qreg=registersBefore;
  if(accept(Tok::Else)){b.elseIf(ifId);parseBlock();}
  const auto elseValues=classicals;
  const auto elseBits=snapshotBits();
  --runtimeDepth;b.endIf(ifId);
  scalarTypes=typesBefore;ctConst=constantsBefore;
  joinClassicals(pred,before,thenValues,elseValues);
  joinBits(pred,bitsBefore,thenBits,elseBits);
  qreg=registersBefore;
  if(cur().kind==Tok::Newline)consume();
}

void Parser::parseForStmt() {
  consume();
  if (cur().kind != Tok::Identifier) { err("expected loop variable"); return; }
  std::string var = consume().text;
  if (!expect(Tok::In, "'in'")) return;
  auto bound=[&]() -> std::optional<std::int64_t> {
    const auto start=pos;if(auto exact=foldIntegerExpr())return exact;pos=start;
    const auto value=foldExpr();if(value&&std::isfinite(*value)&&std::floor(*value)==*value&&std::abs(*value)<=9007199254740991.0)return static_cast<std::int64_t>(*value);
    return std::nullopt;
  };
  auto loOpt = bound();
  if (!expect(Tok::DotDot, "'..'")) return;
  auto hiOpt = bound();
  if (!loOpt || !hiOpt) {
    err("for-loop bounds must be compile-time integers"); return;
  }
  skipNewlines();
  const auto bodyStart = pos;
  skipBlock();
  const auto afterBody = pos;
  if (diag.hasErrors()) return;
  const auto oldConst = ctConst.find(var) == ctConst.end() ? std::optional<double>{} : ctConst[var];
  const auto oldValue = classicals.find(var) == classicals.end() ? std::optional<pd::ValueId>{} : classicals[var];
  for (auto value = static_cast<std::int64_t>(*loOpt);
       value < static_cast<std::int64_t>(*hiOpt) && !diag.hasErrors() && !inlineReturned(); ++value) {
    if (++expandedIterations > 100000) { err("static loop expansion exceeds 100000 iterations"); break; }
    ctConst[var] = static_cast<double>(value);
    classicals[var] = b.constInt(value);
    recordScalar(var);
    pos = bodyStart;
    parseBlock();
  }
  pos = afterBody;
  if (oldConst) ctConst[var] = *oldConst; else ctConst.erase(var);
  if (oldValue) classicals[var] = *oldValue; else classicals.erase(var);
  recordScalar(var);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::skipBlock() {
  skipNewlines();
  if (!expect(Tok::LBrace, "'{'") ) return;
  std::size_t depth = 1;
  while (depth && cur().kind != Tok::Eof) {
    auto token = consume();
    if (token.kind == Tok::LBrace) ++depth;
    else if (token.kind == Tok::RBrace) --depth;
  }
  if (depth) err("unterminated block");
}

void Parser::parseWhileStmt() {
  // The unprefixed form is canonical; keep static while for compatibility.
  {auto scan=pos;int depth=0;bool bounded=false;
    while(scan<toks.size()&&toks[scan].kind!=Tok::LBrace&&toks[scan].kind!=Tok::Eof){if(toks[scan].kind==Tok::MaxIterations)bounded=true;++scan;}
    if(bounded){parseBoundedWhile();return;}}
  consume();
  if (!expect(Tok::LParen, "'('")) return;
  const auto conditionStart = pos;
  auto condition = [&]() -> std::optional<bool> {
    pos = conditionStart;
    if(const auto left=foldIntegerExpr()){
      const auto comparison=consume().kind;const auto right=foldIntegerExpr();
      if(right&&accept(Tok::RParen))switch(comparison){
        case Tok::EqEq:return *left==*right;case Tok::NotEq:return *left!=*right;
        case Tok::Lt:return *left<*right;case Tok::Gt:return *left>*right;
        case Tok::Le:return *left<=*right;case Tok::Ge:return *left>=*right;
        default:break;
      }
    }
    pos=conditionStart;
    auto left = foldExpr();
    const auto comparison = consume().kind;
    auto right = foldExpr();
    if (!left || !right || !std::isfinite(*left) || !std::isfinite(*right) ||
        !accept(Tok::RParen)) {
      err("while requires a finite compile-time condition; measured-bit while is unsupported");
      return std::nullopt;
    }
    switch (comparison) {
      case Tok::EqEq: return *left == *right;
      case Tok::NotEq: return *left != *right;
      case Tok::Lt: return *left < *right;
      case Tok::Gt: return *left > *right;
      case Tok::Le: return *left <= *right;
      case Tok::Ge: return *left >= *right;
      default: err("while requires a comparison predicate"); return std::nullopt;
    }
  };
  auto take = condition();
  if (!take) return;
  skipNewlines();
  const auto bodyStart = pos;
  skipBlock();
  const auto afterBody = pos;
  while (*take && !diag.hasErrors() && !inlineReturned()) {
    if (++expandedIterations > 100000) {
      err("static loop expansion exceeds 100000 iterations; while termination was not established"); break;
    }
    pos = bodyStart;
    parseBlock();
    if (diag.hasErrors() || inlineReturned()) break;
    take = condition();
    if (!take) break;
  }
  pos = afterBody;
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseBoundedWhile(){
  accept(Tok::Bounded);if(!expect(Tok::While,"'while'"))return;
  if(!expect(Tok::LParen,"'('"))return;
  const auto conditionStart=pos;parsePredicate();if(!expect(Tok::RParen,"')'"))return;
  if(!expect(Tok::MaxIterations,"'max_iterations'"))return;
  const auto bound=foldExpr();
  if(!bound||!std::isfinite(*bound)||std::floor(*bound)!=*bound||*bound<=0||*bound>operationBudget){err("bounded while max_iterations must be a positive compile-time integer within the expanded operation budget");return;}
  const auto bodyStart=pos;skipBlock();const auto afterBody=pos;
  const auto loopId=expandedCalls++;
  const auto liveName=freshName("loop_live"),doneName=freshName("iteration_done");
  classicals[liveName]=b.copy(b.constInt(1),pd::bitType());scalarTypes[liveName]=pd::bitType();
  classicals[doneName]=b.copy(b.constInt(0),pd::bitType());scalarTypes[doneName]=pd::bitType();
  loopFrames.push_back({liveName,doneName,false,inlineFrames.size()});
  for(std::size_t iteration=0;iteration<static_cast<std::size_t>(*bound)&&!diag.hasErrors();++iteration){
    if(mod.numOps()>operationBudget){err("bounded while exceeds QSTACK_EXPANDED_OPERATION_BUDGET");return;}
    pos=conditionStart;auto predicate=parsePredicate();
    if(mod.typeOf(predicate).kind!=pd::TypeKind::Bit)predicate=b.cmp("!=",predicate,b.constInt(0));
    predicate=b.binOp("&",predicate,classicals.at(liveName));
    predicate=b.copy(predicate,pd::bitType());
    classicals[doneName]=b.copy(b.constInt(0),pd::bitType());loopFrames.back().mayTransfer=false;
    const auto before=classicals;const auto constantsBefore=ctConst;const auto typesBefore=scalarTypes;const auto bitsBefore=snapshotBits();const auto registersBefore=qreg;
    auto marker=b.beginIf(predicate);++runtimeDepth;pos=bodyStart;parseBlock();--runtimeDepth;b.endIf(marker);
    const auto thenValues=classicals;const auto thenBits=snapshotBits();
    classicals=before;ctConst=constantsBefore;scalarTypes=typesBefore;qreg=registersBefore;
    joinClassicals(predicate,before,thenValues,before);joinBits(predicate,bitsBefore,thenBits,bitsBefore);
    if(inlineReturned()){err("return inside bounded runtime loops requires explicit loop-exit normalization");return;}
  }
  pos=conditionStart;auto exhausted=parsePredicate();
  if(mod.typeOf(exhausted).kind!=pd::TypeKind::Bit)exhausted=b.cmp("!=",exhausted,b.constInt(0));
  exhausted=b.binOp("&",exhausted,classicals.at(liveName));
  exhausted=b.copy(exhausted,pd::bitType());b.output("loop_exhausted_"+std::to_string(loopId),exhausted,"loop_exhausted");
  loopFrames.pop_back();classicals.erase(liveName);classicals.erase(doneName);scalarTypes.erase(liveName);scalarTypes.erase(doneName);
  pos=afterBody;
}

// --- Function definition / call ------------------------------------------

void Parser::parseDefStmt() {
  if (!inlineFrames.empty()) {
    err("nested function definitions are unsupported; define helpers at module scope");
    fatal = true;
    return;
  }
  consume();
  if (cur().kind != Tok::Identifier) { err("expected function name"); return; }
  std::string name = consume().text;
  if (funcs.count(name)) { err("function cannot be redefined: " + name); return; }
  if (!expect(Tok::LParen, "'('")) return;
  std::vector<pd::Builder::Param> params;
  while (cur().kind != Tok::RParen) {
    pd::Type ty = pd::qubitType();
    if      (cur().kind == Tok::Qubit) { ty = pd::qubitType(); consume(); }
    else if (cur().kind == Tok::Bit)   { ty = pd::bitType();   consume(); }
    else if (cur().kind == Tok::Int)   { ty = pd::intType();   consume(); }
    else if (cur().kind == Tok::Angle) { ty = pd::angleType(); consume(); }
    else { err("expected parameter type (qubit/bit/int/angle)"); return; }
    if (cur().kind != Tok::Identifier) { err("expected parameter name"); return; }
    std::string pname = consume().text;
    if (std::any_of(params.begin(), params.end(), [&](const auto& p) { return p.name == pname; })) {
      err("duplicate function parameter: " + pname); return;
    }
    params.push_back({ty, pname});
    if (!accept(Tok::Comma)) break;
  }
  if (!expect(Tok::RParen, "')'")) return;
  skipNewlines();
  FuncDecl fd;
  fd.name = name;
  fd.params = params;
  fd.body_start = pos;
  fd.specialize = std::any_of(params.begin(), params.end(), [](const auto& p) {
    return p.type.kind != pd::TypeKind::Qubit;
  });
  skipBlock();
  fd.body_end = pos;
  pos = fd.body_start;
  {std::size_t depth=0;for(auto token=fd.body_start;token<fd.body_end;++token){
    if(toks[token].kind==Tok::LBrace)++depth;
    else if(toks[token].kind==Tok::RBrace)--depth;
    else if(toks[token].kind==Tok::Return&&depth>1)fd.normalizeReturns=true;
  }}
  if(fd.normalizeReturns)fd.specialize=true;
  // A wrapper around a template and a function using lexical variables
  // also need call-time expansion. Pure qubit helpers retain Def/Call IR.
  for (auto token = fd.body_start; token < fd.body_end; ++token) {
    if (toks[token].kind != Tok::Identifier) continue;
    const auto& symbol = toks[token].text;
    const bool parameter = std::any_of(params.begin(), params.end(), [&](const auto& p) { return p.name == symbol; });
    if (!parameter && (qreg.count(symbol) || creg.count(symbol) || scalarBindings.count(symbol)))
      fd.specialize = true;
    const auto helper = funcs.find(symbol);
    if (helper != funcs.end() && helper->second.specialize && toks[token + 1].kind == Tok::LParen)
      fd.specialize = true;
  }
  if (fd.specialize) {
    fd.capturedQreg = qreg;
    fd.capturedCreg = creg;
    fd.capturedScalars = scalarBindings;
    fd.capturedBitTargets = bitTargets;
    pos = fd.body_end;
    funcs.emplace(name, std::move(fd));
    if (cur().kind == Tok::Newline) consume();
    return;
  }
  const auto savedQreg = qreg;
  const auto savedCreg = creg;
  const auto savedClassicals = classicals;
  const auto savedScalarTypes = scalarTypes;
  const auto savedConstants = ctConst;
  const auto savedScalars = scalarBindings;
  const auto savedBitTargets = bitTargets;
  // Merely defining a structured function must not update captured slots.
  for (auto& [_, reg] : qreg) reg = reg.clone();
  for (auto& [_, reg] : creg) reg = reg.clone();
  for (auto& [_, binding] : scalarBindings) binding = std::make_shared<ScalarBinding>(*binding);
  pd::OpId defId = b.beginDef(name, std::span<const pd::Builder::Param>(params.data(), params.size()));
  for (std::size_t i = 0; i < params.size(); ++i) {
    pd::ValueId pv = b.paramValue(defId, i);
    qreg.erase(params[i].name); creg.erase(params[i].name);
    classicals.erase(params[i].name); ctConst.erase(params[i].name);
    bitTargets.erase(params[i].name);
    scalarBindings.erase(params[i].name);
    if (params[i].type.kind == pd::TypeKind::Qubit) qreg[params[i].name] = {pv};
    else if (params[i].type.kind == pd::TypeKind::Bit) creg[params[i].name] = {pv};
    else classicals[params[i].name] = pv;
  }
  parseBlock();
  qreg = savedQreg; creg = savedCreg; classicals = savedClassicals;
  scalarTypes = savedScalarTypes;
  ctConst = savedConstants; bitTargets = savedBitTargets;
  scalarBindings = savedScalars;
  b.endDef(defId);
  fd.body_end = pos;
  funcs[fd.name] = std::move(fd);
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseCallStmt(const std::string& name) {
  if (!expect(Tok::LParen, "'('")) return;
  std::vector<pd::ValueId> args;
  std::vector<std::pair<std::string, int>> refs;
  std::vector<std::optional<double>> constants;
  while (cur().kind != Tok::RParen && cur().kind != Tok::Eof) {
    if (cur().kind == Tok::Identifier &&
        (qreg.count(cur().text) || creg.count(cur().text))) {
      auto ref = parseQubitRef();
      if (!ref) return;
      auto& [reg, idx] = *ref;
      const bool quantum = qreg.count(reg) != 0;
      const auto& slots = quantum ? qreg.at(reg) : creg.at(reg);
      if (idx == -1 && slots.size() != 1) {
        err("scalar function argument requires one explicit register index"); return;
      }
      const int realIdx = idx == -1 ? 0 : idx;
      args.push_back(quantum ? getQubitSlot(reg, realIdx, cur()) : getBitSlot(reg, realIdx, cur()));
      refs.push_back({reg, realIdx});
      constants.push_back(std::nullopt);
    } else {
      const auto expressionStart = pos;
      auto value = foldExpr();
      pos = expressionStart;
      args.push_back(parseExpr());
      refs.push_back({"", -1});
      constants.push_back(value);
    }
    if (!accept(Tok::Comma)) break;
  }
  if (!expect(Tok::RParen, "')'") || diag.hasErrors()) return;
  const auto fit = funcs.find(name);
  if (fit != funcs.end() && fit->second.specialize) {
    // Copy metadata because a body parse must not retain unordered_map iterators.
    const auto function = fit->second;
    if (args.size() != function.params.size()) {
      err("argument count mismatch for function '" + name + "'"); return;
    }
    if (std::any_of(inlineFrames.begin(), inlineFrames.end(), [&](const auto& frame) {
          return frame.name == name;
        })) {
      err("recursive call to '" + name + "' is not supported"); return;
    }
    if (inlineFrames.size() >= 128 || ++expandedCalls > 100000) {
      err("function specialization exceeds 128 nested calls or 100000 expanded calls"); return;
    }
    std::vector<pd::ValueId> qubitArgs;
    for (std::size_t i = 0; i < args.size(); ++i) {
      const auto type = function.params[i].type.kind;
      const auto actual = mod.typeOf(args[i]).kind;
      if (type == pd::TypeKind::Int || type == pd::TypeKind::Angle) {
        const auto exact=type==pd::TypeKind::Int?integerValue(args[i]):std::nullopt;
        if ((actual != pd::TypeKind::Int && actual != pd::TypeKind::Angle) ||
            !constants[i] || !std::isfinite(*constants[i]) ||
            (type == pd::TypeKind::Int && !exact&&(std::floor(*constants[i]) != *constants[i] ||
                                         std::abs(*constants[i]) > 9007199254740991.0))) {
          err("function scalar argument must be bound to a finite value matching its parameter type"); return;
        }
      } else if (type != actual || refs[i].first.empty()) {
        err("register argument type mismatch for function '" + name + "'"); return;
      }
      if (type == pd::TypeKind::Qubit) {
        if (std::find(qubitArgs.begin(), qubitArgs.end(), args[i]) != qubitArgs.end()) {
          err("function qubit arguments must refer to distinct slots"); return;
        }
        qubitArgs.push_back(args[i]);
      }
    }
    const auto afterCall = pos;
    const auto savedQreg = qreg;
    const auto savedCreg = creg;
    const auto savedClassicals = classicals;
    const auto savedScalarTypes = scalarTypes;
    const auto savedConstants = ctConst;
    const auto savedScalars = scalarBindings;
    const auto savedBitTargets = bitTargets;
    qreg = function.capturedQreg;
    creg = function.capturedCreg;
    classicals.clear(); ctConst.clear(); scalarBindings.clear();scalarTypes.clear();
    for (const auto& [symbol, binding] : function.capturedScalars) {
      classicals[symbol] = binding->value;
      scalarTypes[symbol] = mod.typeOf(binding->value);
      if (binding->constant) ctConst[symbol] = *binding->constant;
      scalarBindings[symbol] = std::make_shared<ScalarBinding>(*binding);
    }
    bitTargets = function.capturedBitTargets;
    for (std::size_t i = 0; i < args.size(); ++i) {
      const auto& param = function.params[i];
      qreg.erase(param.name); creg.erase(param.name);
      classicals.erase(param.name); ctConst.erase(param.name); bitTargets.erase(param.name);
      scalarTypes.erase(param.name);
      scalarBindings.erase(param.name);
      if (param.type.kind == pd::TypeKind::Qubit) {
        qreg[param.name] = Register::alias(savedQreg.at(refs[i].first), refs[i].second);
      } else if (param.type.kind == pd::TypeKind::Bit) {
        creg[param.name] = Register::alias(savedCreg.at(refs[i].first), refs[i].second);
        const auto target = savedBitTargets.find(refs[i].first);
        if (target != savedBitTargets.end()) bitTargets[param.name] = {target->second.at(refs[i].second)};
      } else {
        ctConst[param.name] = *constants[i];
        classicals[param.name] = args[i];
        scalarTypes[param.name] = param.type;
        recordScalar(param.name, true);
      }
    }
    InlineFrame newFrame{name,runtimeDepth};newFrame.normalizeReturns=function.normalizeReturns;
    if(newFrame.normalizeReturns){
      newFrame.done=freshName("returned");classicals[newFrame.done]=b.copy(b.constInt(0),pd::bitType());scalarTypes[newFrame.done]=pd::bitType();
      for(const auto& param:function.params)if(param.type.kind==pd::TypeKind::Qubit)newFrame.quantumParams.push_back(param.name);
    }
    inlineFrames.push_back(std::move(newFrame));
    pos = function.body_start;
    parseBlock();
    auto frame = std::move(inlineFrames.back());
    inlineFrames.pop_back();
    std::vector<pd::ValueId> results = std::move(frame.values);
    if (!frame.returned) {
      for (const auto& param : function.params)
        if (param.type.kind == pd::TypeKind::Qubit) results.push_back(qreg.at(param.name)[0]);
    }
    // A conditional helper cannot change the meaning of a caller's slot at
    // the branch join. Materialize a returned permutation on those wires.
    // Outside runtime control flow, the existing return-alias semantics stay
    // unchanged. Fresh-wire replacement needs an explicit branch value merge.
    std::vector<std::string> quantumParams;
    std::vector<pd::ValueId> current;
    for (const auto& param : function.params) if (param.type.kind == pd::TypeKind::Qubit) {
      quantumParams.push_back(param.name);
      current.push_back(qreg.at(param.name)[0]);
    }
    for (std::size_t i = 0; i < results.size(); ++i)
      if (std::find(results.begin(), results.begin() + i, results[i]) != results.begin() + i)
        err("function cannot return duplicate qubit aliases: " + name);
    if (runtimeDepth && results.size() == current.size() && !diag.hasErrors()) {
      const auto argumentCount=current.size();
      for(auto value:results)if(std::find(current.begin(),current.end(),value)==current.end())current.push_back(value);
      std::vector<std::size_t> wanted,stateAt;
      for(auto value:results)wanted.push_back(static_cast<std::size_t>(std::find(current.begin(),current.end(),value)-current.begin()));
      for(std::size_t i=0;i<current.size();++i)stateAt.push_back(i);
      for(std::size_t i=0;i<argumentCount;++i){
        const auto found=std::find(stateAt.begin()+i,stateAt.end(),wanted[i]);const auto j=static_cast<std::size_t>(found-stateAt.begin());
        if(i==j)continue;
        const auto oldA=current[i],oldB=current[j];const auto swapped=b.swap(oldA,oldB);
        current[i]=swapped.first;current[j]=swapped.second;
        for(auto& [_,reg]:qreg)for(std::size_t slot=0;slot<reg.size();++slot){
          if(pd::ValueId(reg[slot])==oldA)reg[slot]=swapped.first;
          else if(pd::ValueId(reg[slot])==oldB)reg[slot]=swapped.second;
        }
        std::swap(stateAt[i],stateAt[j]);
      }
      current.resize(argumentCount);results=std::move(current);
    }
    qreg = savedQreg; creg = savedCreg; classicals = savedClassicals;
    scalarTypes = savedScalarTypes;
    ctConst = savedConstants; bitTargets = savedBitTargets;
    scalarBindings = savedScalars;
    pos = afterCall;
    if (results.size() != qubitArgs.size() || std::any_of(results.begin(), results.end(), [&](auto value) {
          return mod.typeOf(value).kind != pd::TypeKind::Qubit;
        })) {
      err("function must return one qubit value per qubit parameter: " + name);
    } else {
      std::size_t result = 0;
      for (std::size_t i = 0; i < function.params.size(); ++i)
        if (function.params[i].type.kind == pd::TypeKind::Qubit)
          setQubitSlot(refs[i].first, refs[i].second, results[result++]);
    }
  } else {
    std::vector<pd::Type> resultTypes;
    if (fit != funcs.end())
      for (const auto& param : fit->second.params)
        if (param.type.kind == pd::TypeKind::Qubit) resultTypes.push_back(pd::qubitType());
    auto results = b.call(name, args, resultTypes);
    std::size_t result = 0;
    for (std::size_t i = 0; i < refs.size() && result < results.size(); ++i)
      if (!refs[i].first.empty() && qreg.count(refs[i].first))
        setQubitSlot(refs[i].first, refs[i].second, results[result++]);
  }
  if (cur().kind == Tok::Newline) consume();
}

void Parser::parseAssignStmt(const std::string& name) {
  expect(Tok::Equals,"'='");
  if(!classicals.count(name)){err("assignment requires a declared scalar: "+name);return;}
  const auto expressionStart=pos;auto folded=foldExpr();pos=expressionStart;
  const auto oldType=scalarTypes.count(name)?scalarTypes.at(name):mod.typeOf(classicals.at(name));
  const auto oldWidth=unsignedLiteralWidth;if(oldType.kind==pd::TypeKind::UInt)unsignedLiteralWidth=oldType.width;
  auto value=parsePredicate();unsignedLiteralWidth=oldWidth;
  if(oldType.kind==pd::TypeKind::UInt||oldType.kind==pd::TypeKind::Bit||mod.typeOf(value).kind==pd::TypeKind::Bit){
    value=coerce(value,oldType);ctConst.erase(name);scalarTypes[name]=oldType;
  }else{
    if(runtimeDepth){err("runtime branches cannot change compile-time scalar bindings; declare uint[width] for controller arithmetic");return;}
    if(folded&&std::isfinite(*folded))ctConst[name]=*folded;else ctConst.erase(name);
  }
  classicals[name]=value;recordScalar(name);
  if(cur().kind==Tok::Newline)consume();
}

void Parser::parseReturnStmt() {
  consume();
  std::vector<pd::ValueId> vs;
  while (cur().kind != Tok::Newline && cur().kind != Tok::Eof &&
         cur().kind != Tok::RBrace) {
    if (cur().kind == Tok::Identifier && qreg.count(cur().text)) {
      auto qref = parseQubitRef();
      if (!qref) return;
      auto& [reg, idx] = *qref;
      if (idx == -1 && qreg.at(reg).size() != 1) {
        err("function return requires one explicit qubit index per value"); return;
      }
      vs.push_back(getQubitSlot(reg, idx == -1 ? 0 : idx, cur()));
    } else {
      vs.push_back(parseExpr());
    }
    if (!accept(Tok::Comma)) break;
  }
  if (!inlineFrames.empty()) {
    if(inlineFrames.back().normalizeReturns&&(runtimeDepth!=inlineFrames.back().runtimeDepth||inlineFrames.back().mayReturn)){
      auto& frame=inlineFrames.back();
      if(vs.size()!=frame.quantumParams.size()||std::any_of(vs.begin(),vs.end(),[&](auto value){return mod.typeOf(value).kind!=pd::TypeKind::Qubit;})){
        err("all function return paths must return one live quantum value per quantum parameter");return;
      }
      for(std::size_t i=0;i<vs.size();++i)if(std::find(vs.begin(),vs.begin()+i,vs[i])!=vs.begin()+i){err("function cannot return duplicate quantum aliases");return;}
      std::vector<pd::ValueId> current;for(const auto& param:frame.quantumParams)current.push_back(qreg.at(param)[0]);
      const auto count=current.size();for(auto value:vs)if(std::find(current.begin(),current.end(),value)==current.end())current.push_back(value);
      std::vector<std::size_t> wanted,stateAt;for(auto value:vs)wanted.push_back(static_cast<std::size_t>(std::find(current.begin(),current.end(),value)-current.begin()));
      for(std::size_t i=0;i<current.size();++i)stateAt.push_back(i);
      for(std::size_t i=0;i<count;++i){const auto found=std::find(stateAt.begin()+i,stateAt.end(),wanted[i]);const auto j=static_cast<std::size_t>(found-stateAt.begin());if(i==j)continue;
        const auto oldA=current[i],oldB=current[j];const auto swap=b.swap(oldA,oldB);current[i]=swap.first;current[j]=swap.second;
        for(auto& [_,reg]:qreg)for(std::size_t slot=0;slot<reg.size();++slot){if(pd::ValueId(reg[slot])==oldA)reg[slot]=swap.first;else if(pd::ValueId(reg[slot])==oldB)reg[slot]=swap.second;}
        std::swap(stateAt[i],stateAt[j]);
      }
      classicals[frame.done]=b.copy(b.constInt(1),pd::bitType());recordScalar(frame.done);frame.mayReturn=true;++flowEpoch;
      for(auto& loop:loopFrames)if(loop.functionDepth==inlineFrames.size()){classicals[loop.live]=b.copy(b.constInt(0),pd::bitType());classicals[loop.done]=b.copy(b.constInt(1),pd::bitType());loop.mayTransfer=true;}
    } else if (runtimeDepth != inlineFrames.back().runtimeDepth) {
      err("conditional return requires explicit control-flow return lowering");
    } else {
      inlineFrames.back().returned = true;
      inlineFrames.back().values = std::move(vs);
    }
  } else {
    b.returnOp(std::span<const pd::ValueId>(vs.data(), vs.size()));
  }
  if (cur().kind == Tok::Newline) consume();
}

// --- Statement dispatch + block ------------------------------------------

void Parser::parseStmt() {
  skipNewlines();
  if(mod.numOps()>operationBudget){err("expanded operation budget exceeded; raise QSTACK_EXPANDED_OPERATION_BUDGET or reduce the bounded program");fatal=true;return;}
  switch (cur().kind) {
    case Tok::Qubit:    parseDeclQubit(); return;
    case Tok::Bit:      parseDeclBit();   return;
    case Tok::Int:      parseDeclClassical(false); return;
    case Tok::Angle:    parseDeclClassical(true);  return;
    case Tok::Bool:case Tok::UInt:parseTypedDecl();return;
    case Tok::Output:{consume();if(cur().kind!=Tok::Identifier){err("output requires a named scalar");return;}const auto name=consume().text;if(!classicals.count(name)){err("unknown output scalar: "+name);return;}b.output(name,classicals.at(name));return;}
    case Tok::GateName: parseGateStmt(); return;
    case Tok::Reset:    parseResetStmt(); return;
    case Tok::Barrier:  parseBarrierStmt(); return;
    case Tok::If:       parseIfStmt(); return;
    case Tok::For:      parseForStmt(); return;
    case Tok::While:    parseWhileStmt(); return;
    case Tok::Bounded:  parseBoundedWhile();return;
    case Tok::Break:parseTransfer(true);return;
    case Tok::Continue:parseTransfer(false);return;
    case Tok::Discard:parseDiscard();return;
    case Tok::Def:      parseDefStmt(); return;
    case Tok::Return:   parseReturnStmt(); return;
    case Tok::Identifier: {
      Token nameTok = consume();
      std::string name = nameTok.text;
      if (cur().kind == Tok::LBracket) {
        consume();
        auto idxOpt = foldExpr();
        if (!expect(Tok::RBracket, "']'")) return;
        if (!idxOpt || !std::isfinite(*idxOpt) || std::floor(*idxOpt) != *idxOpt ||
            *idxOpt < 0 || *idxOpt > std::numeric_limits<int>::max()) {
          err("index must be a non-negative compile-time integer"); return;
        }
        if (cur().kind == Tok::Equals) {
          consume();
          if (cur().kind == Tok::Measure) {
            consume();
            auto qref = parseQubitRef();
            if (!qref) return;
            auto& [reg, idx2] = *qref;
            if (idx2 == -1 && (!qreg.count(reg) || qreg.at(reg).size() != 1)) {
              err("measurement into an indexed bit requires one explicit qubit"); return;
            }
            pd::ValueId vq = getQubitSlot(reg, idx2 == -1 ? 0 : idx2, cur());
            pd::ValueId vbit = b.measure(vq);
            setBitSlot(name, static_cast<int>(*idxOpt), vbit);
            if (cur().kind == Tok::Newline) consume();
            return;
          }
          err("only 'measure' allowed on rhs of indexed assignment");
          return;
        }
        err("expected '=' after indexed lhs");
        return;
      }
      if (cur().kind == Tok::LParen) {
        parseCallStmt(name); return;
      }
      if (cur().kind == Tok::Equals) {
        consume();
        if (cur().kind == Tok::Measure) {
          consume();
          auto qref = parseQubitRef();
          if (!qref) return;
          auto& [reg, idx] = *qref;
          if (idx == -1) {
            auto qit = qreg.find(reg);
            auto cit = creg.find(name);
            if (qit == qreg.end() || cit == creg.end()) {
              err("unknown register in 'measure' statement"); return;
            }
            if (qit->second.size() != cit->second.size()) {
              err("register size mismatch in 'measure' assignment"); return;
            }
            for (std::size_t i = 0; i < qit->second.size(); ++i) {
              setBitSlot(name, static_cast<int>(i), b.measure(qit->second[i]));
            }
          } else {
            pd::ValueId vq = getQubitSlot(reg, idx, cur());
            pd::ValueId vbit = b.measure(vq);
            auto cit = creg.find(name);
            if (cit == creg.end() || cit->second.size() != 1) {
              err("expected single-bit register on lhs"); return;
            }
            setBitSlot(name, 0, vbit);
          }
          if (cur().kind == Tok::Newline) consume();
          return;
        }
        --pos;  // parseAssignStmt owns the '=' and compile-time rebinding.
        parseAssignStmt(name);
        return;
      }
      err("unexpected statement starting with identifier '" + name + "'");
      while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) consume();
      return;
    }
    case Tok::Newline: consume(); return;
    case Tok::RBrace:  return;
    case Tok::Eof:     return;
    default:
      err("unexpected token '" + cur().text + "'");
      while (cur().kind != Tok::Newline && cur().kind != Tok::Eof) consume();
      return;
  }
}

void Parser::parseBlock() {
  skipNewlines();
  if (!expect(Tok::LBrace, "'{'")) return;
  skipNewlines();
  struct Guard {
    pd::OpId marker;pd::ValueId predicate;
    std::unordered_map<std::string,pd::ValueId> values;
    std::unordered_map<std::string,double> constants;
    std::unordered_map<std::string,pd::Type> types;
    BitBindings bits;Registers registers;
  };
  std::vector<Guard> guards;std::size_t guardedEpoch=0;
  while (cur().kind != Tok::RBrace && cur().kind != Tok::Eof) {
    if (fatal) return;
    if (inlineReturned()) {
      // A static return exits this function, including all surrounding
      // compile-time loops. Consume the rest without emitting operations.
      std::size_t depth = 0;
      while (cur().kind != Tok::Eof) {
        if (cur().kind == Tok::RBrace && depth == 0) break;
        const auto token = consume().kind;
        if (token == Tok::LBrace) ++depth;
        else if (token == Tok::RBrace) --depth;
      }
      break;
    }
    if(guards.empty()||guardedEpoch!=flowEpoch){
      std::optional<pd::ValueId> predicate;
      auto add=[&](const std::string& done){const auto available=b.cmp("==",classicals.at(done),b.constInt(0));predicate=predicate?b.binOp("&",*predicate,available):available;};
      for(const auto& loop:loopFrames)if(loop.functionDepth==inlineFrames.size()&&loop.mayTransfer)add(loop.done);
      if(!inlineFrames.empty()&&inlineFrames.back().normalizeReturns&&inlineFrames.back().mayReturn)add(inlineFrames.back().done);
      if(predicate){guards.push_back(Guard{b.beginIf(*predicate),*predicate,classicals,ctConst,scalarTypes,snapshotBits(),qreg});guardedEpoch=flowEpoch;}
    }
    const auto previous = pos;
    parseStmt();
    if (pos == previous) { err("parser could not consume statement"); consume(); }
    skipNewlines();
  }
  while(!guards.empty()){
    const auto guard=std::move(guards.back());guards.pop_back();
    const auto after=classicals;const auto bits=snapshotBits();b.endIf(guard.marker);
    ctConst=guard.constants;scalarTypes=guard.types;qreg=guard.registers;
    joinClassicals(guard.predicate,guard.values,after,guard.values);joinBits(guard.predicate,guard.bits,bits,guard.bits);
  }
  expect(Tok::RBrace, "'}'");
}

void Parser::parseProgram() {
  parseHeader();
  while (cur().kind != Tok::Eof && !fatal) {
    const auto previous = pos;
    parseStmt();
    if (pos == previous) { err("unexpected token outside a block"); consume(); }
    skipNewlines();
  }
  if(mod.numOps()>operationBudget)err("expanded operation budget exceeded; raise QSTACK_EXPANDED_OPERATION_BUDGET or reduce the program");
}

}  // namespace

ParseResult parse(std::string_view text, std::string_view filename) {
  Lexer lex(text);
  auto toks = lex.tokenize();
  Parser p(std::move(toks), std::string(filename));
  try {
    p.parseProgram();
  } catch (const std::exception& error) {
    p.err(std::string("invalid source: ") + error.what());
  }
  ParseResult r;
  r.diag = std::move(p.diag);
  if (!r.diag.hasErrors()) r.module = std::move(p.mod);
  return r;
}

}  // namespace phonon::parser

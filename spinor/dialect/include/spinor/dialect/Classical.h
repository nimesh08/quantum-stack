#pragma once
#include "spinor/dialect/Circuit.h"
#include "spinor/dialect/ExactInteger.h"
#include <sstream>
#include <unordered_map>

namespace spinor::dialect {
inline std::string stringAttribute(const Op& op,std::string_view name) {
  for(const auto& attr:op.attributes)if(attr.name==name) {
    if(const auto* value=std::get_if<std::string>(&attr.value))return *value;
    throw std::invalid_argument("classical attribute must be a string: "+std::string(name));
  }
  return {};
}
inline std::string stringAttribute(const WireOp& op,std::string_view name) {
  return stringAttribute(Op{op.kind,{},{},op.attributes,op.loc},name);
}
inline std::vector<std::string> classicalInputs(const WireOp& op) {
  std::vector<std::string> result;
  for(const auto& attr:op.attributes)if(attr.name=="input")result.push_back(std::get<std::string>(attr.value));
  return result;
}
inline std::vector<std::string> splitClassical(std::string_view text,char separator='|') {
  std::vector<std::string> fields;std::size_t start=0;
  do {auto end=text.find(separator,start);if(end==std::string_view::npos)end=text.size();
    fields.emplace_back(text.substr(start,end-start));start=end+1;
  }while(start<=text.size());return fields;
}
inline std::string classicalIndices(const std::vector<int>& indices) {
  std::string out;for(int index:indices){if(!out.empty())out+=',';out+=std::to_string(index);}return out;
}
inline std::vector<int> parseClassicalIndices(std::string_view text) {
  std::vector<int> result;if(text.empty())return result;
  for(const auto& part:splitClassical(text,',')){int value=parseExactInteger<int>(part);
    if(value<0)throw std::invalid_argument("negative classical storage index");result.push_back(value);}
  return result;
}
inline std::string encodeClassicalStorage(const ClassicalStorage& value) {
  return value.id+'|'+std::to_string(value.width)+'|'+classicalIndices(value.bits)+'|'+value.visibility+'|'+(value.initialized?"1":"0")+'|'+value.initialValue;
}
inline ClassicalStorage decodeClassicalStorage(std::string_view text) {
  auto f=splitClassical(text);if(f.size()!=6)throw std::invalid_argument("malformed classical storage metadata");
  return {f[0],parseExactInteger<std::uint32_t>(f[1]),parseClassicalIndices(f[2]),f[3],f[4]=="1",f[5]};
}
inline std::string encodeClassicalValue(const ClassicalValue& value) {
  return value.id+'|'+value.type+'|'+std::to_string(value.width)+'|'+classicalIndices(value.storage)+'|'+value.visibility+'|'+(value.initialized?"1":"0")+'|'+value.initialValue;
}
inline ClassicalValue decodeClassicalValue(std::string_view text) {
  auto f=splitClassical(text);if(f.size()!=7)throw std::invalid_argument("malformed classical value metadata");
  return {f[0],f[1],parseExactInteger<std::uint32_t>(f[2]),parseClassicalIndices(f[3]),f[4],f[5]=="1",f[6]};
}
inline std::string encodeClassicalOutput(const ClassicalOutput& value) {
  return value.name+'|'+value.value+'|'+value.type+'|'+std::to_string(value.width)+'|'+value.role;
}
inline ClassicalOutput decodeClassicalOutput(std::string_view text) {
  auto f=splitClassical(text);if(f.size()!=5)throw std::invalid_argument("malformed classical output metadata");
  return {f[0],f[1],f[2],parseExactInteger<std::uint32_t>(f[3]),f[4]};
}
inline std::uint64_t classicalMask(unsigned width) {
  if(width<1||width>64)throw std::invalid_argument("classical width must be in [1,64]");
  return width==64?~std::uint64_t(0):(std::uint64_t(1)<<width)-1;
}
inline std::uint64_t evaluateClassical(OpKind kind,const std::vector<std::uint64_t>& values,unsigned width) {
  const auto a=values.empty()?0:values[0],b=values.size()<2?0:values[1];
  std::uint64_t result;
  switch(kind){
    case OpKind::CCopy:case OpKind::CCast:result=a;break;
    case OpKind::CNot:result=~a;break;
    case OpKind::CAnd:result=a&b;break;case OpKind::COr:result=a|b;break;case OpKind::CXor:result=a^b;break;
    case OpKind::CAdd:result=a+b;break;case OpKind::CSub:result=a-b;break;
    case OpKind::CEq:result=a==b;break;case OpKind::CNe:result=a!=b;break;
    case OpKind::CLt:result=a<b;break;case OpKind::CLe:result=a<=b;break;
    case OpKind::CGt:result=a>b;break;case OpKind::CGe:result=a>=b;break;
    case OpKind::CShl:case OpKind::CShr:
      if(b>=width)throw std::invalid_argument("shift count exceeds classical width");
      result=kind==OpKind::CShl?a<<b:a>>b;break;
    case OpKind::CSelect:if(values.size()!=3)throw std::invalid_argument("select expects three inputs");result=a?values[1]:values[2];break;
    default:throw std::invalid_argument("unsupported classical evaluator operation");
  }
  return result&classicalMask(width);
}
} // namespace spinor::dialect

#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace spinor::dialect {
template<class Integer> inline Integer parseExactInteger(std::string_view text) {
  Integer value{};
  auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
  if(text.empty()||parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())
    throw std::invalid_argument("integer literal is outside its exact type or malformed: "+std::string(text));
  return value;
}
inline std::int64_t checkedInteger(std::string_view op,std::int64_t a,std::int64_t b) {
  constexpr auto lo=std::numeric_limits<std::int64_t>::min();
  constexpr auto hi=std::numeric_limits<std::int64_t>::max();
  if(op=="+") {
    if((b>0&&a>hi-b)||(b<0&&a<lo-b))throw std::overflow_error("integer addition overflow");
    return a+b;
  }
  if(op=="-") {
    if((b<0&&a>hi+b)||(b>0&&a<lo+b))throw std::overflow_error("integer subtraction overflow");
    return a-b;
  }
  if(op=="*") {
    if(!a||!b)return 0;
    if((a==-1&&b==lo)||(b==-1&&a==lo))throw std::overflow_error("integer multiplication overflow");
    if(a>0 ? (b>0 ? a>hi/b : b<lo/a) : (b>0 ? a<lo/b : a<hi/b))
      throw std::overflow_error("integer multiplication overflow");
    return a*b;
  }
  if(op=="/") {
    if(!b)throw std::domain_error("integer division by zero");
    if(a==lo&&b==-1)throw std::overflow_error("integer division overflow");
    return a/b;
  }
  throw std::invalid_argument("unsupported exact integer operation");
}
} // namespace spinor::dialect

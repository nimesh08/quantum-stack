#include "spinor/passes/CompilationReport.h"
#include "TwoQubitMath.h"
#include <algorithm>
#include <bit>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <type_traits>

namespace spinor::passes {
namespace {
thread_local CompilationReport* sink = nullptr;
std::string quoted(const std::string& value) {
  std::ostringstream out; out << '"';
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') out << '\\' << char(c);
    else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
    else out << char(c);
  }
  out << '"'; return out.str();
}
std::string digest(const std::vector<dialect::WireOp>& ops, double phase) {
  // Stable semantic fingerprint, not a cryptographic integrity guarantee.
  std::ostringstream data; data.imbue(std::locale::classic());
  data << std::hex << std::bit_cast<std::uint64_t>(phase) << ';';
  for (const auto& op : ops) {
    data << dialect::opMnemonic(op.kind) << ':';
    for (int q : op.qubits) data << q << ',';
    auto attrs=op.attributes;
    std::sort(attrs.begin(),attrs.end(),[](const auto& a,const auto& b){return a.name<b.name;});
    for (const auto& a : attrs) {
      data << a.name << '=';
      std::visit([&](const auto& value){
        using T=std::decay_t<decltype(value)>;
        if constexpr(std::is_same_v<T,double>)data<<"f64:"<<std::bit_cast<std::uint64_t>(value);
        else if constexpr(std::is_same_v<T,std::string>)data<<"str:"<<quoted(value);
        else if constexpr(std::is_signed_v<T>)data<<"i64:"<<value;
        else data<<"u64:"<<value;
      },a.value);
      data << ';';
    }
    data << '|';
  }
  std::uint64_t hash=14695981039346656037ull;
  for (unsigned char c:data.str()) { hash^=c; hash*=1099511628211ull; }
  std::ostringstream result; result << std::hex << std::setw(16) << std::setfill('0') << hash;
  return result.str();
}
template<class Matrix> double frobenius(const Matrix& before,const Matrix& after) {
  double value=0;
  for (std::size_t i=0;i<before.e.size();++i) value=std::hypot(value,std::abs(before.e[i]-after.e[i]));
  return value;
}
}
CompilationReport* currentCompilationReport(){return sink;}
CompilationReportScope::CompilationReportScope(CompilationReport* report):previous_(sink){sink=report;}
CompilationReportScope::~CompilationReportScope(){sink=previous_;}
void CompilationReport::addGap(const std::string& message){
  if(std::find(gaps.begin(),gaps.end(),message)==gaps.end())gaps.push_back(message);
}
bool isUnitaryInstruction(const dialect::WireOp& op){
  if(op.kind==dialect::OpKind::GlobalPhase)return true;
  try { if(op.qubits.size()==1){matrix1(op);return true;}
        if(op.qubits.size()==2){matrix2(op);return true;} }
  catch(const std::exception&){}
  return false;
}
bool isNumericalRegionBoundary(const dialect::WireOp& op){return !isUnitaryInstruction(op);}
std::string numericalRegion(std::size_t index){return "unitary-"+std::to_string(index);}
void observeRewrite(const std::string& pass,const std::string& rule,
    const std::vector<dialect::WireOp>& before,const std::vector<dialect::WireOp>& after,
    double beforePhase,double afterPhase,std::optional<double> threshold,const std::string& region){
  if(!sink)return;
  RewriteObservation event{sink->stage+"."+pass,rule,region,digest(before,beforePhase),digest(after,afterPhase)};
  if(event.inputDigest==event.outputDigest)return;
  event.phaseDelta=afterPhase-beforePhase;event.threshold=threshold;
  std::set<int> wires;
  for(const auto* seq:{&before,&after})for(const auto& op:*seq)for(int q:op.qubits)wires.insert(q);
  event.arity=wires.size();
  try {
    if(wires.size()>2)throw std::runtime_error("rewrite exceeds the two-qubit observation limit");
    auto scalar=[](const std::vector<dialect::WireOp>& seq,double phase){
      auto z=std::polar(1.0,phase);
      for(const auto& op:seq)if(op.kind==dialect::OpKind::GlobalPhase)z*=std::polar(1.0,dialect::parameter(op));
      return z;
    };
    if(wires.empty()){
      for(const auto* seq:{&before,&after})for(const auto& op:*seq)
        if(op.kind!=dialect::OpKind::GlobalPhase)throw std::runtime_error("nonunitary or classical rewrite");
      event.residual=std::abs(scalar(before,beforePhase)-scalar(after,afterPhase));
    }else if(wires.size()==1){
      auto compose=[&](const auto& seq,double phase){auto u=la::identity2();
        for(const auto& op:seq)if(op.kind!=dialect::OpKind::GlobalPhase)u=la::mul2(matrix1(op),u);
        auto z=scalar(seq,phase);for(auto& x:u.e)x*=z;return u;};
      event.residual=frobenius(compose(before,beforePhase),compose(after,afterPhase));
    }else{
      auto compose=[&](const auto& seq,double phase){auto u=la::identity4();
        for(const auto& op:seq)if(op.kind!=dialect::OpKind::GlobalPhase)
          u=la::mul4(twoq::operationMatrix(op,*wires.begin(),*wires.rbegin()),u);
        return twoq::scale(u,scalar(seq,phase));};
      event.residual=frobenius(compose(before,beforePhase),compose(after,afterPhase));
    }
    if(!std::isfinite(*event.residual))throw std::runtime_error("nonfinite reconstruction residual");
  }catch(const std::exception& e){event.residual.reset();event.unmeasuredReason=e.what();}
  sink->rewrites.push_back(std::move(event));
}
std::string CompilationReport::json() const {
  std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(17);
  out<<"{\"schema_version\":"<<schemaVersion<<",\"certified\":false,\"approximation_error_budget\":0,"
        "\"input_semantics\":\"represented-binary64\",\"digest_algorithm\":\"fnv1a64-semantic-v1\","
        "\"evaluation_method\":\"binary64-complete-phase\",\"precision_bits\":53,"
        "\"metric\":\"frobenius\",\"whole_program_error\":null,"
        "\"coverage\":{\"complete\":false,\"gaps\":[";
  // No claim of complete floating-point coverage: scalar accumulation,
  // placement/permutation and serializer rounding need separate observations.
  out<<quoted("No certified or complete end-to-end rounding coverage");
  for(const auto& gap:gaps)out<<','<<quoted(gap);
  out<<"],\"has_nonunitary_operations\":"<<(hasNonunitaryOperations?"true":"false")<<"},\"rewrites\":[";
  bool comma=false;
  struct Aggregate{std::size_t checked=0,unmeasured=0;double sum=0,maximum=0;};
  std::map<std::string,Aggregate> regions;
  for(const auto& event:rewrites){
    if(comma)out<<',';comma=true;
    out<<"{\"pass\":"<<quoted(event.pass)<<",\"rule\":"<<quoted(event.rule)<<",\"region\":"<<quoted(event.region)
       <<",\"arity\":"<<event.arity<<",\"input_digest\":"<<quoted(event.inputDigest)<<",\"output_digest\":"<<quoted(event.outputDigest)
       <<",\"phase_delta\":"<<event.phaseDelta<<",\"reconstruction_threshold\":";
    if(event.threshold)out<<*event.threshold;else out<<"null";
    out<<",\"observed_frobenius_residual\":";
    auto& aggregate=regions[event.region];
    if(event.residual){out<<*event.residual;++aggregate.checked;aggregate.sum+=*event.residual;aggregate.maximum=std::max(aggregate.maximum,*event.residual);}
    else{out<<"null";++aggregate.unmeasured;}
    out<<",\"unmeasured_reason\":"<<(event.unmeasuredReason.empty()?"null":quoted(event.unmeasuredReason))<<'}';
  }
  out<<"],\"regions\":[";comma=false;
  for(const auto& [region,a]:regions){if(comma)out<<',';comma=true;
    out<<"{\"id\":"<<quoted(region)<<",\"accepted_rewrites\":"<<a.checked+a.unmeasured
       <<",\"checked_rewrites\":"<<a.checked<<",\"unmeasured_rewrites\":"<<a.unmeasured
       <<",\"sum_observed_local_residuals\":";
    // These are sums of the accepted local observations assigned to this
    // region, not a composition theorem or an estimate for a dynamic path.
    // A measurement elsewhere in the program cannot erase that evidence.
    if(!a.checked)out<<"null";else out<<a.sum;
    out<<",\"max_observed_local_residual\":";
    if(a.checked)out<<a.maximum;else out<<"null";
    out<<",\"aggregation_available\":"<<(a.checked?"true":"false")<<'}';}
  out<<"],\"trial_statistics\":{";comma=false;
  for(const auto& [key,value]:counters){if(comma)out<<',';comma=true;out<<quoted(key)<<':'<<value;}
  out<<"},\"notes\":{";comma=false;
  for(const auto& [key,value]:notes){if(comma)out<<',';comma=true;out<<quoted(key)<<':'<<quoted(value);}
  out<<"}}";return out.str();
}
}

#include "spinor/passes/ClassicalStorageReuse.h"
#include "spinor/passes/CompilationReport.h"
#include "spinor/dialect/Classical.h"
#include <map>
#include <set>
#include <numeric>

namespace spinor::passes {
using namespace dialect;
namespace {
using Names=std::set<std::string>;
void unite(Names& into,const Names& from){into.insert(from.begin(),from.end());}
bool intersects(const std::vector<int>& a,const std::vector<int>& b){
  for(int bit:a)if(std::find(b.begin(),b.end(),bit)!=b.end())return true;return false;
}
}
Module ClassicalStorageReuse::run(const Module& module) const {
  auto c=flatten(module);
  if(c.classicalValues.empty())return module;
  const auto n=c.instructions.size();
  std::map<std::string,std::size_t> values;
  for(std::size_t i=0;i<c.classicalValues.size();++i)values.emplace(c.classicalValues[i].id,i);
  std::vector<Names> uses(n+1),defs(n+1),liveIn(n+1),liveOut(n+1);
  std::vector<std::vector<std::size_t>> successors(n+1);
  std::set<int> pinned(c.exportedClbits.begin(),c.exportedClbits.end());
  for(const auto& v:c.classicalValues)if(v.visibility!="private"||v.initialized)
    pinned.insert(v.storage.begin(),v.storage.end());
  for(const auto& storage:c.classicalStorage)if(storage.visibility!="private")
    pinned.insert(storage.bits.begin(),storage.bits.end());
  for(const auto& output:c.classicalOutputs){
    uses[n].insert(output.value);
    const auto& value=c.classicalValues.at(values.at(output.value));
    pinned.insert(value.storage.begin(),value.storage.end());
  }
  struct Branch{std::size_t start,alternative;};
  std::vector<Branch> stack;
  for(std::size_t i=0;i<n;++i){
    const auto& op=c.instructions[i];
    successors[i]={i+1};
    auto inputs=classicalInputs(op);uses[i].insert(inputs.begin(),inputs.end());
    const auto condition=stringAttribute(op,"condition"),result=stringAttribute(op,"result");
    if(!condition.empty())uses[i].insert(condition);
    if(!result.empty())defs[i].insert(result);
    if((op.kind==OpKind::If&&condition.empty())||(op.kind==OpKind::Measure&&result.empty()))
      pinned.insert(op.clbit);
    if(op.kind==OpKind::If)stack.push_back({i,n});
    else if(op.kind==OpKind::Else){
      if(stack.empty())throw std::invalid_argument("unmatched classical liveness else");
      stack.back().alternative=i;
    }else if(op.kind==OpKind::EndIf){
      if(stack.empty())throw std::invalid_argument("unmatched classical liveness endif");
      const auto branch=stack.back();stack.pop_back();
      successors[branch.start]={branch.start+1,branch.alternative==n?i:branch.alternative+1};
      if(branch.alternative!=n)successors[branch.alternative]={i+1};
    }
  }
  if(!stack.empty())throw std::invalid_argument("unclosed classical liveness branch");
  // Guarded-loop lowering is forward-only, but a fixed point also handles CFG
  // join propagation without relying on textual lifetime intervals.
  bool changed=true;
  while(changed){changed=false;
    for(std::size_t k=n+1;k-->0;){
      Names out;for(auto s:successors[k])unite(out,liveIn[s]);
      Names in=uses[k];for(const auto& value:out)if(!defs[k].count(value))in.insert(value);
      if(out!=liveOut[k]||in!=liveIn[k]){changed=true;liveOut[k]=std::move(out);liveIn[k]=std::move(in);}
    }
  }
  std::map<std::string,Names> interference;
  auto clique=[&](const Names& live){for(const auto& a:live)for(const auto& b:live)if(a!=b)interference[a].insert(b);};
  for(std::size_t i=0;i<=n;++i){clique(liveIn[i]);clique(liveOut[i]);
    for(const auto& a:defs[i])for(const auto& b:liveOut[i])if(a!=b){interference[a].insert(b);interference[b].insert(a);}
  }
  std::vector<std::size_t> order(c.classicalValues.size());std::iota(order.begin(),order.end(),0);
  std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){
    const auto& x=c.classicalValues[a];const auto& y=c.classicalValues[b];
    return std::make_pair(interference[x.id].size(),x.width)>std::make_pair(interference[y.id].size(),y.width);
  });
  std::map<std::string,std::vector<int>> colors;
  for(auto index:order){const auto& value=c.classicalValues[index];
    if(std::any_of(value.storage.begin(),value.storage.end(),[&](int bit){return pinned.count(bit);}))colors[value.id]=value.storage;
  }
  for(auto index:order){auto& value=c.classicalValues[index];if(colors.count(value.id))continue;
    std::vector<int> candidate;
    for(std::size_t start=0;start+value.width<=c.numClbits;++start){
      candidate.clear();bool legal=true;
      for(unsigned offset=0;offset<value.width;++offset){int bit=static_cast<int>(start+offset);candidate.push_back(bit);if(pinned.count(bit))legal=false;}
      if(!legal)continue;
      for(const auto& neighbor:interference[value.id])if(colors.count(neighbor)&&intersects(candidate,colors[neighbor])){legal=false;break;}
      if(legal)break;candidate.clear();
    }
    // A conservative graph or a fragmented pool can need the original mapping.
    if(candidate.empty())return module;
    colors[value.id]=candidate;
  }
  std::size_t bitsBefore=c.numClbits;
  std::map<int,std::pair<bool,bool>> initialization;
  for(const auto& storage:c.classicalStorage){
    const auto initial=storage.initialized?parseExactInteger<std::uint64_t>(storage.initialValue):0;
    for(std::size_t i=0;i<storage.bits.size();++i)initialization[storage.bits[i]]={storage.initialized,((initial>>i)&1)!=0};
  }
  std::set<int> occupied=pinned;
  for(auto& value:c.classicalValues){value.storage=colors.at(value.id);occupied.insert(value.storage.begin(),value.storage.end());}
  for(auto& op:c.instructions){
    const auto result=stringAttribute(op,"result"),condition=stringAttribute(op,"condition");
    if(op.kind==OpKind::Measure&&!result.empty()){
      op.clbit=colors.at(result).at(0);
      for(auto& a:op.attributes)if(a.name=="clbit")a.value=double(op.clbit);
    }
    if(op.kind==OpKind::If&&!condition.empty()){
      op.clbit=colors.at(condition).at(0);
      for(auto& a:op.attributes)if(a.name=="condition_clbit")a.value=double(op.clbit);
    }
  }
  c.numClbits=occupied.empty()?0:std::size_t(*occupied.rbegin()+1);
  c.classicalStorage.clear();
  for(std::size_t bit=0;bit<c.numClbits;++bit){
    const bool fixed=pinned.count(static_cast<int>(bit));
    const auto init=initialization[static_cast<int>(bit)];
    c.classicalStorage.push_back({"s"+std::to_string(bit),1,{static_cast<int>(bit)},fixed?"exported":"private",fixed&&init.first,init.second?"1":"0"});
  }
  if(auto* report=currentCompilationReport()){
    report->counters["private_storage_bits_before"]=bitsBefore;
    report->counters["private_storage_bits_after"]=c.numClbits;
    report->notes["classical_storage_analysis"]="backward-CFG-liveness, greedy-interference-coloring; exported slots pinned";
  }
  return rebuild(c);
}
}

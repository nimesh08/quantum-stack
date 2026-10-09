#include "spinor/passes/Routing.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/passes/CompilationReport.h"
#include "spinor/passes/BranchJoin.h"
#include "spinor/passes/Decomposition.h"
#include <queue>
#include <set>
#include <stdexcept>

namespace spinor::passes {
RoutingResult Routing::run(const dialect::Module& input,const registry::ChipInfo& chip,
                           const CouplingGraph& graph,Layout layout) const {
  using namespace dialect;
  auto in=flatten(input),out=in;out.instructions.clear();out.target=chip.id;out.numQubits=graph.qubits();
  if(in.numQubits>graph.qubits()||layout.v2p.size()!=in.numQubits)
    throw std::runtime_error("routing: circuit exceeds target qubit capacity");
  for(int p:layout.v2p)if(p<0||std::size_t(p)>=graph.qubits())throw std::runtime_error("routing: incomplete placement");
  RoutingResult result;result.initialLayout=layout;result.swapCount=0;
  auto countSwap=[&]{
    ++result.swapCount;
  };
  out.initialLayout=layout.v2p;
  struct Branch {
    Layout entry,thenExit;
    std::vector<std::pair<int,int>> swaps,thenSwaps;
    std::vector<std::size_t> thenCompensation;
    bool hasElse=false;
  };
  std::vector<Branch> branches;
  std::set<std::size_t> remove;
  std::map<RoutingEdge,std::optional<JoinNativeCost>> nativeSwapCosts;
  auto nativeSwapCost=[&](RoutingEdge edge)->std::optional<JoinNativeCost>{
    if(auto known=nativeSwapCosts.find(edge);known!=nativeSwapCosts.end())return known->second;
    CompilationReportScope speculative(nullptr);
    WireCircuit probe;probe.numQubits=graph.qubits();probe.instructions={{OpKind::Swap,{edge.first,edge.second},{},{}}};
    Diagnostics diagnostics;std::optional<JoinNativeCost> count;
    try{auto native=flatten(Decomposition{}.run(rebuild(probe),chip,diagnostics));
      if(!diagnostics.hasErrors()){count=JoinNativeCost{};for(const auto& op:native.instructions)if(!op.qubits.empty()){
        count->first+=op.qubits.size()==2;++count->second;}}
    }catch(const std::exception&){}
    nativeSwapCosts[edge]=count;return count;
  };
  auto restore=[&](std::vector<std::size_t>* positions=nullptr){
    for(auto it=branches.back().swaps.rbegin();it!=branches.back().swaps.rend();++it){
      if(positions)positions->push_back(out.instructions.size());
      out.instructions.push_back({OpKind::Swap,{it->first,it->second},{},{}});
      layout.apply_swap(it->first,it->second);countSwap();
    }
    branches.back().swaps.clear();
  };
  for(auto op:in.instructions){
    if(op.kind==OpKind::If){branches.push_back({layout,{},{},{},{},false});out.instructions.push_back(op);continue;}
    if(op.kind==OpKind::Else){
      if(branches.empty()||branches.back().hasElse)throw std::runtime_error("unbalanced conditional markers");
      auto& branch=branches.back();branch.hasElse=true;branch.thenExit=layout;branch.thenSwaps=branch.swaps;
      restore(&branch.thenCompensation);out.instructions.push_back(op);continue;
    }
    if(op.kind==OpKind::EndIf){
      if(branches.empty())throw std::runtime_error("unbalanced conditional markers");
      auto& branch=branches.back();
      if(branch.hasElse){
        const auto join=chooseSharedBranchJoin(branch.entry,branch.thenSwaps,branch.swaps,nativeSwapCost);
        const auto retainedThen=branch.thenSwaps.size()-join.thenPrefix;
        remove.insert(branch.thenCompensation.begin()+retainedThen,branch.thenCompensation.end());
        result.swapCount-=join.thenPrefix;
        for(std::size_t index=branch.swaps.size();index>join.elsePrefix;--index){
          const auto edge=branch.swaps[index-1];out.instructions.push_back({OpKind::Swap,{edge.first,edge.second},{},{}});
          layout.apply_swap(edge.first,edge.second);countSwap();
        }
        auto net=join.netPath;
        if(auto report=currentCompilationReport()){
          report->counters["branch_join_swaps_removed"]+=join.thenPrefix+join.elsePrefix;
          report->counters["branch_join_candidates"]+=join.candidates;
          report->counters["branch_join_prefix_limit"]=4096;
          report->counters["branch_join_budget_truncated"]+=join.truncated;
        }
        branches.pop_back();
        if(!branches.empty())branches.back().swaps.insert(branches.back().swaps.end(),net.begin(),net.end());
      }else{
        if(layout.v2p!=branch.entry.v2p)restore();
        else if(auto report=currentCompilationReport())report->counters["branch_join_swaps_removed"]+=branch.swaps.size();
        branches.pop_back();
      }
      out.instructions.push_back(op);continue;
    }
    if(op.qubits.size()==2&&op.kind!=OpKind::Barrier){
      int a=layout.v2p.at(op.qubits[0]),b=layout.v2p.at(op.qubits[1]);
      if(a==b)throw std::runtime_error("routing: two-qubit gate has duplicate operands");
      if(!graph.connected(a,b)){
        std::vector<int> prev(graph.qubits(),-1);std::queue<int> todo;todo.push(a);prev[a]=a;
        while(!todo.empty()&&prev[b]<0){int p=todo.front();todo.pop();for(int n:graph.neighbours(p))if(prev[n]<0){prev[n]=p;todo.push(n);}}
        if(prev[b]<0)throw std::runtime_error("routing: no connected path between required qubits");
        std::vector<int> path;for(int p=b;p!=a;p=prev[p])path.push_back(p);path.push_back(a);
        std::reverse(path.begin(),path.end());
        for(std::size_t i=0;i+2<path.size();++i){
          out.instructions.push_back({OpKind::Swap,{path[i],path[i+1]},{},op.loc});
          layout.apply_swap(path[i],path[i+1]);countSwap();
          if(!branches.empty())branches.back().swaps.emplace_back(path[i],path[i+1]);
        }
      }
    }
    for(int& q:op.qubits)q=layout.v2p.at(q);
    // Keep source order, including classical writes and global barriers.
    out.instructions.push_back(std::move(op));
  }
  if(!branches.empty())throw std::runtime_error("unclosed conditional markers");
  if(chip.placement.maxSwaps&&result.swapCount>*chip.placement.maxSwaps)
    throw std::runtime_error("PLACEMENT_SWAP_LIMIT: routing exceeds the configured maximum SWAP count");
  std::vector<WireOp> instructions;instructions.reserve(out.instructions.size()-remove.size());
  for(std::size_t index=0;index<out.instructions.size();++index)if(!remove.contains(index))instructions.push_back(std::move(out.instructions[index]));
  out.instructions=std::move(instructions);
  out.finalLayout=layout.v2p;
  result.finalLayout=layout;result.module=rebuild(out);return result;
}
} // namespace spinor::passes

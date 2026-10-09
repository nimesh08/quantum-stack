#include "spinor/passes/Routing.h"
#include "spinor/dialect/Circuit.h"
#include <queue>
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
  out.initialLayout=layout.v2p;
  std::vector<std::vector<std::pair<int,int>>> branchSwaps;
  auto restore=[&]{
    for(auto it=branchSwaps.back().rbegin();it!=branchSwaps.back().rend();++it){
      out.instructions.push_back({OpKind::Swap,{it->first,it->second},{},{}});
      layout.apply_swap(it->first,it->second);++result.swapCount;
    }
    branchSwaps.back().clear();
  };
  for(auto op:in.instructions){
    if(op.kind==OpKind::If){branchSwaps.emplace_back();out.instructions.push_back(op);continue;}
    if(op.kind==OpKind::Else||op.kind==OpKind::EndIf){
      if(branchSwaps.empty())throw std::runtime_error("unbalanced conditional markers");
      restore();if(op.kind==OpKind::EndIf)branchSwaps.pop_back();out.instructions.push_back(op);continue;
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
          layout.apply_swap(path[i],path[i+1]);++result.swapCount;
          if(!branchSwaps.empty())branchSwaps.back().emplace_back(path[i],path[i+1]);
        }
      }
    }
    for(int& q:op.qubits)q=layout.v2p.at(q);
    // Keep source order, including classical writes and global barriers.
    out.instructions.push_back(std::move(op));
  }
  out.finalLayout=layout.v2p;
  result.finalLayout=layout;result.module=rebuild(out);return result;
}
} // namespace spinor::passes

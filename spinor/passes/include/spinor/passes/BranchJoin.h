#pragma once
#include "spinor/passes/Placement.h"
#include <algorithm>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

namespace spinor::passes {
using RoutingEdge=std::pair<int,int>;
using JoinNativeCost=std::pair<std::size_t,std::size_t>; // two-qubit, all native gates
struct SharedBranchJoin {
  std::size_t thenPrefix=0,elsePrefix=0,candidates=1;
  std::vector<RoutingEdge> netPath;
  bool truncated=false;
};

// Transfer candidates are prefixes of already validated routing histories.
// The entry layout is always available; search never invents a new SWAP edge.
template<class Cost>
SharedBranchJoin chooseSharedBranchJoin(const Layout& entry,
    const std::vector<RoutingEdge>& thenPath,const std::vector<RoutingEdge>& elsePath,Cost cost){
  constexpr std::size_t prefixLimit=4096;
  struct Prefix {std::size_t first=0,last=0;JoinNativeCost firstCost{},lastCost{};};
  struct History {std::map<std::vector<int>,Prefix> positions;JoinNativeCost total{};bool valid=true;};
  auto collect=[&](const auto& path){
    History history;auto layout=entry;history.positions[layout.v2p]={};
    for(std::size_t i=0;i<path.size();++i){
      const auto value=cost(path[i]);if(!value){history.valid=false;return history;}
      history.total.first+=value->first;history.total.second+=value->second;
      layout.apply_swap(path[i].first,path[i].second);
      if(i+1>prefixLimit&&i+1!=path.size())continue;
      auto [position,inserted]=history.positions.try_emplace(layout.v2p,Prefix{i+1,i+1,history.total,history.total});
      if(!inserted){position->second.last=i+1;position->second.lastCost=history.total;}
    }
    return history;
  };
  SharedBranchJoin result;result.truncated=thenPath.size()>prefixLimit||elsePath.size()>prefixLimit;
  const auto yes=collect(thenPath),no=collect(elsePath);
  if(!yes.valid||!no.valid)return result;
  const JoinNativeCost baseline{yes.total.first+no.total.first,yes.total.second+no.total.second};
  auto best=std::tuple{baseline.first,baseline.second,thenPath.size()+elsePath.size(),entry.v2p};
  result.candidates=0;
  for(const auto& [layout,a]:yes.positions){
    const auto found=no.positions.find(layout);if(found==no.positions.end())continue;
    ++result.candidates;const auto& b=found->second;
    const JoinNativeCost remaining{baseline.first-a.lastCost.first-b.lastCost.first,
                                   baseline.second-a.lastCost.second-b.lastCost.second};
    // Component-wise acceptance, even if later ranking priorities change.
    if(remaining.first>baseline.first||remaining.second>baseline.second)continue;
    const auto candidate=std::tuple{remaining.first,remaining.second,thenPath.size()-a.last+elsePath.size()-b.last,layout};
    if(candidate>=best)continue;
    best=candidate;result.thenPrefix=a.last;result.elsePrefix=b.last;
    if(std::tuple{a.firstCost.first,a.firstCost.second,a.first}<=std::tuple{b.firstCost.first,b.firstCost.second,b.first})
      result.netPath.assign(thenPath.begin(),thenPath.begin()+a.first);
    else result.netPath.assign(elsePath.begin(),elsePath.begin()+b.first);
  }
  return result;
}
} // namespace spinor::passes

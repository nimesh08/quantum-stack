#include "spinor/passes/HeterogeneousRouting.h"
#include "spinor/passes/AvailableRouting.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/BranchJoin.h"
#include "spinor/registry/ComponentTopology.h"
#include "spinor/dialect/Resonators.h"
#include <algorithm>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <tuple>

namespace spinor::passes {
namespace {
using namespace dialect;
using Pair=std::pair<int,int>;
std::string gateName(OpKind kind){return std::string(opMnemonic(kind)).substr(7);}
bool symmetric(const WireOp& op){
  switch(op.kind){
    case OpKind::Cz:case OpKind::Swap:case OpKind::Rxx:case OpKind::Rzz:
    case OpKind::ISwap:case OpKind::SqrtISwap:case OpKind::SqrtISwapInv:case OpKind::Syc:return true;
    case OpKind::Ms:return op.attributes.empty();
    default:return false;
  }
}
struct Recipe{std::vector<WireOp> ops;double phase=0;};
struct Recipes {
  const registry::ChipInfo& chip;
  std::set<int> active;
  std::map<Pair,std::optional<Recipe>> swaps;
  bool singleton(const std::string& name,int q) const {
    auto found=chip.singleQubitGateLoci.find(name);
    return active.contains(q)&&(found==chip.singleQubitGateLoci.end()||
      std::find(found->second.begin(),found->second.end(),q)!=found->second.end());
  }
  bool pair(const std::string& name,int a,int b) const {
    if(a==b||!active.contains(a)||!active.contains(b))return false;
    const auto found=chip.twoQubitGateLoci.find(name);
    if(found!=chip.twoQubitGateLoci.end())return std::find(found->second.begin(),found->second.end(),Pair{a,b})!=found->second.end();
    if(name=="move")return std::find(chip.moveLoci.begin(),chip.moveLoci.end(),Pair{a,b})!=chip.moveLoci.end();
    if(name=="cz"&&!chip.resonatorQubits.empty())return std::find(chip.czLoci.begin(),chip.czLoci.end(),Pair{a,b})!=chip.czLoci.end();
    if(chip.allToAll)return true;
    if(std::find(chip.coupling.begin(),chip.coupling.end(),Pair{a,b})!=chip.coupling.end())return true;
    return !chip.directedConnectivity&&name!="cx"&&name!="ecr"&&name!="move"&&
      std::find(chip.coupling.begin(),chip.coupling.end(),Pair{b,a})!=chip.coupling.end();
  }
  std::optional<Recipe> cz(int a,int b) const {
    if(pair("cz",a,b))return Recipe{{{OpKind::Cz,{a,b},{},{}}},0};
    if(pair("cz",b,a))return Recipe{{{OpKind::Cz,{b,a},{},{}}},0};
    for(int r:chip.resonatorQubits)for(auto [moved,other]:{Pair{a,b},Pair{b,a}}){
      if(!pair("move",moved,r))continue;
      Pair locus{other,r};
      if(!pair("cz",locus.first,locus.second)){
        locus={r,other};if(!pair("cz",locus.first,locus.second))continue;
      }
      return Recipe{{{OpKind::Move,{moved,r},{},{}},{OpKind::Cz,{locus.first,locus.second},{},{}},
                      {OpKind::Move,{moved,r},{},{}}},0};
    }
    return std::nullopt;
  }
  std::optional<Recipe> make(WireOp requested) const {
    // Speculative recipes must not enter the accepted numerical report.
    CompilationReportScope speculative(nullptr);
    try {
      if(requested.qubits.empty()||requested.kind==OpKind::Barrier)return Recipe{{requested},0};
      if(requested.qubits.size()==1&&(requested.kind==OpKind::Measure||requested.kind==OpKind::Reset)){
        if(!singleton(gateName(requested.kind),requested.qubits[0]))return std::nullopt;
        return Recipe{{requested},0};
      }
      std::vector<std::string> entanglers{chip.decompose.twoQubitEntangler};
      for(const auto& name:chip.nativeGates)if(chip.twoQubitGateLoci.contains(name)||
          name=="cz"||name=="cx"||name=="ecr"||name=="rxx"||name=="rzz"||name=="ms"||name=="swap"||
          name=="iswap"||name=="sqrt_iswap"||name=="sqrt_iswap_inv"||name=="syc")
        if(std::find(entanglers.begin(),entanglers.end(),name)==entanglers.end())entanglers.push_back(name);
      if(requested.qubits.size()==1)entanglers={""};
      std::optional<Recipe> best;
      for(const auto& entangler:entanglers){
        auto local=chip;
        local.decompose.twoQubitEntangler=entangler;
        const int a=requested.qubits[0],b=requested.qubits.size()==2?requested.qubits[1]:a;
        auto mediated=entangler=="cz"&&requested.qubits.size()==2?cz(a,b):std::nullopt;
        if(requested.qubits.size()==2&&!mediated&&!pair(entangler,a,b)&&!pair(entangler,b,a))continue;
        std::erase_if(local.nativeGates,[&](const std::string& name){
          if(chip.twoQubitGateLoci.contains(name)||name=="cx"||name=="cz"||name=="ecr"||name=="swap"||name=="move"||name=="rxx"||name=="rzz"||name=="ms"||name=="iswap"||name=="sqrt_iswap"||name=="sqrt_iswap_inv"||name=="syc")
            return name!=entangler;
          return requested.qubits.size()==1&&!singleton(name,a);
        });
        local.allToAll=false;local.directedConnectivity=true;local.coupling.clear();
        if(mediated){local.coupling={{a,b},{b,a}};}
        else {if(pair(entangler,a,b))local.coupling.emplace_back(a,b);if(pair(entangler,b,a))local.coupling.emplace_back(b,a);}
        WireCircuit input;input.numQubits=chip.qubits;input.numClbits=requested.clbit>=0?requested.clbit+1:0;
        input.instructions={requested};
        if(symmetric(input.instructions[0])&&!mediated&&!pair(gateName(requested.kind),a,b)&&pair(gateName(requested.kind),b,a))
          std::swap(input.instructions[0].qubits[0],input.instructions[0].qubits[1]);
        Diagnostics diag;auto decomposed=flatten(Decomposition{}.run(rebuild(input),local,diag));
        if(diag.hasErrors())continue;
        Recipe recipe;recipe.phase=decomposed.globalPhase;bool valid=true;
        for(auto op:decomposed.instructions){
          if(op.qubits.size()==1&&!singleton(gateName(op.kind),op.qubits[0])){
            auto replacement=make(op);
            if(!replacement){valid=false;break;}
            recipe.phase+=replacement->phase;recipe.ops.insert(recipe.ops.end(),replacement->ops.begin(),replacement->ops.end());
          }else if(op.qubits.size()==2){
            if(op.kind==OpKind::Cz&&mediated){
              auto implementation=cz(op.qubits[0],op.qubits[1]);
              if(!implementation){valid=false;break;}
              recipe.ops.insert(recipe.ops.end(),implementation->ops.begin(),implementation->ops.end());
            }else{
              if(!pair(gateName(op.kind),op.qubits[0],op.qubits[1])){
                if(symmetric(op)&&pair(gateName(op.kind),op.qubits[1],op.qubits[0]))std::swap(op.qubits[0],op.qubits[1]);
                else{valid=false;break;}
              }
              recipe.ops.push_back(std::move(op));
            }
          }else recipe.ops.push_back(std::move(op));
        }
        if(valid&&(!best||recipe.ops.size()<best->ops.size()))best=std::move(recipe);
      }
      return best;
    }catch(const std::exception&){return std::nullopt;}
  }
  const std::optional<Recipe>& swap(int a,int b){
    auto key=std::minmax(a,b);Pair ordered{key.first,key.second};
    auto found=swaps.find(ordered);
    if(found==swaps.end())found=swaps.emplace(ordered,make({OpKind::Swap,{ordered.first,ordered.second},{},{}})).first;
    return found->second;
  }
};
struct BranchState {
  Layout entry,thenExit;
  std::vector<Pair> path,thenPath;
  std::vector<std::pair<std::size_t,std::size_t>> compensationGroups;
  bool hasElse=false;
};
struct State {
  WireCircuit circuit;Layout layout;
  std::vector<BranchState> branches;
  std::set<std::size_t> omitted;
  std::size_t swapCount=0,forwardSwaps=0,branchSavings=0,two=0,gates=0;
  std::size_t joinCandidates=0,joinTruncated=0;
};
auto cost(const State& s){return std::tuple{s.two,s.gates,s.swapCount,s.layout.v2p,s.circuit.initialLayout};}
void append(State& state,const Recipe& recipe){
  state.circuit.instructions.insert(state.circuit.instructions.end(),recipe.ops.begin(),recipe.ops.end());
  for(const auto& op:recipe.ops)if(!op.qubits.empty()&&op.kind!=OpKind::Measure&&op.kind!=OpKind::Barrier){++state.gates;state.two+=op.qubits.size()==2;}
  if(recipe.phase!=0){
    if(state.branches.empty())state.circuit.globalPhase+=recipe.phase;
    else state.circuit.instructions.push_back({OpKind::GlobalPhase,{}, {angleAttr(recipe.phase)}, {}});
  }
}
}

Module compileHeterogeneousCircuit(const Module& input,const registry::ChipInfo& chip,
    OptimizationLevel level,Diagnostics& diagnostics,CompilationReport* report){
  const auto original=flatten(input);
  const auto numeric=static_cast<int>(level);
  const auto limit=chip.placement.maxStates?chip.placement.maxStates:numeric<2?10000:numeric==2?50000:200000;
  const auto beam=chip.placement.beamWidth?chip.placement.beamWidth:numeric<2?16:numeric==2?32:64;
  const auto layoutLimit=chip.placement.maxLayouts?chip.placement.maxLayouts:numeric==3?8:1;
  Recipes recipes{chip,{},{}};
  if(chip.availableQubits)recipes.active.insert(chip.availableQubits->begin(),chip.availableQubits->end());
  else for(std::size_t q=0;q<chip.qubits;++q)recipes.active.insert(int(q));
  for(int q:chip.unavailableQubits)recipes.active.erase(q);
  auto physical=registry::computationalComponents(chip);
  std::erase_if(physical,[&](int q){return !recipes.active.contains(q);});
  if(physical.size()<original.numQubits)throw std::runtime_error("TARGET_INCOMPATIBLE: insufficient available computational qubits");
  for(const auto& op:original.instructions)if(op.kind==OpKind::Measure||op.kind==OpKind::Reset){
    if(std::none_of(physical.begin(),physical.end(),[&](int q){return recipes.singleton(gateName(op.kind),q);}))
      throw std::runtime_error("TARGET_INCOMPATIBLE: no usable physical subgraph supports required "+gateName(op.kind));
  }
  // A disconnected active graph cannot transmit an unknown quantum state
  // between its components. Count computational capacity, not resonator slots.
  if(!chip.allToAll){
    std::map<int,std::set<int>> adjacency;
    auto edge=[&](int a,int b){if(recipes.active.contains(a)&&recipes.active.contains(b)){adjacency[a].insert(b);adjacency[b].insert(a);}};
    for(auto [a,b]:chip.coupling)edge(a,b);
    for(const auto& [gate,loci]:chip.twoQubitGateLoci)for(auto [a,b]:loci)edge(a,b);
    for(auto [a,b]:chip.moveLoci)edge(a,b);for(auto [a,b]:chip.czLoci)edge(a,b);
    std::set<int> visited;std::size_t maximum=0;
    for(int start:physical)if(!visited.contains(start)){
      std::vector<int> todo{start};visited.insert(start);std::size_t capacity=0;
      while(!todo.empty()){int q=todo.back();todo.pop_back();capacity+=std::find(physical.begin(),physical.end(),q)!=physical.end();
        for(int n:adjacency[q])if(visited.insert(n).second)todo.push_back(n);}
      maximum=std::max(maximum,capacity);
    }
    std::vector<std::set<int>> logical(original.numQubits);
    for(const auto& op:original.instructions)if(op.qubits.size()==2&&op.kind!=OpKind::Barrier){logical[op.qubits[0]].insert(op.qubits[1]);logical[op.qubits[1]].insert(op.qubits[0]);}
    std::set<int> walked;
    for(std::size_t start=0;start<original.numQubits;++start)if(walked.insert(int(start)).second){
      std::vector<int> todo{int(start)};std::size_t size=0;
      while(!todo.empty()){int q=todo.back();todo.pop_back();++size;for(int n:logical[q])if(walked.insert(n).second)todo.push_back(n);}
      if(size>maximum)throw std::runtime_error("TARGET_INCOMPATIBLE: no physical connected component can hold an interacting logical component");
    }
  }
  std::mt19937_64 generator(chip.placement.seed);
  std::shuffle(physical.begin(),physical.end(),generator);
  auto degree=[&](int q){std::set<int> neighbors;
    for(auto [a,b]:chip.coupling){if(a==q)neighbors.insert(b);if(b==q)neighbors.insert(a);}
    for(const auto& [gate,loci]:chip.twoQubitGateLoci)for(auto [a,b]:loci){if(a==q)neighbors.insert(b);if(b==q)neighbors.insert(a);}
    return neighbors.size();};
  std::stable_sort(physical.begin(),physical.end(),[&](int a,int b){return degree(a)>degree(b);});
  // Prefer a constrained matching for each wire's first single-qubit operation.
  // A second unconstrained matching supports time-sharing a restricted gate slot.
  std::vector<std::vector<int>> domains(original.numQubits,physical);
  std::vector<bool> seen(original.numQubits);
  for(auto op:original.instructions)for(int q:op.qubits)if(!seen.at(q)){
    seen[q]=true;
    if(op.qubits.size()==1&&op.kind!=OpKind::Barrier){
      std::erase_if(domains[q],[&](int p){auto mapped=op;mapped.qubits={p};return !recipes.make(mapped);});
    }
  }
  std::vector<State> states;
  std::size_t expanded=0;bool exhausted=false,swapLimited=false;
  auto matching=[&](const auto& choices){
    std::vector<int> order(original.numQubits);std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](int a,int b){return choices[a].size()<choices[b].size();});
    std::vector<int> mapping(original.numQubits,-1);std::set<int> used;
    std::function<void(std::size_t)> visit=[&](std::size_t depth){
      if(states.size()>=layoutLimit||exhausted)return;
      if(expanded>=limit){exhausted=true;return;}++expanded;
      if(depth==order.size()){
        State s;s.circuit=original;s.circuit.numQubits=chip.qubits;s.circuit.target=chip.id;
        s.circuit.resonatorQubits=chip.resonatorQubits;s.circuit.instructions.clear();s.circuit.initialLayout=mapping;
        s.layout.v2p=mapping;s.layout.p2v.assign(chip.qubits,-1);
        for(std::size_t q=0;q<mapping.size();++q)s.layout.p2v[mapping[q]]=int(q);
        states.push_back(std::move(s));return;
      }
      const int q=order[depth];
      for(int p:choices[q])if(!used.contains(p)){
        used.insert(p);mapping[q]=p;visit(depth+1);used.erase(p);
        if(states.size()>=layoutLimit||exhausted)break;
      }
    };visit(0);
  };
  matching(domains);
  if(states.empty()&&!exhausted)matching(std::vector<std::vector<int>>(original.numQubits,physical));
  const auto initialLayouts=states.size();
  for(const auto& source:original.instructions){
    if(states.empty())break;
    if(source.kind==OpKind::If){for(auto& s:states){s.circuit.instructions.push_back(source);s.branches.push_back({s.layout});}continue;}
    if(source.kind==OpKind::Else||source.kind==OpKind::EndIf){
      for(auto& s:states){
        if(s.branches.empty())throw std::runtime_error("unbalanced branch during heterogeneous routing");
        auto& branch=s.branches.back();
        auto restore=[&](std::size_t prefix=0,bool record=false){
          for(std::size_t index=branch.path.size();index>prefix;--index){
            const auto edge=branch.path[index-1];const auto start=s.circuit.instructions.size();
            append(s,*recipes.swap(edge.first,edge.second));s.layout.apply_swap(edge.first,edge.second);++s.swapCount;
            if(record)branch.compensationGroups.emplace_back(start,s.circuit.instructions.size());
          }
          branch.path.resize(prefix);
        };
        if(source.kind==OpKind::Else){
          if(branch.hasElse)throw std::runtime_error("duplicate else during heterogeneous routing");
          branch.hasElse=true;branch.thenExit=s.layout;branch.thenPath=branch.path;
          restore(0,true);
        }else if(branch.hasElse){
          auto nativeCost=[&](RoutingEdge edge)->std::optional<JoinNativeCost>{
            const auto& recipe=recipes.swap(edge.first,edge.second);if(!recipe)return std::nullopt;
            JoinNativeCost count{};for(const auto& op:recipe->ops)if(!op.qubits.empty()){
              count.first+=op.qubits.size()==2;++count.second;}return count;
          };
          const auto join=chooseSharedBranchJoin(branch.entry,branch.thenPath,branch.path,nativeCost);
          const auto retainedThen=branch.thenPath.size()-join.thenPrefix;
          for(std::size_t group=retainedThen;group<branch.compensationGroups.size();++group){
            const auto [begin,end]=branch.compensationGroups[group];
            // The entire native recipe group is removed, including any scoped
            // global phase emitted by append(). Partial deletion is unsafe.
            for(std::size_t index=begin;index<end;++index){
              s.omitted.insert(index);const auto& op=s.circuit.instructions[index];
              if(!op.qubits.empty()&&op.kind!=OpKind::Measure&&op.kind!=OpKind::Barrier){--s.gates;s.two-=op.qubits.size()==2;}
            }
          }
          s.swapCount-=join.thenPrefix;restore(join.elsePrefix);
          s.branchSavings+=join.thenPrefix+join.elsePrefix;s.joinCandidates+=join.candidates;s.joinTruncated+=join.truncated;
          auto net=join.netPath;s.branches.pop_back();
          if(!s.branches.empty())s.branches.back().path.insert(s.branches.back().path.end(),net.begin(),net.end());
        }else{
          if(s.layout.v2p!=branch.entry.v2p)restore();else s.branchSavings+=branch.path.size();
          s.branches.pop_back();
        }
        s.circuit.instructions.push_back(source);
      }
      continue;
    }
    // Classical SSA, phases and barriers are order-preserving fences.
    if(source.qubits.empty()||source.kind==OpKind::Barrier){
      for(auto& s:states){auto mapped=source;for(auto& q:mapped.qubits)q=s.layout.v2p.at(q);s.circuit.instructions.push_back(std::move(mapped));}continue;
    }
    std::vector<State> ready,frontier=std::move(states);
    std::set<std::vector<int>> visited;
    for(const auto& s:frontier)visited.insert(s.layout.v2p);
    while(!frontier.empty()){
      std::stable_sort(frontier.begin(),frontier.end(),[](const auto& a,const auto& b){return cost(a)<cost(b);});
      if(frontier.size()>beam)frontier.resize(beam);
      std::vector<State> next;
      for(auto& state:frontier){
        auto mapped=source;for(auto& q:mapped.qubits)q=state.layout.v2p.at(q);
        if(auto implementation=recipes.make(mapped)){
          append(state,*implementation);ready.push_back(std::move(state));
          if(numeric<2)break;
          continue;
        }
        if(expanded>=limit){exhausted=true;continue;}++expanded;
        for(std::size_t i=0;i<physical.size()&&!exhausted;++i)for(std::size_t j=i+1;j<physical.size();++j){
          const int a=physical[i],b=physical[j];
          if(state.layout.p2v[a]<0&&state.layout.p2v[b]<0)continue;
          if(chip.placement.maxSwaps&&state.forwardSwaps>=*chip.placement.maxSwaps){swapLimited=true;continue;}
          auto moved=state;moved.layout.apply_swap(a,b);
          if(visited.contains(moved.layout.v2p))continue;
          if(expanded>=limit){exhausted=true;break;}++expanded;
          const auto& recipe=recipes.swap(a,b);if(!recipe)continue;
          visited.insert(moved.layout.v2p);append(moved,*recipe);++moved.swapCount;++moved.forwardSwaps;
          if(!moved.branches.empty())moved.branches.back().path.emplace_back(a,b);
          next.push_back(std::move(moved));
        }
      }
      if(!ready.empty()&&(numeric<2||ready.size()>=beam||exhausted))break;
      frontier=std::move(next);
    }
    std::stable_sort(ready.begin(),ready.end(),[](const auto& a,const auto& b){return cost(a)<cost(b);});
    if(ready.size()>beam)ready.resize(beam);states=std::move(ready);
  }
  std::erase_if(states,[&](const auto& s){const bool rejected=chip.placement.maxSwaps&&s.swapCount>*chip.placement.maxSwaps;swapLimited|=rejected;return rejected;});
  if(report){
    report->notes["placement_strategy"]="heterogeneous";
    report->notes["placement_termination"]=exhausted?"state_limit":"bounded_search_completed";
    report->counters["placement_expanded_states"]+=expanded;
    report->counters["placement_state_limit"]=limit;report->counters["placement_beam_width"]=beam;
    report->counters["placement_seed"]=chip.placement.seed;report->counters["placement_initial_layouts"]=initialLayouts;
    report->counters["placement_budget_exhausted"]=exhausted;
    report->addGap("Heterogeneous placement is a deterministic bounded search; failure is not a proof of layout infeasibility.");
    report->addGap("Heterogeneous native recipe reconstruction residuals are not independently measured in this report.");
  }
  if(states.empty())throw std::runtime_error(exhausted?"PLACEMENT_SEARCH_LIMIT: no legal incumbent within the configured state budget":swapLimited?"PLACEMENT_SWAP_LIMIT: no candidate within the requested routing SWAP limit":"PLACEMENT_SEARCH_INCOMPLETE: no legal candidate in the bounded placement/routing search");
  std::stable_sort(states.begin(),states.end(),[](const auto& a,const auto& b){return cost(a)<cost(b);});
  auto& best=states.front();best.circuit.finalLayout=best.layout.v2p;
  if(report){report->counters["routing_swaps"]=best.swapCount;report->counters["branch_join_swaps_removed"]+=best.branchSavings;
    report->counters["branch_join_candidates"]+=best.joinCandidates;report->counters["branch_join_prefix_limit"]=4096;
    report->counters["branch_join_budget_truncated"]+=best.joinTruncated;}
  std::vector<WireOp> selected;
  for(std::size_t index=0;index<best.circuit.instructions.size();++index)if(!best.omitted.contains(index))selected.push_back(std::move(best.circuit.instructions[index]));
  best.circuit.instructions=std::move(selected);
  auto result=canonicalizeNative(rebuild(best.circuit),chip);
  validateCompiled(result,chip,diagnostics);
  return result;
}
}

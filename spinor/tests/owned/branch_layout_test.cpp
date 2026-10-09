#include "spinor/passes/Routing.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/passes/BranchJoin.h"
#include <stdexcept>
using namespace spinor;
using namespace spinor::dialect;
void require(bool value){if(!value)throw std::runtime_error("branch layout regression");}
int main(){
  registry::ChipInfo chip;chip.qubits=3;chip.id="line";chip.coupling={{0,1},{1,2}};
  chip.nativeGates={"cx","swap"};chip.decompose.twoQubitEntangler="cx";
  passes::CouplingGraph graph(3,chip.coupling,false);
  passes::Layout identity{{0,1,2},{0,1,2}};
  WireCircuit circuit;circuit.numQubits=3;circuit.numClbits=3;
  circuit.instructions={{OpKind::If,{}, {namedDouble("condition_clbit",0),namedDouble("condition_value",1)}, {},0},
    {OpKind::Cx,{0,2},{},{}},{OpKind::Else,{},{},{}},{OpKind::Cx,{0,2},{},{}},{OpKind::EndIf,{},{},{}},
    {OpKind::Measure,{0},{},{},2},{OpKind::Measure,{1},{},{},0},{OpKind::Measure,{2},{},{},1}};
  chip.placement.maxSwaps=2;
  auto common=passes::Routing{}.run(rebuild(circuit),chip,graph,identity);
  require(common.swapCount==2);require(common.finalLayout.v2p==std::vector<int>({1,0,2}));
  const auto compiled=flatten(common.module);
  require(compiled.instructions[compiled.instructions.size()-3].qubits==std::vector<int>({1}));
  require(compiled.instructions[compiled.instructions.size()-3].clbit==2);
  circuit.instructions[3].qubits={0,1};
  auto different=passes::Routing{}.run(rebuild(circuit),chip,graph,identity);
  require(different.swapCount==2);require(different.finalLayout.v2p==identity.v2p);
  chip.placement.maxSwaps=1;bool rejected=false;
  try{passes::Routing{}.run(rebuild(circuit),chip,graph,identity);}catch(const std::exception& error){rejected=std::string(error.what()).find("PLACEMENT_SWAP_LIMIT")!=std::string::npos;}
  require(rejected);
  // Different exits still share a cheaper intermediate interface. Compensation
  // only reverses the suffix after that shared prefix.
  passes::Layout four{{0,1,2,3},{0,1,2,3}};
  std::vector<passes::RoutingEdge> yes{{0,1},{1,2}},no{{0,1},{2,3}};
  auto join=passes::chooseSharedBranchJoin(four,yes,no,[](auto)->std::optional<passes::JoinNativeCost>{return passes::JoinNativeCost{3,7};});
  require(join.thenPrefix==1&&join.elsePrefix==1&&join.netPath==std::vector<passes::RoutingEdge>{{0,1}});
  require(join.candidates==2&&!join.truncated);
  // Revisited entry permutations may omit compensation entirely while using an
  // empty representation for the outer branch's net routing history.
  yes={{0,1},{0,1}};no={{2,3},{2,3}};
  join=passes::chooseSharedBranchJoin(four,yes,no,[](auto)->std::optional<passes::JoinNativeCost>{return passes::JoinNativeCost{3,7};});
  require(join.thenPrefix==2&&join.elsePrefix==2&&join.netPath.empty());
  // Exercise actual routing rather than just the join helper. Both arms start
  // with SWAP(0,1), but their second routing swaps and exit permutations differ.
  // Keeping that shared prefix saves two swaps over returning to branch entry.
  chip.qubits=4;chip.coupling={{0,1},{1,2},{2,3}};chip.placement.maxSwaps=6;
  passes::CouplingGraph fourGraph(4,chip.coupling,false);
  WireCircuit unequal;unequal.numQubits=4;unequal.numClbits=4;
  unequal.instructions={{OpKind::If,{}, {namedDouble("condition_clbit",0),namedDouble("condition_value",1)}, {},0},
    {OpKind::Cx,{0,2},{},{}},{OpKind::Cx,{0,3},{},{}},
    {OpKind::Else,{},{},{}},{OpKind::Cx,{0,2},{},{}},{OpKind::Cx,{3,0},{},{}},
    {OpKind::EndIf,{},{},{}},{OpKind::Measure,{0},{},{},3},{OpKind::Measure,{3},{},{},1}};
  auto shared=passes::Routing{}.run(rebuild(unequal),chip,fourGraph,four);
  require(shared.swapCount==6);require(shared.finalLayout.v2p==std::vector<int>({1,0,2,3}));
  const auto sharedCircuit=flatten(shared.module);
  require(sharedCircuit.instructions[sharedCircuit.instructions.size()-2].qubits==std::vector<int>{1});
  require(sharedCircuit.instructions[sharedCircuit.instructions.size()-2].clbit==3);
  chip.placement.maxSwaps=5;rejected=false;
  try{passes::Routing{}.run(rebuild(unequal),chip,fourGraph,four);}catch(const std::exception& error){rejected=std::string(error.what()).find("PLACEMENT_SWAP_LIMIT")!=std::string::npos;}
  require(rejected);
}

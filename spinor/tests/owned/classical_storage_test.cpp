#include "spinor/passes/ClassicalStorageReuse.h"
#include "spinor/dialect/Classical.h"
#include <iostream>
#include <stdexcept>
using namespace spinor::dialect;
using spinor::passes::ClassicalStorageReuse;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
WireOp classical(OpKind kind,std::string result,std::vector<std::string> inputs={},std::string literal=""){
  WireOp op{kind,{},{{"result",std::move(result)}},{}};
  for(auto& input:inputs)op.attributes.push_back({"input",std::move(input)});
  if(!literal.empty())op.attributes.push_back({"value",std::move(literal)});
  return op;
}
int main()try{
  WireCircuit c;c.numQubits=1;c.numClbits=5;c.exportedClbits={0,4};
  for(int bit=0;bit<5;++bit)c.classicalStorage.push_back({"s"+std::to_string(bit),1,{bit},bit==0||bit==4?"exported":"private",true,"0"});
  c.classicalValues={{"a","bool",1,{1},"private",false,""},{"b","bool",1,{2},"private",false,""},
                     {"d","bool",1,{3},"private",false,""},{"out","bool",1,{4},"exported",false,""}};
  c.instructions={classical(OpKind::CConst,"a",{},"1"),classical(OpKind::CCopy,"b",{"a"}),
                  classical(OpKind::CNot,"d",{"b"}),classical(OpKind::CCopy,"out",{"d"}),
                  {OpKind::Measure,{0},{},{},0}};
  c.classicalOutputs={{"result","out","bool",1,"value"}};
  auto reused=flatten(ClassicalStorageReuse{}.run(rebuild(c)));
  require(reused.instructions.size()==c.instructions.size(),"storage optimization deleted an operation");
  require(reused.instructions.back().kind==OpKind::Measure,"unused measurement must be retained");
  require(reused.numClbits==5,"pinned sparse readout width changed");
  require(reused.classicalValues[3].storage==std::vector<int>{4},"exported slot moved");
  require(reused.classicalValues[0].storage==reused.classicalValues[1].storage&&
          reused.classicalValues[1].storage==reused.classicalValues[2].storage,"non-overlapping private lifetimes were not reused");
  // Both operands remain live together through a branch join. They cannot alias
  // even though declarations and definitions occur in different arms.
  c.instructions={classical(OpKind::CConst,"a",{},"1"),classical(OpKind::CConst,"b",{},"0"),
    {OpKind::If,{},{{"condition_clbit",0.0},{"condition_value",1.0}},{},0},
    {OpKind::X,{0},{},{}},{OpKind::Else,{},{},{}},{OpKind::Z,{0},{},{}},{OpKind::EndIf,{},{},{}},
    classical(OpKind::CXor,"d",{"a","b"}),classical(OpKind::CCopy,"out",{"d"})};
  reused=flatten(ClassicalStorageReuse{}.run(rebuild(c)));
  require(reused.classicalValues[0].storage!=reused.classicalValues[1].storage,"interfering branch-live values aliased");
  std::cout<<"private storage CFG liveness, pinned exports and measurement preservation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

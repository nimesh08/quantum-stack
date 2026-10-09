#include "spinor/emit/Emitters.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/dialect/Classical.h"
#include <iomanip>
#include <sstream>

namespace spinor::emit {
namespace {
std::string escaped(const std::string& value){
  std::ostringstream out;out<<'"';
  for(unsigned char c:value){
    if(c=='"'||c=='\\')out<<'\\'<<c;
    else if(c<32)out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<unsigned(c)<<std::dec;
    else out<<c;
  }
  out<<'"';return out.str();
}
}
std::string emitPhysicalJson(const dialect::Module& m){
  using namespace dialect;auto c=flatten(m);std::ostringstream out;out<<std::setprecision(17);
  out<<"{\"schema_version\":2,\"target\":"<<escaped(c.target)<<",\"name\":"<<escaped(c.name)
     <<",\"num_qubits\":"<<c.numQubits<<",\"num_clbits\":"<<c.numClbits<<",\"global_phase\":"<<c.globalPhase<<",\"instructions\":[";
  bool first=true;
  for(const auto& op:c.instructions){
    if(!first)out<<',';first=false;std::string name(opMnemonic(op.kind));name=name.substr(7);
    out<<"{\"op\":"<<escaped(name)<<",\"qubits\":[";
    for(std::size_t i=0;i<op.qubits.size();++i){if(i)out<<',';out<<op.qubits[i];}
    out<<"],\"params\":[";
    if(op.kind==OpKind::PhasedXZ)out<<parameter(op,"x")<<','<<parameter(op,"z")<<','<<parameter(op,"axis_phase");
    else if(op.kind==OpKind::U1q)out<<parameter(op,"theta")<<','<<parameter(op,"phi");
    else if(op.kind==OpKind::GlobalPhase||op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rz||op.kind==OpKind::Rxx||op.kind==OpKind::Rzz||op.kind==OpKind::Gpi||op.kind==OpKind::Gpi2)out<<parameter(op);
    out<<"],\"clbits\":[";if(op.kind==OpKind::Measure||op.kind==OpKind::If)out<<op.clbit;out<<"]";
    if(op.kind==OpKind::If)out<<",\"condition_value\":"<<parameter(op,"condition_value");
    const auto result=stringAttribute(op,"result");
    if(!result.empty())out<<",\"result\":"<<escaped(result);
    const auto condition=stringAttribute(op,"condition");
    if(!condition.empty())out<<",\"condition\":"<<escaped(condition);
    if(isClassical(op.kind)){
      out<<",\"inputs\":[";bool inputFirst=true;
      for(const auto& value:classicalInputs(op)){if(!inputFirst)out<<',';inputFirst=false;out<<escaped(value);}
      out<<']';
      if(op.kind==OpKind::CConst)out<<",\"value\":"<<escaped(stringAttribute(op,"value"));
    }
    out<<"}";
  }
  out<<"],\"logical_to_physical\":[";
  for(std::size_t i=0;i<c.finalLayout.size();++i){if(i)out<<',';out<<c.finalLayout[i];}
  out<<"],\"initial_logical_to_physical\":[";
  for(std::size_t i=0;i<c.initialLayout.size();++i){if(i)out<<',';out<<c.initialLayout[i];}
  out<<"],\"measurement_mapping\":[";first=true;
  for(const auto& op:c.instructions)if(op.kind==OpKind::Measure){if(!first)out<<',';first=false;out<<"{\"qubit\":"<<op.qubits.at(0)<<",\"clbit\":"<<op.clbit<<'}';}
  out<<"],\"resonator_qubits\":[";
  for(std::size_t i=0;i<c.resonatorQubits.size();++i){if(i)out<<',';out<<c.resonatorQubits[i];}
  out<<"],\"computational_qubits\":[";first=true;
  for(std::size_t q=0;q<c.numQubits;++q)
    if(std::find(c.resonatorQubits.begin(),c.resonatorQubits.end(),int(q))==c.resonatorQubits.end()){
      if(!first)out<<',';first=false;out<<q;
    }
  out<<"],\"quantum_inputs\":[";
  auto inputs=c.quantumInputs;
  if(inputs.empty()&&c.reservedPool.empty()){
    const auto logical=c.initialLayout.empty()?c.numQubits:c.initialLayout.size();
    for(std::size_t q=0;q<logical;++q)inputs.push_back(static_cast<int>(q));
  }
  for(std::size_t i=0;i<inputs.size();++i){if(i)out<<',';out<<inputs[i];}
  out<<"],\"reserved_pool\":[";for(std::size_t i=0;i<c.reservedPool.size();++i){if(i)out<<',';out<<c.reservedPool[i];}
  out<<"],\"classical_storage\":[";first=true;
  const auto indices=[&](const auto& values){out<<'[';bool f=true;for(int value:values){if(!f)out<<',';f=false;out<<value;}out<<']';};
  auto storage=[&](const ClassicalStorage& value){if(!first)out<<',';first=false;
    out<<"{\"id\":"<<escaped(value.id)<<",\"width\":"<<value.width<<",\"bits\":";indices(value.bits);
    out<<",\"visibility\":"<<escaped(value.visibility)<<",\"initialized\":"<<(value.initialized?"true":"false");
    if(value.initialized)out<<",\"initial_value\":"<<escaped(value.initialValue);
    out<<'}';};
  if(c.classicalStorage.empty())for(std::size_t i=0;i<c.numClbits;++i)
    storage({"s"+std::to_string(i),1,{static_cast<int>(i)},"exported",true,"0"});
  else for(const auto& value:c.classicalStorage)storage(value);
  out<<"],\"classical_values\":[";first=true;
  for(const auto& value:c.classicalValues){if(!first)out<<',';first=false;
    out<<"{\"id\":"<<escaped(value.id)<<",\"type\":"<<escaped(value.type)<<",\"width\":"<<value.width<<",\"storage\":";indices(value.storage);
    out<<",\"visibility\":"<<escaped(value.visibility)<<",\"initialized\":"<<(value.initialized?"true":"false");
    if(value.initialized)out<<",\"initial_value\":"<<escaped(value.initialValue);
    out<<'}';
  }
  out<<"],\"classical_outputs\":[";first=true;
  for(const auto& value:c.classicalOutputs){if(!first)out<<',';first=false;
    out<<"{\"name\":"<<escaped(value.name)<<",\"value\":"<<escaped(value.value)<<",\"type\":"<<escaped(value.type)<<",\"width\":"<<value.width<<",\"role\":"<<escaped(value.role)<<'}';
  }
  out<<"],\"exported_clbits\":";
  if(c.exportedClbits.empty()&&c.classicalStorage.empty()){
    std::vector<int> legacy;for(std::size_t i=0;i<c.numClbits;++i)legacy.push_back(static_cast<int>(i));indices(legacy);
  }else indices(c.exportedClbits);
  out<<"}\n";return out.str();
}
} // namespace spinor::emit

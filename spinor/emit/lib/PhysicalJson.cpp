#include "spinor/emit/Emitters.h"
#include "spinor/dialect/Circuit.h"
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
  out<<"{\"schema_version\":1,\"target\":"<<escaped(c.target)<<",\"name\":"<<escaped(c.name)
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
  out<<"]}\n";return out.str();
}
} // namespace spinor::emit

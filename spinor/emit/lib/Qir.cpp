#include "spinor/emit/Emitters.h"
#include "spinor/dialect/Circuit.h"
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace spinor::emit {
using namespace dialect;
std::string emitQir(const Module& m,const registry::ChipInfo* chip) {
  auto c=flatten(m);std::ostringstream body,os;
  for(const auto& op:c.instructions)if(op.kind==OpKind::Move)
    throw std::runtime_error("MOVE requires IQM native JSON; this QIR profile has no MOVE intrinsic");
  body<<std::scientific<<std::setprecision(17);
  const std::string platform=chip?chip->qirPlatform:"standard";
  const bool quantinuum=platform=="quantinuum-h2"||platform=="quantinuum-helios";
  const bool initialize=platform!="quantinuum-h2";
  const bool dynamic=hasControlFlow(m);
  const std::string readResult=quantinuum?"__quantum__rt__read_result":"__quantum__qis__read_result__body";
  auto ptr=[](std::size_t n,const std::string& qualifier=std::string{}){
    return "ptr "+qualifier+"inttoptr (i64 "+std::to_string(n)+" to ptr)";
  };
  std::map<std::string,std::string> declarations;
  std::map<int,std::size_t> finalResult;
  for(const auto& op:c.instructions)if(op.kind==OpKind::Measure)finalResult[op.clbit]=0;
  const bool sparse=finalResult.size()<c.numClbits;
  const bool adaptive=quantinuum||dynamic||(!chip&&sparse)||
    (chip&&chip->supports.feedforward!=registry::CapabilityFlags::Feedforward::None);
  if(!adaptive&&sparse)throw std::runtime_error("Base-profile QIR cannot represent unmeasured classical output slots; use a supported adaptive target or measure every output bit");
  if(dynamic&&chip&&chip->supports.feedforward==registry::CapabilityFlags::Feedforward::None)
    throw std::runtime_error("QIR target does not support classical feedforward");
  finalResult.clear();
  std::size_t results=0,branchId=0,phiId=0;
  bool measurementBlock=false;
  std::string currentBlock="body";
  std::vector<std::string> values(c.numClbits,"false");
  struct Branch {std::size_t id;bool hasElse=false;std::vector<std::string> before,thenValues;std::string thenBlock;};
  std::vector<Branch> branches;
  for(const auto& op:c.instructions){
    if(op.kind==OpKind::GlobalPhase){body<<"  ; branch global phase "<<parameter(op)<<" radians (unobservable after classical measurement)\n";continue;}
    if(op.kind==OpKind::If){
      auto id=branchId++;branches.push_back({id,false,values,{},""});
      // The condition is evaluated once on branch entry; later writes to the
      // same classical bit only affect subsequent branches/output.
      const auto condition=values.at(static_cast<std::size_t>(op.clbit));
      const bool one=parameter(op,"condition_value")==1;
      body<<"  br i1 "<<condition<<", label %"<<(one?"then":"else")<<id
          <<", label %"<<(one?"else":"then")<<id<<"\nthen"<<id<<":\n";
      currentBlock="then"+std::to_string(id);continue;
    }
    if(op.kind==OpKind::Else){
      if(branches.empty()||branches.back().hasElse)throw std::runtime_error("Unbalanced QIR else marker");
      auto& branch=branches.back();branch.hasElse=true;branch.thenValues=values;branch.thenBlock=currentBlock;
      body<<"  br label %end"<<branch.id<<"\nelse"<<branch.id<<":\n";
      values=branch.before;currentBlock="else"+std::to_string(branch.id);continue;
    }
    if(op.kind==OpKind::EndIf){
      if(branches.empty())throw std::runtime_error("Unbalanced QIR endif marker");
      auto branch=branches.back();branches.pop_back();
      auto elseValues=values;auto elseBlock=currentBlock;
      if(!branch.hasElse){
        branch.thenValues=values;branch.thenBlock=currentBlock;
        elseValues=branch.before;elseBlock="else"+std::to_string(branch.id);
        body<<"  br label %end"<<branch.id<<"\n"<<elseBlock<<":\n";
      }
      body<<"  br label %end"<<branch.id<<"\nend"<<branch.id<<":\n";
      // phi predecessors are the actual final blocks of nested branches.
      for(std::size_t bit=0;bit<values.size();++bit){
        if(branch.thenValues[bit]==elseValues[bit])values[bit]=elseValues[bit];
        else{
          values[bit]="%merged"+std::to_string(phiId++);
          body<<"  "<<values[bit]<<" = phi i1 [ "<<branch.thenValues[bit]<<", %"<<branch.thenBlock
              <<" ], [ "<<elseValues[bit]<<", %"<<elseBlock<<" ]\n";
        }
      }
      currentBlock="end"+std::to_string(branch.id);continue;
    }
    if(op.kind==OpKind::Barrier)continue;
    const bool irreversible=op.kind==OpKind::Measure||op.kind==OpKind::Reset;
    if(!adaptive){
      if(irreversible&&!measurementBlock){body<<"  br label %measurements\nmeasurements:\n";measurementBlock=true;}
      else if(!irreversible&&measurementBlock)throw std::runtime_error("Base-profile QIR requires irreversible operations after quantum transformations; select a supported adaptive target");
    }
    std::string name,signature;
    switch(op.kind){
      case OpKind::H:name="h__body";break;case OpKind::X:name="x__body";break;
      case OpKind::Y:name="y__body";break;case OpKind::Z:name="z__body";break;
      case OpKind::S:name="s__body";break;case OpKind::Sdg:name="s__adj";break;
      case OpKind::T:name="t__body";break;case OpKind::Tdg:name="t__adj";break;
      case OpKind::Rx:name="rx__body";break;case OpKind::Ry:name="ry__body";break;case OpKind::Rz:name="rz__body";break;
      case OpKind::U1q:
        if(!quantinuum)throw std::runtime_error("Native U1q QIR requires the Quantinuum rxy instruction contract");
        name="rxy__body";break;
      case OpKind::Cx:name="cnot__body";break;case OpKind::Cz:name="cz__body";break;
      case OpKind::Swap:name="swap__body";break;case OpKind::Rzz:name="rzz__body";break;
      case OpKind::Reset:name="reset__body";break;case OpKind::Measure:name="mz__body";break;
      default:throw std::runtime_error("QIR gate set does not support "+std::string(opMnemonic(op.kind)));
    }
    std::ostringstream args;args<<std::scientific<<std::setprecision(17);
    if(op.kind==OpKind::U1q){args<<"double "<<parameter(op,"theta")<<", double "<<parameter(op,"phi")<<", ";signature="double, double, ";}
    else if(op.kind==OpKind::Rx||op.kind==OpKind::Ry||op.kind==OpKind::Rz||op.kind==OpKind::Rzz){args<<"double "<<parameter(op)<<", ";signature="double, ";}
    for(std::size_t i=0;i<op.qubits.size();++i){if(i){args<<", ";signature+=", ";}args<<ptr(op.qubits[i]);signature+="ptr";}
    if(op.kind==OpKind::Measure){args<<", "<<ptr(results,"writeonly ");signature+=", ptr writeonly";finalResult[op.clbit]=results++;}
    declarations[name]=signature;
    body<<"  call void @__quantum__qis__"<<name<<'('<<args.str()<<")\n";
    if(dynamic&&op.kind==OpKind::Measure){
      auto id=results-1;values.at(static_cast<std::size_t>(op.clbit))="%measurement"+std::to_string(id);
      body<<"  "<<values[op.clbit]<<" = call i1 @"<<readResult<<'('<<ptr(id,"readonly ")<<")\n";
    }
  }
  if(!branches.empty())throw std::runtime_error("Unclosed QIR branch");
  if(!adaptive&&!measurementBlock)body<<"  br label %measurements\nmeasurements:\n";
  body<<"  br label %output\noutput:\n";
  std::vector<std::string> labels;
  auto label=[&](const std::string& value){
    const auto id=labels.size();labels.push_back(value);
    return "ptr getelementptr inbounds (["+std::to_string(value.size()+1)+" x i8], ptr @output_label"+std::to_string(id)+", i64 0, i64 0)";
  };
  body<<"  call void @__quantum__rt__array_record_output(i64 "<<c.numClbits<<", "<<label("0_0a")<<")\n";
  bool boolOutput=false;
  for(std::size_t bit=0;bit<c.numClbits;++bit){
    auto it=finalResult.find(static_cast<int>(bit));
    const bool boolean=dynamic||it==finalResult.end();
    const auto outputLabel=label(std::to_string(bit+1)+"_0a"+std::to_string(bit)+(boolean?"b":"r"));
    if(boolean){boolOutput=true;body<<"  call void @__quantum__rt__bool_record_output(i1 "<<(dynamic?values[bit]:"false")<<", "<<outputLabel<<")\n";}
    else body<<"  call void @__quantum__rt__result_record_output("<<ptr(it->second)<<", "<<outputLabel<<")\n";
  }
  os<<"; Spinor owned native compiler; global_phase="<<std::setprecision(17)<<c.globalPhase<<" radians; qir_platform="<<platform<<"\n";
  for(std::size_t i=0;i<labels.size();++i)os<<"@output_label"<<i<<" = private constant ["<<labels[i].size()+1<<" x i8] c\""<<labels[i]<<"\\00\"\n";
  for(const auto& [name,signature]:declarations)
    os<<"declare void @__quantum__qis__"<<name<<'('<<signature<<')'<<((name=="mz__body"||name=="reset__body")?" #1":"")<<"\n";
  os<<"declare void @__quantum__rt__array_record_output(i64, ptr)\n"
      "declare void @__quantum__rt__result_record_output(ptr, ptr)\n";
  if(boolOutput)os<<"declare void @__quantum__rt__bool_record_output(i1, ptr)\n";
  if(dynamic)os<<"declare i1 @"<<readResult<<"(ptr readonly)\n";
  if(initialize)os<<"declare void @__quantum__rt__initialize(ptr)\n";
  os<<"define i64 @main() #0 {\nentry:\n";
  if(initialize)os<<"  call void @__quantum__rt__initialize(ptr null)\n";
  os<<"  br label %body\nbody:\n"<<body.str()<<"  ret i64 0\n}\n";
  os<<"attributes #0 = { \"entry_point\" \"qir_profiles\"=\""<<(adaptive?"adaptive_profile":"base_profile")
    <<"\" \"output_labeling_schema\"=\"labeled\" \"required_num_qubits\"=\""<<c.numQubits<<"\" \"required_num_results\"=\""<<results<<"\" }\n"
      "attributes #1 = { \"irreversible\" }\n"
      "!llvm.module.flags = !{!0, !1, !2, !3}\n"
      "!0 = !{i32 1, !\"qir_major_version\", i32 1}\n"
      "!1 = !{i32 7, !\"qir_minor_version\", i32 0}\n"
      "!2 = !{i32 1, !\"dynamic_qubit_management\", i1 false}\n"
      "!3 = !{i32 1, !\"dynamic_result_management\", i1 false}\n";
  return os.str();
}
}

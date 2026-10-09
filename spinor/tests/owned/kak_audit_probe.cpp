#include "spinor/passes/TwoQubitDecomposer.h"
#include "spinor/emit/Emitters.h"
#include <iostream>
#include <iomanip>
#include <string>
int main(int argc,char** argv) {
  using namespace spinor;
  registry::ChipInfo chip;
  chip.id="independent_audit";chip.qubits=2;chip.allToAll=true;
  const std::string basis=argc>1?argv[1]:"cx";
  chip.decompose.twoQubitEntangler=basis;
  chip.decompose.twoQubitEntanglerCountMax=basis=="syc"?18:(basis=="iswap"||basis=="sqrt_iswap"||basis=="sqrt_iswap_inv"?6:3);
  if(basis=="sqrt_iswap"||basis=="sqrt_iswap_inv"||basis=="syc")chip.nativeGates={"phased_xz",basis};
  else if(basis=="iswap"||basis=="rxx")chip.nativeGates={"rx","rz",basis};
  else if(basis=="ms")chip.nativeGates={"gpi","gpi2","ms"};
  else if(basis=="rzz")chip.nativeGates={"u1q","rz","rzz"};
  else if(basis=="cz"||basis=="ecr")chip.nativeGates={"rz","sx",basis};
  else chip.nativeGates={"rz","ry",basis};
  chip.decompose.oneQubitRotationGate=basis=="ms"?"gpi":"rz";
  chip.decompose.oneQubitPi2Gate=basis=="ms"?"gpi2":"sx";
  passes::U4 input;
  while(std::cin>>input[0][0].first) {
    std::cin>>input[0][0].second;
    for(int i=1;i<16;++i)std::cin>>input[i/4][i%4].first>>input[i/4][i%4].second;
    try {
      auto result=passes::TwoQubitDecomposer{}.decompose(input,passes::computeTraits(chip),&chip);
      dialect::WireCircuit output;
      output.name="kak_audit";output.target=chip.id;output.numQubits=2;
      output.instructions=result.operations;output.globalPhase=result.globalPhase;
      std::cout<<emit::emitPhysicalJson(dialect::rebuild(output))<<std::endl;
      std::cerr<<"{\"construction\":\""<<result.construction
               <<"\",\"analytical_attempted\":"<<(result.analyticalAttempted?"true":"false")
               <<",\"analytical_rejection\":\""<<result.analyticalRejection<<"\"}\n";
    } catch(const std::exception& e) {std::cout<<"ERROR "<<e.what()<<std::endl;std::cerr<<"{\"construction\":\"error\"}\n";}
  }
}

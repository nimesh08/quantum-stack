#include "spinor/passes/PassManager.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/dialect/Circuit.h"
#include "spinor/parser/Parser.h"
#include "spinor/sim/Simulator.h"
#include "spinor/emit/Emitters.h"
#include "../../passes/lib/NativeSynthesis.h"
#include <iostream>
#include <random>
#include <stdexcept>
using namespace spinor;using namespace dialect;using namespace passes;using namespace passes::la;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
Mat4 matrix(const Module& m){
  auto c=flatten(m);auto u=identity4();
  for(const auto& op:c.instructions){
    if(op.kind==OpKind::Barrier||op.kind==OpKind::Measure)continue;
    Mat4 g;
    if(op.qubits.size()==1)g=op.qubits[0]==0?kron(matrix1(op),identity2()):kron(identity2(),matrix1(op));
    else {g=matrix2(op);if(op.qubits[0]==1)g=mul4(SWAP(),mul4(g,SWAP()));}
    u=mul4(g,u);
  }
  for(auto& z:u.e)z*=std::polar(1.0,c.globalPhase);return u;
}
void exact(const Mat4& a,const Mat4& b,const std::string& context){for(int i=0;i<16;++i)require(std::abs(a.e[i]-b.e[i])<1e-8,"matrix mismatch: "+context);}
int main()try{
  std::mt19937_64 rng(17);std::uniform_real_distribution<double> angle(-20,20);
  std::vector<registry::ChipInfo> chips;
  for(auto entangler:{"cz","cx","ecr","ms","rzz","rxx"}){
    registry::ChipInfo chip;chip.id=entangler;chip.qubits=2;chip.allToAll=true;chip.decompose.twoQubitEntangler=entangler;
    chip.nativeGates={"rz","sx",entangler};
    if(chip.id=="ms"||chip.id=="rzz")chip.nativeGates={"gpi","gpi2",entangler};
    if(chip.id=="rxx")chip.nativeGates={"u1q","rz",entangler};
    chips.push_back(chip);
  }
  auto qtu=chips.back();qtu.id="quantinuum";qtu.nativeGates={"u1q","rz","rzz"};qtu.decompose.twoQubitEntangler="rzz";chips.push_back(qtu);
  for(auto entangler:{"cz","sqrt_iswap","sqrt_iswap_inv","syc"}){
    auto chip=chips.front();chip.id=std::string("google_")+entangler;
    chip.nativeGates={"phased_xz",entangler};chip.decompose.twoQubitEntangler=entangler;chips.push_back(chip);
  }
  auto rigetti=chips.front();rigetti.id="rigetti_iswap";rigetti.nativeGates={"rx","rz","iswap"};
  rigetti.decompose.twoQubitEntangler="iswap";rigetti.decompose.oneQubitRotationGate="rz";rigetti.decompose.oneQubitPi2Gate="rx";chips.push_back(rigetti);
  for(const auto& chip:chips){
    for(int trial=0;trial<30;++trial){
      Module m;m.targetAttr="generic";Builder b(m);auto q0=b.allocQubit(),q1=b.allocQubit();
      q0=b.rx(angle(rng),q0);q0=b.ry(angle(rng),q0);q1=b.rz(angle(rng),q1);q1=b.h(q1);
      auto pair=b.cx(q0,q1);q0=pair.first;q1=pair.second;
      pair=b.rzz(angle(rng),q1,q0);q1=pair.first;q0=pair.second;
      pair=b.rxx(angle(rng),q0,q1);q0=pair.first;q1=pair.second;
      pair=b.ecr(q0,q1);q0=pair.first;q1=pair.second;
      auto desired=matrix(m);
      for(auto level:{OptimizationLevel::O0,OptimizationLevel::O1,OptimizationLevel::O2,OptimizationLevel::O3}){
        Diagnostics d;auto result=PassManager{}.compile(m,chip,level,d);
        if(d.hasErrors())for(const auto& e:d.items())std::cerr<<e.message<<'\n';
        require(!d.hasErrors(),"compile "+chip.id);exact(desired,matrix(result),chip.id);
        if(chip.id=="rigetti_iswap")for(const auto& op:flatten(result).instructions)if(op.kind==OpKind::Rx){
          double steps=parameter(op)/(M_PI/2);require(std::abs(steps-std::round(steps))<1e-10&&std::abs(steps)<=2,"calibrated RX angle");
        }
        require(result.finalLayout==std::vector<int>({0,1}),"native final layout");
        auto round=parse(print(result),d);require(bool(round)&&!d.hasErrors(),"native IR round trip");exact(matrix(result),matrix(*round),"roundtrip");
        require(round->finalLayout==result.finalLayout,"final layout IR round trip");
      }
    }
  }
  for(auto chip:chips)if(chip.id=="cx"||chip.id=="ecr"){
    chip.directedConnectivity=true;chip.allToAll=false;chip.coupling={{1,0}};
    Module m;m.targetAttr="generic";Builder b(m);auto a=b.allocQubit(),z=b.allocQubit();
    auto p=b.cx(a,z);b.ecr(p.first,p.second);
    Diagnostics d;auto result=PassManager{}.compile(m,chip,OptimizationLevel::O2,d);
    require(!d.hasErrors(),"directed reverse compile");exact(matrix(m),matrix(result),"directed reverse "+chip.id);
  }
  for(auto chip:chips)if(chip.id!="cx"&&chip.id!="ecr"){
    chip.directedConnectivity=true;chip.allToAll=false;chip.coupling={{0,1}};
    OpKind kind=OpKind::Cz;auto entangler=chip.decompose.twoQubitEntangler;
    if(entangler=="ms")kind=OpKind::Ms;else if(entangler=="rxx")kind=OpKind::Rxx;
    else if(entangler=="rzz")kind=OpKind::Rzz;else if(entangler=="iswap")kind=OpKind::ISwap;
    else if(entangler=="sqrt_iswap")kind=OpKind::SqrtISwap;else if(entangler=="sqrt_iswap_inv")kind=OpKind::SqrtISwapInv;
    else if(entangler=="syc")kind=OpKind::Syc;
    WireOp reverse{kind,{1,0},{},{}};
    if(kind==OpKind::Rxx||kind==OpKind::Rzz)reverse.attributes={angleAttr(.371)};
    auto input=rebuild(WireCircuit{"symmetric_locus","generic",2,0,.23,{reverse}});
    Diagnostics invalid;validateCompiled(input,chip,invalid);
    require(invalid.hasErrors(),"reversed compiled symmetric locus rejected");
    for(auto level:{OptimizationLevel::O0,OptimizationLevel::O3}){
      Diagnostics diag;auto native=PassManager{}.compile(input,chip,level,diag);
      require(!diag.hasErrors(),"canonical symmetric locus "+chip.id);
      exact(matrix(input),matrix(native),"canonical symmetric locus phase "+chip.id);
      bool found=false;
      for(const auto& op:flatten(native).instructions)if(op.qubits.size()==2&&op.kind!=OpKind::Barrier){
        require(op.qubits==std::vector<int>({0,1}),"symmetric gate uses actual provider locus");found=true;
      }
      require(found,"symmetric entangler retained");
    }
  }
  auto parsed=parser::parse("target generic\nqubit q[2]\nbit c[5]\nx q[0]\nc[3] = measure q[0]\nreset q[0]\nc[0] = measure q[0]\nc[4] = measure q[1]\n");
  require(bool(parsed.module),"sparse source parse");
  std::mt19937_64 random(42);auto counts=sim::sample(*parsed.module,32,random);
  require(counts.size()==1&&counts["01000"]==32,"sparse reset readout");
  auto json=emit::emitPhysicalJson(*parsed.module);require(json.find("\"num_clbits\":5")!=std::string::npos,"JSON width");
  auto qasm=emit::emitQasm3(*parsed.module);require(qasm.find("c[3] = measure q[0]")!=std::string::npos,"QASM readout mapping");
  auto qir=emit::emitQir(*parsed.module);require(qir.find("array_record_output(i64 5")!=std::string::npos,"QIR output width");
  // A small active circuit on a wide chip must not allocate 2^156 amplitudes.
  auto c=flatten(*parsed.module);c.numQubits=156;c.instructions[0].qubits[0]=155;
  c.instructions[1].qubits[0]=155;c.instructions[2].qubits[0]=155;c.instructions[3].qubits[0]=155;
  counts=sim::sample(rebuild(c),4,random);require(counts["01000"]==4,"wide physical compaction");
  auto dynamic=parser::parse(R"(target generic
qubit q[4]
bit c[4]
h q[0]
c[0] = measure q[0]
if c[0] == 1 {
x q[1]
cx q[1], q[3]
} else {
x q[2]
cx q[2], q[3]
}
c[1] = measure q[1]
c[2] = measure q[2]
c[3] = measure q[3]
)");
  require(bool(dynamic.module),"dynamic source parse");
  auto dynamicChip=chips.front();dynamicChip.qubits=4;dynamicChip.allToAll=false;dynamicChip.coupling={{0,1},{1,2},{2,3}};
  dynamicChip.supports.feedforward=registry::CapabilityFlags::Feedforward::Full;
  Diagnostics dynamicDiag;auto native=PassManager{}.compile(*dynamic.module,dynamicChip,OptimizationLevel::O3,dynamicDiag);
  if(dynamicDiag.hasErrors())for(const auto& e:dynamicDiag.items())std::cerr<<e.message<<'\n';
  require(!dynamicDiag.hasErrors(),"dynamic native compile");
  require(native.finalLayout.size()==4,"dynamic final layout width");
  auto finalLayout=flatten(native).finalLayout;
  require(emit::emitPhysicalJson(native).find("\"logical_to_physical\":[")!=std::string::npos,"JSON logical layout");
  counts=sim::sample(native,128,random);std::size_t total=0;
  for(const auto& [bits,n]:counts){require(bits=="1011"||bits=="1100","branch routing/readout: "+bits);total+=n;}
  require(total==128&&counts.size()==2,"both runtime branches execute");
  auto roundDynamic=parse(print(native),dynamicDiag);require(bool(roundDynamic),"dynamic native IR roundtrip");
  require(emit::emitPhysicalJson(native).find("\"condition_value\":1")!=std::string::npos,"dynamic JSON predicate");
  require(emit::emitQir(*dynamic.module,&dynamicChip).find("@__quantum__qis__read_result__body")!=std::string::npos,"QIR conditional readout");
  dynamicChip.supports.feedforward=registry::CapabilityFlags::Feedforward::None;Diagnostics unsupported;
  PassManager{}.compile(*dynamic.module,dynamicChip,OptimizationLevel::O1,unsupported);require(unsupported.hasErrors(),"unsupported feed-forward fails");
  {
    Module braket;braket.targetAttr="ionq";Builder build(braket);auto a=build.allocQubit(),b=build.allocQubit();
    auto pair=build.ms(a,b);auto first=build.measure(pair.first),second=build.measure(pair.second);
    setMeasurementTarget(braket,first,2);setMeasurementTarget(braket,second,0);braket.globalPhase=0.3;
    emit::EmitOptions options;options.braketVerbatim=true;auto ion=chips[3];ion.provider="ionq";
    auto qasm=emit::emitQasm3(braket,&ion,options);
    require(qasm.find("ms(0, 0, ")!=std::string::npos,"Braket MS angle syntax");
    require(qasm.find("c[2] = measure $0")>qasm.find("}\n"),"Braket measurement outside verbatim");
    require(qasm.find("gphase(")==std::string::npos,"Braket scalar phase retained in artifact, not unverified QPU operation");
    auto aqt=ion;aqt.provider="aws";aqt.vendor="aqt";
    Module aqtModule;Builder builder(aqtModule);auto left=builder.allocQubit(),right=builder.allocQubit();left=builder.u1q(.3,.4,left);builder.rxx(.2,left,right);
    auto aqtQasm=emit::emitQasm3(aqtModule,&aqt,options);
    require(aqtQasm.find("prx(")!=std::string::npos&&aqtQasm.find("xx(")!=std::string::npos,"Braket AQT gate aliases");
  }
  registry::ChipInfo disconnected=chips.front();disconnected.allToAll=false;disconnected.coupling={};
  Module m;m.targetAttr="generic";Builder b(m);auto a=b.allocQubit(),z=b.allocQubit();b.cx(a,z);Diagnostics d;
  PassManager{}.compile(m,disconnected,OptimizationLevel::O0,d);require(d.hasErrors(),"disconnected routing must fail");
  {
    auto source=parser::parse("target generic\nqubit q[3]\nbit c[4]\nh q[0]\ncx q[1], q[2]\nrx(0.31) q[0]\ncx q[0], q[2]\ncx q[1], q[2]\ncx q[0], q[1]\nc[3] = measure q[0]\nc[0] = measure q[1]\nc[2] = measure q[2]\n");
    require(bool(source.module),"layout equivalence source");
    auto wide=chips.front();wide.qubits=156;wide.allToAll=false;wide.coupling={{3,7},{7,9},{9,12}};
    Diagnostics diag;auto native=PassManager{}.compile(*source.module,wide,OptimizationLevel::O2,diag);
    if(diag.hasErrors())for(const auto& item:diag.items())std::cerr<<item.message<<'\n';
    require(!diag.hasErrors(),"routed wide compile");
    require(native.initialLayout.size()==3&&native.finalLayout.size()==3,"logical layout size");
    require(sim::equivalent(*source.module,native).equivalent,"exhaustive mapped unitary and readout");
    auto bad=flatten(native);for(auto& op:bad.instructions)if(op.kind==OpKind::Measure){op.clbit=1;break;}
    require(!sim::equivalent(*source.module,rebuild(bad)).equivalent,"readout mismatch caught");
  }
  std::cout<<"owned compiler: exact matrices, phase, all optimization levels, readout, reset, compaction, and disconnected routing passed\n";
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}

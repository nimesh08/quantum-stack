#include "phonon/lower/Lowering.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace phonon::lower {

std::string emitSpinorSource(const spinor::dialect::Module& m) {
  namespace sd = spinor::dialect;
  using K = sd::OpKind;
  std::map<sd::ValueId, std::size_t> slots;
  std::size_t qubits = 0, bits = m.numClbits, allocatedBits = 0, measurements = 0;
  for (const auto& op : m.ops()) {
    if (op.kind == K::AllocQubit) slots[op.results.at(0)] = qubits++;
    if (op.kind == K::AllocBit) ++allocatedBits;
    if (op.kind == K::Measure) ++measurements;
    for (const auto& a : op.attributes) if (a.name == "clbit")
      bits = std::max(bits, static_cast<std::size_t>(std::get<double>(a.value)) + 1);
  }
  bits = std::max(bits, allocatedBits);
  if (bits == 0) bits = measurements;
  std::ostringstream out;
  out << std::setprecision(17);
  out << "target " << (m.targetAttr.empty() ? "generic" : m.targetAttr) << '\n';
  if (qubits) out << "qubit q[" << qubits << "]\n";
  if (bits) out << "bit c[" << bits << "]\n";
  auto slot = [&](sd::ValueId v) {
    auto it = slots.find(v);
    if (it == slots.end()) throw std::runtime_error("unmapped qubit while serializing lowered source");
    return it->second;
  };
  auto angle = [&](const sd::Op& op, const char* name) {
    for (const auto& a : op.attributes) if (a.name == name) {
      auto value = std::get<double>(a.value);
      if (!std::isfinite(value)) throw std::runtime_error("non-finite gate parameter");
      return value;
    }
    throw std::runtime_error(std::string("missing gate parameter: ") + name);
  };
  std::size_t nextMeasurement = 0;
  for (const auto& op : m.ops()) {
    if (op.kind == K::AllocQubit || op.kind == K::AllocBit) continue;
    if(op.kind==K::If){out<<"if c["<<angle(op,"condition_clbit")<<"] == "<<angle(op,"condition_value")<<" {\n";continue;}
    if(op.kind==K::Else){out<<"} else {\n";continue;}
    if(op.kind==K::EndIf){out<<"}\n";continue;}
    if(op.kind==K::GlobalPhase){out<<"gphase("<<angle(op,"angle")<<")\n";continue;}
    if (op.kind == K::Measure) {
      auto bit = nextMeasurement++;
      for (const auto& a : op.attributes) if (a.name == "clbit")
        bit = static_cast<std::size_t>(std::get<double>(a.value));
      if (bit >= bits) throw std::runtime_error("measurement destination exceeds classical register");
      out << "c[" << bit << "] = measure q[" << slot(op.operands.at(0)) << "]\n";
      continue;
    }
    std::string name(sd::opMnemonic(op.kind));
    auto dot = name.find('.');
    if (dot != std::string::npos) name.erase(0, dot + 1);
    if(op.kind==K::Barrier&&op.operands.empty()){out<<"barrier\n";continue;}
    if (op.operands.empty()) throw std::runtime_error("unsupported operation while serializing: " + name);
    out << name;
    if (op.kind == K::U1q) out << '(' << angle(op, "theta") << ", " << angle(op, "phi") << ')';
    else if (op.kind == K::Rx || op.kind == K::Ry || op.kind == K::Rz ||
             op.kind == K::Gpi || op.kind == K::Gpi2 || op.kind == K::Rzz || op.kind == K::Rxx)
      out << '(' << angle(op, "angle") << ')';
    for (std::size_t i = 0; i < op.operands.size(); ++i)
      out << (i ? ", q[" : " q[") << slot(op.operands[i]) << ']';
    out << '\n';
    for (std::size_t i = 0; i < op.results.size() && i < op.operands.size(); ++i)
      if (m.typeOf(op.results[i]).kind == sd::TypeKind::Qubit)
        slots[op.results[i]] = slot(op.operands[i]);
  }
  return out.str();
}

}  // namespace phonon::lower

#pragma once
#include "spinor/dialect/Circuit.h"
#include <map>
#include <set>
#include <stdexcept>

namespace spinor::dialect {
// IQM MOVE is not a full two-qubit SWAP. Its phase is only guaranteed to
// cancel when the same qubit/resonator pair closes an initially empty
// resonator's sandwich. Enforce that contract before evaluating or emitting.
inline void validateResonatorCircuit(const WireCircuit& circuit) {
  std::set<int> resonators;
  for (int r : circuit.resonatorQubits)
    if (r<0 || static_cast<std::size_t>(r)>=circuit.numQubits || !resonators.insert(r).second)
      throw std::runtime_error("invalid reserved resonator index");
  for (const auto* layout : {&circuit.initialLayout,&circuit.finalLayout})
    for (int q : *layout) if (resonators.contains(q))
      throw std::runtime_error("logical inputs/outputs cannot occupy a resonator");
  std::map<int,int> stored; // resonator -> parked computational qubit
  auto parked = [&](int q) {
    return std::any_of(stored.begin(),stored.end(),[&](const auto& item){return item.second==q;});
  };
  for (const auto& op : circuit.instructions) {
    if (isControl(op.kind) || op.kind==OpKind::Barrier) {
      if (!stored.empty()) throw std::runtime_error("MOVE sandwiches must close before control-flow or barrier boundaries");
      continue;
    }
    if (op.kind==OpKind::GlobalPhase) continue;
    if (op.kind==OpKind::Move) {
      if (op.qubits.size()!=2 || !op.attributes.empty()) throw std::runtime_error("MOVE requires two components and no parameters");
      const int q=op.qubits[0],r=op.qubits[1];
      if (resonators.contains(q) || !resonators.contains(r)) throw std::runtime_error("MOVE operands must be ordered (qubit, resonator)");
      auto held=stored.find(r);
      if (held==stored.end()) {
        if (parked(q)) throw std::runtime_error("qubit state is already stored in a resonator");
        stored[r]=q;
      } else {
        if (held->second!=q) throw std::runtime_error("MOVE must return the state to its original qubit");
        stored.erase(held);
      }
      continue;
    }
    for (int q : op.qubits) {
      if (parked(q)) throw std::runtime_error("operation touches a qubit whose state is stored in a resonator");
      if (resonators.contains(q) &&
          (op.kind!=OpKind::Cz || op.qubits.size()!=2 || !stored.contains(q)))
        throw std::runtime_error("reserved resonators support only CZ during a balanced MOVE sandwich");
    }
    if (op.kind==OpKind::Cz && op.qubits.size()==2 &&
        resonators.contains(op.qubits[0]) && resonators.contains(op.qubits[1]))
      throw std::runtime_error("resonator-resonator CZ is unsupported");
  }
  if (!stored.empty()) throw std::runtime_error("unclosed MOVE sandwich at end of circuit");
}
}

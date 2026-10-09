#pragma once
#include "spinor/registry/Registry.h"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace spinor::registry {
// Keep physical slots distinct from computational capacity. The explicit
// partition also supports architectures whose component labels interleave.
inline std::vector<int> computationalComponents(const ChipInfo& chip) {
  std::set<int> resonators, computers;
  auto insert = [&](int q, auto& set) {
    if (q < 0 || static_cast<std::size_t>(q) >= chip.qubits || !set.insert(q).second)
      throw std::runtime_error("invalid or duplicate physical component index");
  };
  for (int r : chip.resonatorQubits) insert(r, resonators);
  auto result = chip.computationalQubits;
  if (result.empty())
    for (std::size_t q=0;q<chip.qubits;++q) if (!resonators.contains(static_cast<int>(q))) result.push_back(static_cast<int>(q));
  for (int q : result) {
    insert(q, computers);
    if (resonators.contains(q)) throw std::runtime_error("component cannot be both qubit and resonator");
  }
  if (computers.size()+resonators.size()!=chip.qubits || computers.empty())
    throw std::runtime_error("computational/resonator partition must cover all physical slots and contain a qubit");
  for (const auto& [q,r] : chip.moveLoci)
    if (!computers.contains(q) || !resonators.contains(r))
      throw std::runtime_error("MOVE locus must be ordered (computational qubit, resonator)");
  for (const auto& [a,b] : chip.czLoci)
    if (a==b || a<0 || b<0 || static_cast<std::size_t>(a)>=chip.qubits ||
        static_cast<std::size_t>(b)>=chip.qubits || (resonators.contains(a)&&resonators.contains(b)))
      throw std::runtime_error("CZ locus must join distinct qubits or a qubit and a resonator");
  const bool nativeMove=std::find(chip.nativeGates.begin(),chip.nativeGates.end(),"move")!=chip.nativeGates.end();
  if ((nativeMove && resonators.empty()) || (!chip.moveLoci.empty() && !nativeMove))
    throw std::runtime_error("native MOVE requires resonator metadata and calibrated MOVE loci");
  return result;
}
}

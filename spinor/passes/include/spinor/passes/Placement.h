// spinor/passes/include/spinor/passes/Placement.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/CouplingGraph.h"

#include <vector>

namespace spinor::passes {

struct Layout {
  std::vector<int> v2p;   // virtual_idx -> physical_idx
  std::vector<int> p2v;   // physical_idx -> virtual_idx (-1 free)

  void apply_swap(int a, int b) {
    int va = p2v[a];
    int vb = p2v[b];
    p2v[a] = vb;
    p2v[b] = va;
    if (va >= 0) v2p[va] = b;
    if (vb >= 0) v2p[vb] = a;
  }
};

class Placement {
 public:
  // Run interaction-graph-driven placement of `module`'s virtual
  // qubits onto `g`. The number of virtual qubits is read from the
  // count of alloc_qubit ops in `module`.
  Layout run(const dialect::Module& module, const CouplingGraph& g) const;
};

}  // namespace spinor::passes

// spinor/passes/include/spinor/passes/Collect2qBlocks.h
//
// Collect2qBlocks — find maximal connected sub-DAGs touching
// exactly two qubits. Pure topology pass; no chip info needed.
// Output is a list of op-id groups. Mirrors Qiskit's
// `Collect2qBlocks` pass.

#pragma once

#include "spinor/dialect/Spinor.h"

#include <cstdint>
#include <vector>

namespace spinor::passes {

// A "block" is a contiguous run of operations on a fixed pair of
// (virtual) qubits, uninterrupted by any operation touching a
// third qubit or any fence (measure/reset/barrier).
struct TwoQBlock {
  int qa = -1;   // first qubit (lower index)
  int qb = -1;   // second qubit
  std::vector<dialect::OpId> ops;
};

class Collect2qBlocks {
 public:
  // Scan `m` and return the list of 2Q blocks. Vendor-agnostic.
  std::vector<TwoQBlock> run(const dialect::Module& m) const;
};

}  // namespace spinor::passes

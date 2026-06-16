// spinor/sim/include/spinor/sim/Simulator.h
//
// Phase A simulator + check lane.
//
// Statevector simulator: in-tree, dense complex<double>, up to
// ~24 qubits. No external dependency. Built always.
//
// Stim wrapper: a thin adapter for Clifford circuits. Compiled
// only when SPINOR_HAVE_STIM is defined. M8 ships a stub that
// returns "not available"; the full integration lands once Stim
// is vendored or installed system-wide. Decision D7.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

#include <complex>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace spinor::sim {

using cdbl = std::complex<double>;

// Run a small (≤24 qubit) circuit on the dense statevector
// engine. Initial state is |0...0>. Measurement ops are treated
// as projectors: the sim records each measure but does not
// collapse for equivalence-check purposes (we compare the
// pre-measure state vector).
//
// The mapping from `dialect::Module` ValueIds to qubit indices
// is by allocation order: the k-th `alloc_qubit` op corresponds
// to qubit k in the simulator.
struct StateVector {
  std::size_t qubits = 0;
  std::vector<cdbl> amps;  // size 1 << qubits
};

StateVector simulate(const dialect::Module& m);

struct EquivResult {
  bool equivalent = false;
  double maxAbsDiff = 0.0;        // post-phase-removal
  std::optional<cdbl> phase;      // applied to b before comparison
};

// Are two modules equivalent up to a global phase?
EquivResult equivalent(const dialect::Module& a,
                       const dialect::Module& b,
                       double tol = 1e-6);

struct ResourceEstimate {
  std::size_t totalGates = 0;
  std::size_t twoQubitGates = 0;
  std::size_t depth = 0;
  std::size_t qubits = 0;
  std::size_t measurements = 0;
  // If a chip is supplied, an estimated total error and a
  // monetary cost per shot.
  std::optional<double> totalErrorEstimate;
  std::optional<double> shotCostUsd;
};

ResourceEstimate estimate(const dialect::Module& m,
                          const registry::ChipInfo* chip = nullptr,
                          std::size_t shots = 1000);

}  // namespace spinor::sim

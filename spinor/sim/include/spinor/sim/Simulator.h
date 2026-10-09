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
#include <map>
#include <random>
#include <string>
#include <vector>

namespace spinor::sim {

using cdbl = std::complex<double>;

// Run a small (≤24 qubit) unitary circuit from |0...0>. Terminal
// measurements are ignored; reset, control flow and gates following
// measurement require the trajectory sampler instead.
//
// The mapping from `dialect::Module` ValueIds to qubit indices
// is by allocation order: the k-th `alloc_qubit` op corresponds
// to qubit k in the simulator.
struct StateVector {
  std::size_t qubits = 0;
  std::vector<cdbl> amps;  // size 1 << qubits
};

StateVector simulate(const dialect::Module& m);

// Sample projective measurements and resets, retaining classical destination
// indices. Physical wires are compacted before allocating a statevector.
std::map<std::string, std::size_t> sample(const dialect::Module& m,
    std::size_t shots, std::mt19937_64& rng);

struct EquivResult {
  bool equivalent = false;
  double maxAbsDiff = 0.0;        // post-phase-removal
  std::optional<cdbl> phase;      // applied to b before comparison
};

// Exhaustive unitary equivalence up to one global phase, including terminal
// readout mapping. Honors initial/final placement and zero-initialized routing
// ancillas. Limited to 8 logical and 12 active physical qubits; unsupported
// dynamic/nonunitary circuits and larger inputs throw instead of claiming a pass.
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

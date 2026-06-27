// spinor/emit/include/spinor/emit/Emitters.h
//
// Phase A emitters.

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/registry/Registry.h"

#include <string>

namespace spinor::emit {

struct EmitOptions {
  // OpenQASM only.
  bool braketVerbatim = false;     // wrap body in `#pragma braket verbatim`
};

// OpenQASM 3.1 emitter. The `chip` param is required when
// `opts.braketVerbatim` is true (uses `$<n>` physical qubit
// syntax); otherwise we emit named registers.
std::string emitQasm3(const dialect::Module& m,
                      const registry::ChipInfo* chip = nullptr,
                      EmitOptions opts = {});

// QIR emitter. Always emits the textual LLVM-IR form (.ll); QIR
// bitcode is just `llc -filetype=obj` away. Profile is selected
// from `chip->supports.feedforward`: None → Base; Limited/Full →
// Adaptive (at the cost of additional declarations).
std::string emitQir(const dialect::Module& m,
                    const registry::ChipInfo* chip = nullptr);

// Quil emitter. Narrow but cheap: walks the IR.
std::string emitQuil(const dialect::Module& m);

// Qiskit Python emitter. Generates a self-contained Python program
// that builds the equivalent `QuantumCircuit`, runs Qiskit's
// `transpile(..., optimization_level=N)` against the target IBM
// backend, submits via `SamplerV2`, and prints a single-line JSON
// blob `{"histogram": {...}, "job_id": "...", "depth": N, "gates": {...}}`
// to stdout. Used by `spinorc emit`/`spinorc run` for IBM chips.
//
// The emitter walks the IR at its INPUT level (universal gates,
// before decompose/cleanup) — Qiskit's transpiler does the
// lowering to the chip's native basis better than Spinor's
// scaffolded passes can today.
std::string emitQiskitPython(const dialect::Module& m,
                             const registry::ChipInfo& chip,
                             int optimizationLevel = 3);

}  // namespace spinor::emit

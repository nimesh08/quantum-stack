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

}  // namespace spinor::emit

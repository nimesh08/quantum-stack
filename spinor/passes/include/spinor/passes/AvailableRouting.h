#pragma once
#include "spinor/dialect/Circuit.h"
#include "spinor/passes/PassManager.h"
#include <optional>

namespace spinor::passes {
// Compile on a usable subgraph, then restore the provider's physical indices.
// A missing result means the original target needs no restricted subgraph.
std::optional<dialect::Module> compileAvailableCircuit(const dialect::Module&,
    const registry::ChipInfo&, OptimizationLevel, dialect::Diagnostics&);
void validateAvailableCircuit(const dialect::WireCircuit&, const registry::ChipInfo&);
}

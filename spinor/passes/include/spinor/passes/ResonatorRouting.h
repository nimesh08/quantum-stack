#pragma once
#include "spinor/passes/PassManager.h"

namespace spinor::passes {
// Compile on an effective computational-qubit graph, then lower each mediated
// CZ into the calibrated MOVE/CZ/MOVE protocol without exposing resonators as
// logical storage or feeding their partial operation into KAK synthesis.
dialect::Module compileResonatorCircuit(const dialect::Module& input,
    const registry::ChipInfo& chip, OptimizationLevel level, dialect::Diagnostics& diagnostics,
    CompilationReport* report = nullptr);
}

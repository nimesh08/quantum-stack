#pragma once
#include "spinor/passes/PassManager.h"
#include "spinor/passes/CompilationReport.h"
namespace spinor::passes {
// Prepared logical input; synthesizes and routes at exact operation loci.
dialect::Module compileHeterogeneousCircuit(const dialect::Module&,
    const registry::ChipInfo&, OptimizationLevel, dialect::Diagnostics&,
    CompilationReport* = nullptr);
}

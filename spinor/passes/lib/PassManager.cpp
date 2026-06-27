// spinor/passes/lib/PassManager.cpp

#include "spinor/passes/PassManager.h"

#include "spinor/passes/Cleanup.h"
#include "spinor/passes/Collect2qBlocks.h"
#include "spinor/passes/CommutativeCancellation.h"
#include "spinor/passes/ConsolidateBlocks.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Decomposition.h"
#include "spinor/passes/KakResynthesis.h"
#include "spinor/passes/OneQubitEulerResynthesis.h"
#include "spinor/passes/OptimizationLoop.h"
#include "spinor/passes/Placement.h"
#include "spinor/passes/Routing.h"
#include "spinor/passes/SynthesisTraits.h"
#include "spinor/passes/VF2PostLayout.h"

namespace spinor::passes {

dialect::Module PassManager::compile(const dialect::Module& module,
                                     const registry::ChipInfo& chip,
                                     OptimizationLevel level,
                                     dialect::Diagnostics& diag) const {
  // Stage 1: Placement (always run; chip-agnostic — only reads
  // the coupling map).
  CouplingGraph g(chip.qubits, chip.coupling, chip.allToAll);
  Placement pl;
  auto layout = pl.run(module, g);

  // Stage 2: Routing (always run; chip-agnostic — only reads the
  // coupling map and uses SABRE forward search).
  Routing routing;
  auto routed = routing.run(module, chip, g, layout);

  // Stage 3: Decomposition (always run; vendor-modular via the
  // YAML registry strings — entangler / rotation_gate / pi_2_gate).
  Decomposition dec;
  auto decomposed = dec.run(routed.module, chip, diag);

  // Stage 4: Cleanup. O0 returns raw post-decomposition IR for
  // hardware characterization use. O1/O2/O3 run the local
  // peephole (vendor-aware overload).
  if (level == OptimizationLevel::O0) {
    return decomposed;
  }

  Cleanup cl;
  auto cleaned = cl.run(decomposed, chip);

  // Stage 5: optimization pipeline. Vendor-modular passes,
  // selected by level. All passes take `const SynthesisTraits&`
  // (or nothing) — zero `if (vendor == ibm)` anywhere.
  //
  // Phase A: the per-pass implementations are scaffolded as
  // identity transforms; the loop infrastructure and pass
  // wiring are in place so each individual pass can be filled
  // in without changing call sites or the CLI/API contract.
  SynthesisTraits traits = computeTraits(chip);

  // O1 loop body: peephole + 1Q Euler resynthesis +
  // CommutativeCancellation (CommutativeCancellation enabled at
  // O2+; here it runs but is a no-op at O1 since its rule table
  // is sparse). Wrapped in a fixed-point loop.
  if (level == OptimizationLevel::O1 ||
      level == OptimizationLevel::O2 ||
      level == OptimizationLevel::O3) {
    OptimizationLoop<FixedPointCriterion> loop;
    auto body = [&](const dialect::Module& m) -> dialect::Module {
      OneQubitEulerResynthesis euler;
      auto a = euler.run(m, traits);
      if (level == OptimizationLevel::O2 ||
          level == OptimizationLevel::O3) {
        CommutativeCancellation cc;
        a = cc.run(a, traits);
      }
      Cleanup peephole;
      return peephole.run(a, chip);
    };
    cleaned = loop.run(cleaned, body, FixedPointCriterion{}, /*maxIters=*/8);
  }

  // O3: 2Q block KAK resynthesis + VF2 post-layout rescue,
  // wrapped in a minimum-point loop.
  if (level == OptimizationLevel::O3) {
    OptimizationLoop<MinimumPointCriterion> loop3;
    auto body3 = [&](const dialect::Module& m) -> dialect::Module {
      Collect2qBlocks collect;
      auto blocks = collect.run(m);
      ConsolidateBlocks consolidate;
      auto consolidated = consolidate.run(m, blocks);
      KakResynthesis kak;
      auto resynth = kak.run(consolidated, blocks, traits);
      OneQubitEulerResynthesis euler;
      resynth = euler.run(resynth, traits);
      CommutativeCancellation cc;
      resynth = cc.run(resynth, traits);
      Cleanup peephole;
      return peephole.run(resynth, chip);
    };
    cleaned = loop3.run(cleaned, body3, MinimumPointCriterion{}, /*maxIters=*/6);
    VF2PostLayout vf2;
    cleaned = vf2.run(cleaned, chip);
  }

  return cleaned;
}

}  // namespace spinor::passes

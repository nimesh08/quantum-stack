// phonon/optimizer/lib/Pipeline.cpp

#include "phonon/optimizer/Pipeline.h"

namespace phonon::optimizer {

Stats runPipeline(dialect::Module& m, PipelineConfig cfg) {
  if (!cfg.synth) cfg.synth = makeNullSynthesizer();
  if (!cfg.zx)    cfg.zx    = makeNullZxSimplifier();

  Stats agg;

  // 1) First built pass cycle. We run cancel + commute + merge so
  //    that opportunities exposed by commutation (e.g. an Rz that
  //    can slide past a CX onto its target) get merged in the same
  //    sweep. `commuteAndReduce` is what catches the
  //    `H A; CX A,B; H A` -> `CX inverse direction` style patterns
  //    that a pure local cancel would miss.
  Stats a = cancelInversePairs(m);
  Stats b = mergeRotations(m);
  Stats c1 = commuteAndReduce(m);
  agg.gatesRemoved      += a.gatesRemoved;
  agg.rotationsMerged   += b.rotationsMerged;
  agg.gatesRemoved      += c1.gatesRemoved;
  agg.rotationsMerged   += c1.rotationsMerged;
  agg.reorderingsApplied = c1.reorderingsApplied;

  // 2) Borrowed: synthesis then ZX simplification.
  cfg.synth->synthesize(m);
  cfg.zx->simplify(m);

  // 3) Re-run built passes if requested.
  if (cfg.runBuiltAfterBorrowed) {
    Stats c = cancelInversePairs(m);
    Stats d = mergeRotations(m);
    Stats c2 = commuteAndReduce(m);
    agg.gatesRemoved      += c.gatesRemoved;
    agg.rotationsMerged   += d.rotationsMerged;
    agg.gatesRemoved      += c2.gatesRemoved;
    agg.rotationsMerged   += c2.rotationsMerged;
    agg.reorderingsApplied += c2.reorderingsApplied;
  }

  // 4) Schedule (depth report only in Phase B).
  Stats s = schedule(m);
  agg.depthBefore = s.depthBefore;
  agg.depthAfter  = s.depthAfter;

  return agg;
}

}  // namespace phonon::optimizer

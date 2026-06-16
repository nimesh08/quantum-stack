// phonon/optimizer/lib/Pipeline.cpp

#include "phonon/optimizer/Pipeline.h"

namespace phonon::optimizer {

Stats runPipeline(dialect::Module& m, PipelineConfig cfg) {
  if (!cfg.synth) cfg.synth = makeNullSynthesizer();
  if (!cfg.zx)    cfg.zx    = makeNullZxSimplifier();

  Stats agg;

  // 1) First built pass cycle.
  Stats a = cancelInversePairs(m);
  Stats b = mergeRotations(m);
  agg.gatesRemoved      += a.gatesRemoved;
  agg.rotationsMerged   += b.rotationsMerged;

  // 2) Borrowed: synthesis then ZX simplification.
  cfg.synth->synthesize(m);
  cfg.zx->simplify(m);

  // 3) Re-run built passes if requested.
  if (cfg.runBuiltAfterBorrowed) {
    Stats c = cancelInversePairs(m);
    Stats d = mergeRotations(m);
    agg.gatesRemoved      += c.gatesRemoved;
    agg.rotationsMerged   += d.rotationsMerged;
  }

  // 4) Schedule (depth report only in Phase B).
  Stats s = schedule(m);
  agg.depthBefore = s.depthBefore;
  agg.depthAfter  = s.depthAfter;

  return agg;
}

}  // namespace phonon::optimizer

// spinor/passes/lib/TwoQubitDecomposer.cpp
//
// Phase A: scaffolded. Full Cartan/KAK math lands in a follow-up
// patch (see plan §4a). The interface is reserved here so callers
// (KakResynthesis) link against a stable target. The math is
// pure textbook — no vendor branches — see the citations in the
// header.

#include "spinor/passes/TwoQubitDecomposer.h"

namespace spinor::passes {

KakResult TwoQubitDecomposer::decompose(const U4& u,
                                        const SynthesisTraits& traits) const {
  (void)u;
  (void)traits;
  return KakResult{};
}

}  // namespace spinor::passes

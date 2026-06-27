// spinor/passes/lib/CommutativeCancellation.cpp

#include "spinor/passes/CommutativeCancellation.h"

namespace spinor::passes {

dialect::Module CommutativeCancellation::run(const dialect::Module& m,
                                             const SynthesisTraits& traits) const {
  (void)traits;
  return m;
}

}  // namespace spinor::passes

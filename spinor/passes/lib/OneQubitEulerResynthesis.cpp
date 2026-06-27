// spinor/passes/lib/OneQubitEulerResynthesis.cpp

#include "spinor/passes/OneQubitEulerResynthesis.h"

namespace spinor::passes {

dialect::Module OneQubitEulerResynthesis::run(const dialect::Module& m,
                                              const SynthesisTraits& traits) const {
  (void)traits;
  return m;
}

}  // namespace spinor::passes

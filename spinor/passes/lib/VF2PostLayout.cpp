// spinor/passes/lib/VF2PostLayout.cpp

#include "spinor/passes/VF2PostLayout.h"

namespace spinor::passes {

dialect::Module VF2PostLayout::run(const dialect::Module& m,
                                   const registry::ChipInfo& chip) const {
  (void)chip;
  return m;
}

}  // namespace spinor::passes

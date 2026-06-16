// spinor/passes/include/spinor/passes/Routing.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/passes/CouplingGraph.h"
#include "spinor/passes/Placement.h"
#include "spinor/registry/Registry.h"

namespace spinor::passes {

struct RoutingResult {
  dialect::Module module;
  Layout initialLayout;
  Layout finalLayout;
  std::size_t swapCount = 0;
};

class Routing {
 public:
  // SABRE forward routing. Inserts SWAP ops where needed; the
  // output module's contract attribute is `chip.id`.
  RoutingResult run(const dialect::Module& m,
                    const registry::ChipInfo& chip,
                    const CouplingGraph& g,
                    Layout initial) const;
};

}  // namespace spinor::passes

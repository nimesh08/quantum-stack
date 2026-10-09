#pragma once
#include "spinor/dialect/Spinor.h"
namespace spinor::passes {
// Color only explicitly private storage using backward CFG liveness. Exported
// memory, legacy bit operands and initialized immutable values remain pinned.
// Every measurement and quantum operation remains in the output program.
class ClassicalStorageReuse {
public:
  dialect::Module run(const dialect::Module&) const;
};
}

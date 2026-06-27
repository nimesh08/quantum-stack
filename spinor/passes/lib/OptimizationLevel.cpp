// spinor/passes/lib/OptimizationLevel.cpp

#include "spinor/passes/OptimizationLevel.h"

namespace spinor::passes {

OptimizationLevel parseOptimizationLevel(const char* s) {
  if (!s || !*s) return kDefaultOptimizationLevel;
  const char* p = s;
  if ((p[0] == 'O' || p[0] == 'o') && p[1] != '\0') ++p;
  if (p[1] != '\0') return kDefaultOptimizationLevel;
  switch (p[0]) {
    case '0': return OptimizationLevel::O0;
    case '1': return OptimizationLevel::O1;
    case '2': return OptimizationLevel::O2;
    case '3': return OptimizationLevel::O3;
    default:  return kDefaultOptimizationLevel;
  }
}

}  // namespace spinor::passes

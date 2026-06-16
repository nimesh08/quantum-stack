// spinor/verify/include/spinor/verify/TargetInfo.h
//
// A minimal shim of the M4 registry record, sufficient for M3's
// W4-W6 enforcement. M4 will replace this with a full loader; M3's
// tests construct TargetInfo objects inline.

#pragma once

#include <cstddef>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace spinor::verify {

struct TargetInfo {
  std::string id;                                // "" or "generic" or device id
  bool generic = true;
  std::vector<std::string> nativeGates;          // ignored when generic

  // Coupling map: undirected, stored as (low, high) pairs with
  // low<high. Empty AND allToAll==false means "no two-qubit gate
  // is allowed" (effectively a 1-qubit chip).
  bool allToAll = true;                          // ignored when generic
  std::vector<std::pair<int, int>> coupling;
  std::size_t qubitCount = 0;                    // 0 means "unbounded" (generic)

  bool midCircuitMeasure = true;                 // relaxes W4

  // Helper: true if `pair` (a, b) is connected. Order-insensitive.
  bool connected(int a, int b) const {
    if (allToAll) return a != b;
    if (a == b) return false;
    int lo = a < b ? a : b;
    int hi = a < b ? b : a;
    for (const auto& [x, y] : coupling) {
      if (x == lo && y == hi) return true;
    }
    return false;
  }

  // Helper: true if mnemonic is in `nativeGates`.
  bool isNative(const std::string& mnemonic) const {
    for (const auto& g : nativeGates) {
      if (g == mnemonic) return true;
    }
    return false;
  }
};

// Convenience constructor for generic targets.
inline TargetInfo generic_target() {
  TargetInfo t;
  t.id = "generic";
  t.generic = true;
  return t;
}

}  // namespace spinor::verify

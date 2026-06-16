// spinor/registry/include/spinor/registry/Registry.h

#pragma once

#include "spinor/dialect/Spinor.h"
#include "spinor/verify/TargetInfo.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace spinor::registry {

struct DecomposeRecipe {
  enum class OneQubit { EulerZyz };
  enum class TwoQubit { Kak };

  OneQubit oneQubit = OneQubit::EulerZyz;
  TwoQubit twoQubit = TwoQubit::Kak;
  std::string oneQubitRotationGate;   // e.g. "rz"
  std::string oneQubitPi2Gate;        // e.g. "sx"; "" if none
  std::string twoQubitEntangler;      // e.g. "ecr"
  int twoQubitEntanglerCountMax = 3;
};

struct CapabilityFlags {
  bool midCircuitMeasure = true;
  enum class Feedforward { None, Limited, Full };
  Feedforward feedforward = Feedforward::None;
  bool reset = true;
};

struct ChipInfo {
  std::string id;
  std::string provider;
  std::size_t qubits = 0;
  std::vector<std::string> nativeGates;

  // Either an all-to-all chip OR an explicit edge list.
  bool allToAll = false;
  std::vector<std::pair<int, int>> coupling;

  CapabilityFlags supports;
  DecomposeRecipe decompose;

  double pricePerShotUsd = 0.0;
  std::optional<double> pricePerMinuteUsd;

  std::string notes;
  std::string calibrationSource;
  std::string calibrationRefresh;
  std::filesystem::path calibrationStore;
};

class Registry {
 public:
  // Load all chips/ and topologies/ under `root`. Diagnostics
  // accumulate; chips with errors are dropped.
  static Registry load(const std::filesystem::path& root,
                       dialect::Diagnostics& diag);

  bool has(const std::string& id) const {
    return chips_.find(id) != chips_.end();
  }
  const ChipInfo& get(const std::string& id) const {
    return chips_.at(id);
  }
  std::vector<std::string> ids() const;
  std::size_t size() const { return chips_.size(); }

  // Adapter to M3.
  verify::TargetInfo targetInfo(const std::string& id) const;

 private:
  std::unordered_map<std::string, ChipInfo> chips_;
  friend class RegistryAccess;
};

}  // namespace spinor::registry

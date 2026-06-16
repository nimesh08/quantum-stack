// spinor/registry/lib/Loader.cpp
//
// Validates and assembles ChipInfo records from YAML files.

#include "spinor/registry/Registry.h"

#include "Yaml.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace spinor::registry {

namespace fs = std::filesystem;
using yaml::Node;
using yaml::Parser;
using yaml::ParseError;

namespace {

// Set of known Spinor-mnemonic gate names without the "spinor." prefix.
const std::set<std::string>& knownNativeGates() {
  static const std::set<std::string> s = {
      "h",    "x",    "y",    "z",   "s",    "sdg",  "t",    "tdg",
      "rx",   "ry",   "rz",
      "cx",   "cz",   "swap",
      "ecr",  "ms",   "rzz",  "sx",  "sxdg",
      "gpi",  "gpi2", "u1q",
  };
  return s;
}

fs::path expandTilde(const std::string& s) {
  if (!s.empty() && s.front() == '~') {
    const char* home = std::getenv("HOME");
    if (home) return fs::path(home) / fs::path(s.substr(s.size() >= 2 && s[1] == '/' ? 2 : 1));
  }
  return fs::path(s);
}

// Convert "linear_n size: 4" to edges, etc.
struct ResolvedTopology {
  bool ok = false;
  bool allToAll = false;
  std::size_t qubits = 0;
  std::vector<std::pair<int, int>> edges;
  std::string error;
};

ResolvedTopology resolveTopology(const fs::path& topologiesDir,
                                 const std::string& name,
                                 std::size_t requestedSize) {
  ResolvedTopology r;
  // Built-in: linear_n.
  if (name == "linear_n") {
    if (requestedSize == 0) {
      r.error = "linear_n topology requires a positive size";
      return r;
    }
    r.qubits = requestedSize;
    for (std::size_t i = 0; i + 1 < requestedSize; ++i) {
      r.edges.push_back(
          {static_cast<int>(i), static_cast<int>(i + 1)});
    }
    r.ok = true;
    return r;
  }
  if (name == "all_to_all") {
    if (requestedSize == 0) {
      r.error = "all_to_all topology requires a positive size";
      return r;
    }
    r.qubits = requestedSize;
    r.allToAll = true;
    r.ok = true;
    return r;
  }
  // Otherwise read a file in topologiesDir/<name>.yaml
  fs::path file = topologiesDir / (name + ".yaml");
  if (!fs::exists(file)) {
    r.error = "topology file not found: " + file.string();
    return r;
  }
  try {
    Node n = Parser::parse_file(file.string());
    if (!n.isMap()) {
      r.error = "topology " + name + ": top-level must be a map";
      return r;
    }
    if (n.has("qubits") && n.at("qubits").isNumber()) {
      r.qubits = static_cast<std::size_t>(n.at("qubits").asInt());
    } else {
      r.error = "topology " + name + ": missing 'qubits'";
      return r;
    }
    if (n.has("all_to_all") && n.at("all_to_all").isBool() &&
        n.at("all_to_all").asBool()) {
      r.allToAll = true;
      r.ok = true;
      return r;
    }
    if (!n.has("edges") || !n.at("edges").isArray()) {
      r.error = "topology " + name + ": missing 'edges' list";
      return r;
    }
    for (const Node& e : n.at("edges").asArray()) {
      if (!e.isArray() || e.asArray().size() != 2) {
        r.error = "topology " + name + ": edge must be a 2-element list";
        return r;
      }
      int a = static_cast<int>(e.asArray()[0].asInt());
      int b = static_cast<int>(e.asArray()[1].asInt());
      if (a == b) {
        r.error = "topology " + name + ": self-loop edge (" +
                  std::to_string(a) + ", " + std::to_string(a) + ")";
        return r;
      }
      if (a < 0 || b < 0 ||
          static_cast<std::size_t>(a) >= r.qubits ||
          static_cast<std::size_t>(b) >= r.qubits) {
        r.error = "topology " + name + ": edge out of range: (" +
                  std::to_string(a) + ", " + std::to_string(b) + ")";
        return r;
      }
      int lo = std::min(a, b);
      int hi = std::max(a, b);
      r.edges.push_back({lo, hi});
    }
    std::sort(r.edges.begin(), r.edges.end());
    r.edges.erase(std::unique(r.edges.begin(), r.edges.end()), r.edges.end());
    r.ok = true;
    return r;
  } catch (const ParseError& pe) {
    r.error = "yaml parse error in topology " + name + ": " +
              pe.message + " (line " + std::to_string(pe.line) + ")";
    return r;
  } catch (const std::exception& e) {
    r.error = std::string("topology load failure: ") + e.what();
    return r;
  }
}

bool validateChip(ChipInfo& chip, dialect::Diagnostics& diag,
                  const std::string& sourcePath) {
  bool ok = true;
  if (chip.id.empty()) {
    diag.error("chip at " + sourcePath + ": missing 'id'");
    ok = false;
  }
  if (chip.qubits == 0) {
    diag.error("chip " + chip.id + ": qubits must be > 0");
    ok = false;
  }
  if (chip.nativeGates.empty()) {
    diag.error("chip " + chip.id + ": empty native_gates");
    ok = false;
  }
  for (const auto& g : chip.nativeGates) {
    if (knownNativeGates().find(g) == knownNativeGates().end()) {
      diag.error("chip " + chip.id + ": unknown gate '" + g +
                 "' in native_gates");
      ok = false;
    }
  }
  if (chip.decompose.twoQubitEntanglerCountMax != 3) {
    diag.error("chip " + chip.id +
               ": decompose.two_qubit.entangler_count_max must be 3 (KAK)");
    ok = false;
  }
  if (chip.decompose.oneQubit != DecomposeRecipe::OneQubit::EulerZyz) {
    diag.error("chip " + chip.id +
               ": decompose.one_qubit.recipe must be 'euler_zyz'");
    ok = false;
  }
  if (chip.decompose.twoQubit != DecomposeRecipe::TwoQubit::Kak) {
    diag.error("chip " + chip.id +
               ": decompose.two_qubit.recipe must be 'kak'");
    ok = false;
  }
  if (knownNativeGates().find(chip.decompose.twoQubitEntangler) ==
          knownNativeGates().end() &&
      !chip.decompose.twoQubitEntangler.empty()) {
    diag.error("chip " + chip.id + ": decompose entangler '" +
               chip.decompose.twoQubitEntangler + "' is not a known gate");
    ok = false;
  }
  return ok;
}

bool loadOneChip(const fs::path& file, const fs::path& topologiesDir,
                 ChipInfo& out, dialect::Diagnostics& diag) {
  try {
    Node n = Parser::parse_file(file.string());
    if (!n.isMap()) {
      diag.error("chip " + file.string() + ": top-level must be a map");
      return false;
    }
    if (n.has("id")) out.id = n.at("id").asString();
    if (n.has("provider")) out.provider = n.at("provider").asString();
    if (n.has("qubits")) out.qubits = static_cast<std::size_t>(n.at("qubits").asInt());
    if (n.has("native_gates") && n.at("native_gates").isArray()) {
      for (const auto& g : n.at("native_gates").asArray()) {
        out.nativeGates.push_back(g.asString());
      }
    }

    // coupling_map: { topology, size } or explicit { all_to_all }
    if (n.has("coupling_map") && n.at("coupling_map").isMap()) {
      const Node& cm = n.at("coupling_map");
      std::string topName;
      std::size_t sz = out.qubits;
      if (cm.has("topology")) topName = cm.at("topology").asString();
      if (cm.has("size") && cm.at("size").isNumber()) {
        sz = static_cast<std::size_t>(cm.at("size").asInt());
      }
      if (!topName.empty()) {
        ResolvedTopology rt = resolveTopology(topologiesDir, topName, sz);
        if (!rt.ok) {
          diag.error("chip " + (out.id.empty() ? file.string() : out.id) +
                     ": " + rt.error);
          return false;
        }
        out.allToAll = rt.allToAll;
        out.coupling = rt.edges;
        if (rt.qubits > 0 && rt.qubits != out.qubits) {
          diag.error("chip " + out.id + ": qubits=" +
                     std::to_string(out.qubits) +
                     " but topology " + topName + " has " +
                     std::to_string(rt.qubits) + " nodes");
          return false;
        }
      } else if (cm.has("all_to_all") && cm.at("all_to_all").asBool()) {
        out.allToAll = true;
      }
    }

    // supports
    if (n.has("supports") && n.at("supports").isMap()) {
      const Node& s = n.at("supports");
      if (s.has("mid_circuit_measure") &&
          s.at("mid_circuit_measure").isBool()) {
        out.supports.midCircuitMeasure = s.at("mid_circuit_measure").asBool();
      }
      if (s.has("feedforward") && s.at("feedforward").isString()) {
        const std::string& f = s.at("feedforward").asString();
        if (f == "none") out.supports.feedforward = CapabilityFlags::Feedforward::None;
        else if (f == "limited") out.supports.feedforward = CapabilityFlags::Feedforward::Limited;
        else if (f == "full") out.supports.feedforward = CapabilityFlags::Feedforward::Full;
      }
      if (s.has("reset") && s.at("reset").isBool()) {
        out.supports.reset = s.at("reset").asBool();
      }
    }

    // decompose
    if (n.has("decomposition") && n.at("decomposition").isMap()) {
      const Node& d = n.at("decomposition");
      if (d.has("one_qubit") && d.at("one_qubit").isMap()) {
        const Node& q1 = d.at("one_qubit");
        if (q1.has("recipe") && q1.at("recipe").asString() == "euler_zyz") {
          out.decompose.oneQubit = DecomposeRecipe::OneQubit::EulerZyz;
        }
        if (q1.has("rotation_gate")) {
          out.decompose.oneQubitRotationGate =
              q1.at("rotation_gate").asString();
        }
        if (q1.has("pi_2_gate") && q1.at("pi_2_gate").isString()) {
          out.decompose.oneQubitPi2Gate = q1.at("pi_2_gate").asString();
        }
      }
      if (d.has("two_qubit") && d.at("two_qubit").isMap()) {
        const Node& q2 = d.at("two_qubit");
        if (q2.has("recipe") && q2.at("recipe").asString() == "kak") {
          out.decompose.twoQubit = DecomposeRecipe::TwoQubit::Kak;
        }
        if (q2.has("entangler")) {
          out.decompose.twoQubitEntangler = q2.at("entangler").asString();
        }
        if (q2.has("entangler_count_max") &&
            q2.at("entangler_count_max").isNumber()) {
          out.decompose.twoQubitEntanglerCountMax =
              static_cast<int>(q2.at("entangler_count_max").asInt());
        }
      }
    }

    // pricing
    if (n.has("pricing") && n.at("pricing").isMap()) {
      const Node& p = n.at("pricing");
      if (p.has("per_shot_usd") && p.at("per_shot_usd").isNumber()) {
        out.pricePerShotUsd = p.at("per_shot_usd").asDouble();
      }
      if (p.has("per_minute_usd") && p.at("per_minute_usd").isNumber()) {
        out.pricePerMinuteUsd = p.at("per_minute_usd").asDouble();
      }
    }

    // calibration
    if (n.has("calibration") && n.at("calibration").isMap()) {
      const Node& c = n.at("calibration");
      if (c.has("source")) out.calibrationSource = c.at("source").asString();
      if (c.has("refresh")) out.calibrationRefresh = c.at("refresh").asString();
      if (c.has("store"))
        out.calibrationStore = expandTilde(c.at("store").asString());
    }

    if (n.has("notes") && n.at("notes").isString()) {
      out.notes = n.at("notes").asString();
    }

    // ID must match the file basename.
    std::string base = file.stem().string();
    if (!out.id.empty() && out.id != base) {
      diag.error("chip file '" + file.filename().string() +
                 "' declares id '" + out.id + "' (must match filename)");
      return false;
    }
    if (out.id.empty()) out.id = base;

    return validateChip(out, diag, file.string());
  } catch (const ParseError& pe) {
    std::ostringstream os;
    os << "yaml parse error in " << file.string() << " line "
       << pe.line << ": " << pe.message;
    diag.error(os.str());
    return false;
  } catch (const std::exception& e) {
    diag.error("chip " + file.string() + ": " + e.what());
    return false;
  }
}

}  // namespace

Registry Registry::load(const fs::path& root, dialect::Diagnostics& diag) {
  Registry reg;
  fs::path chipsDir = root / "chips";
  fs::path topDir = root / "topologies";
  if (!fs::is_directory(chipsDir)) {
    diag.error("registry: chips/ directory not found at " +
               chipsDir.string());
    return reg;
  }
  for (const auto& entry : fs::directory_iterator(chipsDir)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".yaml") continue;
    ChipInfo chip;
    if (loadOneChip(entry.path(), topDir, chip, diag)) {
      reg.chips_.emplace(chip.id, std::move(chip));
    }
  }
  return reg;
}

std::vector<std::string> Registry::ids() const {
  std::vector<std::string> v;
  v.reserve(chips_.size());
  for (const auto& [id, _] : chips_) v.push_back(id);
  std::sort(v.begin(), v.end());
  return v;
}

verify::TargetInfo Registry::targetInfo(const std::string& id) const {
  verify::TargetInfo t;
  auto it = chips_.find(id);
  if (it == chips_.end()) return t;  // generic fallback if unknown
  const ChipInfo& c = it->second;
  t.id = c.id;
  t.generic = false;
  t.nativeGates = c.nativeGates;
  t.allToAll = c.allToAll;
  t.coupling = c.coupling;
  t.qubitCount = c.qubits;
  t.midCircuitMeasure = c.supports.midCircuitMeasure;
  return t;
}

}  // namespace spinor::registry

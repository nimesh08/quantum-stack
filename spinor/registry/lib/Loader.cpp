// spinor/registry/lib/Loader.cpp
//
// Validates and assembles ChipInfo records from YAML files.

#include "spinor/registry/Registry.h"
#include "spinor/registry/ComponentTopology.h"

#include "Yaml.h"

#include <algorithm>
#include <cmath>
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
      "cx",   "cz",   "swap", "move",
      "ecr",  "ms",   "rzz", "rxx", "sx",  "sxdg", "phased_xz", "sqrt_iswap", "sqrt_iswap_inv", "syc", "iswap",
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
  bool directed = false;
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
    if (n.has("directed") && n.at("directed").isBool()) r.directed = n.at("directed").asBool();
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
      int lo = r.directed ? a : std::min(a, b);
      int hi = r.directed ? b : std::max(a, b);
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
  try { (void)computationalComponents(chip); }
  catch (const std::exception& error) { diag.error("chip " + chip.id + ": " + error.what()); ok=false; }
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
    if (n.has("qir_platform")) out.qirPlatform = n.at("qir_platform").asString();
    if (out.qirPlatform != "standard" && out.qirPlatform != "quantinuum-h2" && out.qirPlatform != "quantinuum-helios")
      throw std::runtime_error("Unsupported qir_platform: " + out.qirPlatform);
    if (n.has("vendor")) out.vendor = n.at("vendor").asString();
    if (n.has("readiness")) out.readiness = n.at("readiness").asString();
    if (n.has("readiness_reason")) out.readinessReason = n.at("readiness_reason").asString();
    if (n.has("capability_provenance")) out.capabilityProvenance = n.at("capability_provenance").asString();
    if (n.has("capability_verified")) out.capabilityVerified = n.at("capability_verified").asBool();
    if (n.has("directed_connectivity")) out.directedConnectivity = n.at("directed_connectivity").asBool();
    if (n.has("routes") && n.at("routes").isArray())
      for (const auto& value : n.at("routes").asArray()) out.routes.push_back(value.asString());
    if (n.has("formats") && n.at("formats").isArray())
      for (const auto& value : n.at("formats").asArray()) out.formats.push_back(value.asString());
    if (n.has("qubits")) out.qubits = static_cast<std::size_t>(n.at("qubits").asInt());
    auto componentIndex = [&](const Node& value) {
      if (!value.isInt() || value.asInt() < 0 || static_cast<std::size_t>(value.asInt()) >= out.qubits)
        throw std::runtime_error("physical component index must be an in-range integer");
      return static_cast<int>(value.asInt());
    };
    auto indices = [&](const char* key, auto& destination) {
      if (!n.has(key)) return;
      std::set<int> unique;
      for (const auto& value : n.at(key).asArray()) {
        int index = componentIndex(value);
        if (!unique.insert(index).second) throw std::runtime_error(std::string("duplicate component in ") + key);
        destination.push_back(index);
      }
    };
    indices("computational_qubits", out.computationalQubits);
    indices("resonator_qubits", out.resonatorQubits);
    if (n.has("available_qubits")) {
      out.availableQubits.emplace();
      indices("available_qubits", *out.availableQubits);
    }
    indices("unavailable_qubits", out.unavailableQubits);
    if (n.has("single_qubit_gate_loci")) {
      for (const auto& [gate, values] : n.at("single_qubit_gate_loci").asMap()) {
        if (!knownNativeGates().contains(gate) && gate != "measure" && gate != "reset")
          throw std::runtime_error("unknown single-qubit operation locus: " + gate);
        auto& destination = out.singleQubitGateLoci[gate];
        std::set<int> unique;
        for (const auto& value : values.asArray()) {
          int index = componentIndex(value);
          if (!unique.insert(index).second) throw std::runtime_error("duplicate single-qubit operation locus");
          destination.push_back(index);
        }
      }
    }
    auto operationLoci = [&](const char* key, auto& destination) {
      if (!n.has(key)) return;
      std::set<std::pair<int,int>> unique;
      for (const auto& value : n.at(key).asArray()) {
        const auto& row = value.asArray();
        if (row.size() != 2) throw std::runtime_error(std::string(key) + " requires two-component loci");
        auto pair = std::pair{componentIndex(row[0]), componentIndex(row[1])};
        if (pair.first == pair.second || !unique.insert(pair).second)
          throw std::runtime_error(std::string("duplicate or self locus in ") + key);
        destination.push_back(pair);
      }
    };
    operationLoci("move_loci", out.moveLoci);
    operationLoci("cz_loci", out.czLoci);
    if (n.has("two_qubit_gate_loci")) {
      const std::set<std::string> gates{"cx","cz","swap","move","ecr","ms","rxx","rzz",
        "iswap","sqrt_iswap","sqrt_iswap_inv","syc"};
      for (const auto& [gate, values] : n.at("two_qubit_gate_loci").asMap()) {
        if (!gates.contains(gate)) throw std::runtime_error("unknown two-qubit operation locus: " + gate);
        auto& destination=out.twoQubitGateLoci[gate];
        std::set<std::pair<int,int>> unique;
        for (const auto& value : values.asArray()) {
          const auto& row=value.asArray();
          if(row.size()!=2)throw std::runtime_error("two-qubit operation locus requires two components");
          const auto pair=std::pair{componentIndex(row[0]),componentIndex(row[1])};
          if(pair.first==pair.second||!unique.insert(pair).second)
            throw std::runtime_error("duplicate or self two-qubit operation locus");
          destination.push_back(pair);
        }
      }
    }
    if(n.has("placement")) {
      const auto& p=n.at("placement");
      if(p.has("strategy"))out.placement.strategy=p.at("strategy").asString();
      if(out.placement.strategy!="auto"&&out.placement.strategy!="uniform"&&out.placement.strategy!="heterogeneous")
        throw std::runtime_error("placement strategy must be auto, uniform or heterogeneous");
      auto count=[&](const char* key,std::size_t& destination,bool allowZero=false){
        if(!p.has(key))return;
        const auto& value=p.at(key);
        if(!value.isInt()||value.asInt()<0||(!allowZero&&value.asInt()==0))
          throw std::runtime_error(std::string("placement ")+key+" must be a positive integer");
        destination=static_cast<std::size_t>(value.asInt());
      };
      count("seed",out.placement.seed,true);count("max_states",out.placement.maxStates);
      count("beam_width",out.placement.beamWidth);count("max_layouts",out.placement.maxLayouts);
      if(p.has("max_swaps")){std::size_t value=0;count("max_swaps",value,true);out.placement.maxSwaps=value;}
    }
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
        out.directedConnectivity = out.directedConnectivity || rt.directed;
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

    if(n.has("capabilities")) {
      const auto& capabilities=n.at("capabilities");
      if(capabilities.has("features"))for(const auto& [feature,node]:capabilities.at("features").asMap()){
        const auto state=node.asString();
        if(state!="supported"&&state!="unsupported"&&state!="unknown")
          throw std::runtime_error("classical capability status must be supported, unsupported or unknown");
        out.classicalFeatures[feature]=state;
      }
      if(capabilities.has("integer_widths")){
        std::set<unsigned> widths;
        for(const auto& node:capabilities.at("integer_widths").asArray()){
          if(!node.isInt()||node.asInt()<1||node.asInt()>64||!widths.insert(unsigned(node.asInt())).second)
            throw std::runtime_error("classical integer widths must be unique integers in [1,64]");
          out.classicalIntegerWidths.push_back(unsigned(node.asInt()));
        }
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
      auto index = [&](const Node& value) {
        if (!value.isInt() || value.asInt() < 0 ||
            static_cast<unsigned long long>(value.asInt()) >= out.qubits)
          throw std::runtime_error("calibration qubit index must be an in-range integer");
        return static_cast<int>(value.asInt());
      };
      auto probability = [](const Node& value) {
        const double error = value.asDouble();
        if (!std::isfinite(error) || error < 0 || error >= 1)
          throw std::runtime_error("calibration error probability must be finite and in [0, 1)");
        return error;
      };
      auto nodes = [&](const std::string& key, auto& errors) {
        if (!c.has(key)) return;
        for (const auto& entry : c.at(key).asArray()) {
          if (!entry.isArray() || entry.asArray().size() != 2)
            throw std::runtime_error("calibration " + key + " entries must be [qubit, error]");
          const auto& row = entry.asArray();
          if (!errors.emplace(index(row[0]), probability(row[1])).second)
            throw std::runtime_error("duplicate calibration qubit in " + key);
        }
      };
      nodes("one_qubit_errors", out.calibrationOneQubitError);
      nodes("readout_errors", out.calibrationReadoutError);
      if (c.has("two_qubit_errors")) {
        for (const auto& entry : c.at("two_qubit_errors").asArray()) {
          if (!entry.isArray() || entry.asArray().size() != 3)
            throw std::runtime_error("calibration two_qubit_errors entries must be [source, target, error]");
          const auto& row = entry.asArray();
          const int a = index(row[0]), b = index(row[1]);
          if (a == b) throw std::runtime_error("calibration two-qubit edge must have distinct qubits");
          if (!out.allToAll &&
              std::find(out.coupling.begin(), out.coupling.end(), std::pair{a, b}) == out.coupling.end() &&
              (out.directedConnectivity ||
               std::find(out.coupling.begin(), out.coupling.end(), std::pair{b, a}) == out.coupling.end()))
            throw std::runtime_error("calibration two-qubit edge is absent from target connectivity");
          if (!out.calibrationTwoQubitError.emplace(std::pair{a, b}, probability(row[2])).second)
            throw std::runtime_error("duplicate two-qubit calibration edge");
        }
      }
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
  if (it == chips_.end()) throw std::invalid_argument("unknown target: " + id);
  const ChipInfo& c = it->second;
  t.id = c.id;
  t.generic = false;
  t.nativeGates = c.nativeGates;
  t.allToAll = c.allToAll;
  t.directedConnectivity = c.directedConnectivity;
  t.coupling = c.coupling;
  t.qubitCount = c.qubits;
  t.midCircuitMeasure = c.supports.midCircuitMeasure;
  return t;
}

}  // namespace spinor::registry

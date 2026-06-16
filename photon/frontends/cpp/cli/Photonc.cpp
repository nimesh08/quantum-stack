// photon/frontends/cpp/cli/Photonc.cpp
//
// `photonc-cxx` driver. Reads a small YAML build config, ingests the
// listed C++ source(s), lowers to Phonon, runs the engine, prints
// the resource estimate.

#include "photon/bindings/Engine.h"
#include "photon/cppfront/Ingest.h"
#include "photon/lang/Lower.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct BuildConfig {
  std::vector<std::string> sources;
  std::string entry = "main";
  std::string target = "generic";
  std::int64_t shots = 1024;
};

// Tiny line-based parser for the YAML subset used by the build config.
// Tolerates comments and indented mappings; rejects anything else.
BuildConfig parseConfig(std::string_view src) {
  BuildConfig cfg;
  std::string line;
  std::stringstream ss{std::string(src)};
  bool inBuild = false;
  while (std::getline(ss, line)) {
    auto hashPos = line.find('#');
    if (hashPos != std::string::npos) line.erase(hashPos);
    auto trim = [](std::string& s) {
      while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                            s.back() == '\r')) s.pop_back();
      std::size_t i = 0;
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
      s.erase(0, i);
    };
    std::string trimmed = line;
    trim(trimmed);
    if (trimmed.empty()) continue;
    if (trimmed == "build:") { inBuild = true; continue; }
    if (!inBuild) continue;
    auto colon = trimmed.find(':');
    if (colon == std::string::npos) continue;
    std::string key = trimmed.substr(0, colon);
    std::string val = trimmed.substr(colon + 1);
    trim(val);
    auto strip = [](std::string& s) {
      while (!s.empty() && (s.front() == '"' || s.front() == '\'' ||
                            s.front() == ' ')) s.erase(0, 1);
      while (!s.empty() && (s.back() == '"' || s.back() == '\'' ||
                            s.back() == ' ')) s.pop_back();
    };
    strip(val);
    if (key == "entry")  cfg.entry = val;
    else if (key == "target") cfg.target = val;
    else if (key == "shots")  cfg.shots = std::stoll(val);
    else if (key == "sources") {
      // Either inline `[a, b]` or block list on following lines.
      if (!val.empty() && val.front() == '[') {
        // Strip the brackets, split on comma.
        val.erase(0, 1);
        if (!val.empty() && val.back() == ']') val.pop_back();
        std::stringstream parts(val);
        std::string item;
        while (std::getline(parts, item, ',')) {
          strip(item);
          if (!item.empty()) cfg.sources.push_back(item);
        }
      }
    }
  }
  return cfg;
}

std::string readFile(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: photonc-cxx <build.yaml>\n";
    return 2;
  }
  auto cfg = parseConfig(readFile(argv[1]));
  if (cfg.sources.empty()) {
    std::cerr << "no sources in build config\n";
    return 1;
  }
  std::string allSrc;
  for (const auto& s : cfg.sources) allSrc += readFile(s) + "\n";
  auto ir = photon::cppfront::ingestCpp(allSrc, cfg.entry, cfg.target);
  if (!ir.module) {
    for (const auto& d : ir.diag.items()) std::cerr << d.message << "\n";
    return 1;
  }
  auto lr = photon::lang::lowerToPhonon(*ir.module);
  if (!lr.module) {
    for (const auto& d : lr.diag.items()) std::cerr << d.message << "\n";
    return 1;
  }
  auto cp = photon::bindings::CompiledProgram::fromPhononModule(
      std::move(*lr.module), cfg.target);
  if (!cp.ok()) {
    std::cerr << "engine error: " << cp.error() << "\n";
    return 1;
  }
  auto e = cp.estimate();
  std::cout << "compiled. target=" << cfg.target
            << " shots=" << cfg.shots
            << " num_qubits=" << e.num_qubits
            << " two_qubit_count=" << e.two_qubit_count
            << " depth=" << e.depth << "\n";
  return 0;
}

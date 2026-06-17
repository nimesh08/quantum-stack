// common/cli/lib/Providers.cpp

#include "qs/common/cli/Providers.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace qs::common::cli {

namespace {

// Tiny single-purpose YAML reader: locates the line `provider: <name>`
// inside a chip YAML and returns the value. We deliberately don't pull
// in a full YAML library here - the registry already has one, but this
// library is meant to stay dep-light. The chip YAMLs are written by
// our generator and have a predictable shape.
std::optional<std::string> readProviderField(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f) return std::nullopt;
  std::string line;
  while (std::getline(f, line)) {
    // Trim leading whitespace.
    auto i = line.find_first_not_of(" \t");
    if (i == std::string::npos) continue;
    auto trimmed = line.substr(i);
    static constexpr const char* prefix = "provider:";
    if (trimmed.size() < std::char_traits<char>::length(prefix)) continue;
    if (trimmed.compare(0, std::char_traits<char>::length(prefix), prefix) != 0) {
      continue;
    }
    auto rest = trimmed.substr(std::char_traits<char>::length(prefix));
    auto j = rest.find_first_not_of(" \t");
    if (j == std::string::npos) return std::nullopt;
    auto val = rest.substr(j);
    // Strip trailing whitespace / comments.
    auto h = val.find('#');
    if (h != std::string::npos) val = val.substr(0, h);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' ||
                            val.back() == '\r')) {
      val.pop_back();
    }
    if (!val.empty() && (val.front() == '"' || val.front() == '\'')) {
      val.erase(0, 1);
      if (!val.empty() && (val.back() == '"' || val.back() == '\'')) {
        val.pop_back();
      }
    }
    return val;
  }
  return std::nullopt;
}

}  // namespace

std::optional<std::string> providerForChip(std::string_view registryRoot,
                                           std::string_view chipId) {
  std::filesystem::path p =
      std::filesystem::path(std::string(registryRoot)) / "chips" /
      (std::string(chipId) + ".yaml");
  if (!std::filesystem::exists(p)) return std::nullopt;
  return readProviderField(p);
}

std::vector<ProviderInfo> allProviders() {
  return {
    {"ibm",      "IBM_QUANTUM_TOKEN",
     "https://quantum.cloud.ibm.com",
     "IBM Quantum (qiskit-ibm-runtime SamplerV2; verbatim by Rule 5)"},
    {"aws",      "AWS_DEFAULT_REGION",
     "https://braket.amazonaws.com",
     "AWS Braket (uses ~/.aws/credentials; supports IonQ / Rigetti / IQM / OQC)"},
    {"azure",    "AZURE_QUANTUM_RESOURCE_ID",
     "https://westus.quantum.azure.com",
     "Azure Quantum (azure-quantum SDK; supports Quantinuum, IonQ)"},
    {"local",    "",
     "",
     "In-process simulator (spinor C++ LocalProvider). No network."},
    {"qci",      "QCI_API_TOKEN",
     "",
     "Quantum Circuits Inc. Aqumen (cassette-only today; live URL not public)"},
    {"anyon",    "ANYON_API_TOKEN",
     "",
     "Anyon Technologies Yukon (cassette-only today)"},
    {"tii",      "QIBOLAB_HOST",
     "",
     "TII / Qibolab (point at a user-hosted Qibolab instance)"},
    {"alicebob", "ALICEBOB_API_KEY",
     "",
     "Alice and Bob Boson 4 cat-qubit (cassette-only today)"},
  };
}

const ProviderInfo* findProvider(std::string_view id) {
  static const auto kProviders = allProviders();
  for (const auto& p : kProviders) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

}  // namespace qs::common::cli

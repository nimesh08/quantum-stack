// common/cli/include/qs/common/cli/Providers.h
//
// chip-id -> provider mapping read from spinor/registry/chips/*.yaml.
// Used by all three CLI binaries for auto-derivation of --provider
// when the user only passed --target.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qs::common::cli {

struct ProviderInfo {
  std::string id;             // ibm, aws, azure, qci, anyon, tii, alicebob, local
  std::string env_var;        // canonical env var for the API key
  std::string default_url;    // documented default REST endpoint (if any)
  std::string description;    // one-line human description
};

// Look up the provider for a chip id by reading the chip YAML at
// `<registryRoot>/chips/<chipId>.yaml`. Returns std::nullopt if the
// chip is unknown or the YAML is malformed.
std::optional<std::string> providerForChip(std::string_view registryRoot,
                                           std::string_view chipId);

// All providers supported by spinor_submit (matches SUPPORTED_PROVIDERS
// in spinor/submit/python/spinor_submit/__init__.py).
std::vector<ProviderInfo> allProviders();

// Look up one provider's info by id. Returns nullptr for unknown ids.
const ProviderInfo* findProvider(std::string_view id);

}  // namespace qs::common::cli

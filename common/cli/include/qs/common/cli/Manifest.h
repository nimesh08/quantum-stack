// common/cli/include/qs/common/cli/Manifest.h
//
// Sidecar JSON written next to a compiled QASM3 artifact.
// Schema "heisenberg.manifest/v1".

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace qs::common::cli {

struct ManifestEstimate {
  std::size_t num_qubits      = 0;
  std::size_t two_qubit_count = 0;
  std::size_t depth           = 0;
};

struct Manifest {
  std::string source_lang;        // "photon" / "phonon" / "spinor"
  std::string source_path;        // bell.pho
  std::string source_sha256;      // hex digest of the source file's bytes
  std::string target;             // chip id
  std::string provider;           // provider id
  bool        verbatim   = true;  // Rule 5
  ManifestEstimate estimate;
  int         shots             = 1024;
  std::optional<double> cost_usd_estimate;
  std::string produced_by_binary;  // "photonc"
  std::string produced_by_version; // "0.1.0"
  std::string produced_by_git_sha; // "e3b1df8"
  std::string produced_at_utc;     // "2026-06-17T08:00:00Z"
};

// Compute SHA256 of a file's bytes. Hex-encoded, lowercase. On read
// failure returns the empty string.
std::string sha256OfFile(std::string_view path);

// Render the manifest to a JSON string (pretty-printed, two-space).
std::string toJson(const Manifest& m);

// ISO-8601 UTC timestamp for the current moment, e.g.
// "2026-06-17T08:00:00Z".
std::string nowUtcIso8601();

}  // namespace qs::common::cli

// common/cli/include/qs/common/cli/Submit.h
//
// C++ -> Python `python -m spinor_submit` subprocess shim.
//
// Why subprocess: vendor SDKs (qiskit-ibm-runtime, amazon-braket-sdk,
// azure-quantum) handle non-trivial auth (IBM Cloud IAM, AWS SigV4,
// Azure DefaultCredential). Re-implementing each in C++ would be
// 2-3 weeks per provider plus a permanent maintenance tail. The
// roughly 100 ms subprocess hop is invisible against any network
// round-trip to a quantum cloud.

#pragma once

#include "qs/common/cli/Flags.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace qs::common::cli {

struct SubmitRequest {
  std::string qasm_path;          // path to a .qasm3 file
  std::string chip;               // chip id
  std::string provider;           // provider id
  int         shots         = 1024;
  Mode        mode          = Mode::Cassette;
  std::string program_name  = "bell";   // selects the cassette JSON
  std::optional<std::string> api_key_file;
  bool                       api_key_stdin = false;
  std::optional<std::string> url;
  std::optional<std::string> region;
  std::optional<std::string> instance;
  std::map<std::string, std::string> extras;
  bool                       verbose = false;
};

struct SubmitResult {
  bool   ok      = false;
  int    exit_code = 0;
  std::string stdout_text;       // Python's stdout (the histogram)
  std::string stderr_text;       // Python's stderr (errors)
};

// Build the argv for the python subprocess. Exposed so unit tests can
// assert the shape without actually spawning Python.
std::vector<std::string> buildPythonArgv(const SubmitRequest& r);

// Spawn `python -m spinor_submit submit ...` and capture its output.
// Honours $QSTACK_PYTHON if set (default "python3").
SubmitResult runPython(const SubmitRequest& r);

}  // namespace qs::common::cli

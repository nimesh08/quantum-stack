// photon/frontends/cpp/include/photon/cppfront/Ingest.h
//
// C++ frontend ingester. Recognises a `[[photon::kernel]]`-marked
// function inside a `.cpp` source and produces a photon::lang::Module
// which then feeds the same lowering chain as the `.pho` parser.

#pragma once

#include "photon/lang/Module.h"

#include <optional>
#include <string>
#include <string_view>

namespace photon::cppfront {

struct IngestResult {
  std::optional<photon::lang::Module> module;
  photon::lang::Diagnostics diag;
};

// Parse `source`, find a function named `entry` decorated with
// `[[photon::kernel]]`, walk its body into a photon::lang::Module
// with `target` as the module target. Other functions in the file
// are ignored (matched by the LibTooling path; the self-hosted path
// matches them lazily).
IngestResult ingestCpp(std::string_view source,
                       std::string_view entry,
                       std::string_view target = "generic");

}  // namespace photon::cppfront

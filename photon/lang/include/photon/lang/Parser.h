// photon/lang/include/photon/lang/Parser.h
//
// Photon (.pho) recursive-descent parser. Produces a photon::lang::Module.

#pragma once

#include "photon/lang/Module.h"

#include <optional>
#include <string>
#include <string_view>

namespace photon::lang {

struct ParseResult {
  std::optional<Module> module;
  Diagnostics diag;
};

ParseResult parse(std::string_view text,
                  std::string_view filename = "<input>");

}  // namespace photon::lang

// spinor/parser/cpp/include/spinor/parser/Parser.h

#pragma once

#include "spinor/dialect/Spinor.h"

#include <optional>
#include <string>
#include <string_view>

namespace spinor::parser {

struct ParseResult {
  std::optional<dialect::Module> module;
  dialect::Diagnostics diag;
};

ParseResult parse(std::string_view text,
                  std::string_view filename = "<input>");

}  // namespace spinor::parser

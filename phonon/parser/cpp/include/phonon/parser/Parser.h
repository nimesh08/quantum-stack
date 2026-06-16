// phonon/parser/cpp/include/phonon/parser/Parser.h

#pragma once

#include "phonon/dialect/Phonon.h"

#include <optional>
#include <string>
#include <string_view>

namespace phonon::parser {

struct ParseResult {
  std::optional<dialect::Module> module;
  dialect::Diagnostics diag;
};

ParseResult parse(std::string_view text,
                  std::string_view filename = "<input>");

}  // namespace phonon::parser

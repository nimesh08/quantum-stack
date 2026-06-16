// photon/lang/include/photon/lang/Library.h
//
// photon.lib — the standard algorithm library. Each routine is an
// in-process expander that lowers a Photon library call into the
// equivalent Phonon ops on the right slots.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "photon/lang/Module.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace photon::lang {

struct ExpandCtx {
  std::unordered_map<std::string, std::vector<phonon::dialect::ValueId>>* qslots;
  phonon::dialect::Builder* builder;
  phonon::dialect::Location loc;
  // Compile-time evaluators borrowed from the lowerer.
  std::function<std::optional<std::int64_t>(const ExprPtr&)> foldInt;
  std::function<std::optional<double>(const ExprPtr&)> foldReal;
  // Diagnostics sink.
  Diagnostics* diag;
};

// Returns true if `name` matches a library routine and the expansion
// succeeded. False when `name` is not a library routine; in that case
// the lowerer falls back to its phonon.call placeholder. Diagnostics
// for malformed lib calls (wrong arity, unknown qreg, etc) are written
// to ctx.diag.
bool expandLibrary(const std::string& name,
                   const std::string& receiver,
                   const std::vector<ExprPtr>& args,
                   const Location& loc,
                   ExpandCtx& ctx);

// Returns true if `name` is one of the library routines.
bool isLibraryRoutine(const std::string& name);

}  // namespace photon::lang

// spinor/parser/qasm/include/spinor/parser/Qasm.h
//
// OpenQASM 3.1 subset importer. Decision D1 in
// docs/build/phaseA_decisions.md.
//
// Subset:
//   OPENQASM 3.x;             (header line ignored)
//   include "stdgates.inc";   (ignored)
//   qubit[N] q;               (== `qubit q[N]`)
//   bit[N]   c;               (== `bit c[N]`)
//   <gate>[(angles...)] q[i] [, q[j]] ;
//   c[i] = measure q[i];
//   reset q[i];
//   barrier [q[i] [, ...]] ;
// Anything outside this subset is rejected with a precise error
// directing the user to write Phonon for control flow.

#pragma once

#include "spinor/dialect/Spinor.h"

#include <optional>
#include <string>
#include <string_view>

namespace spinor::parser::qasm {

struct ImportResult {
  std::optional<dialect::Module> module;
  dialect::Diagnostics diag;
};

// Default target attribute is "generic"; callers may override.
ImportResult import(std::string_view text,
                    std::string_view filename = "<input>",
                    std::string target = "generic");

}  // namespace spinor::parser::qasm

// photon/bindings/include/photon/bindings/Engine.h
//
// Pure-C++ wrapper around the engine pipeline, used by the
// nanobind module. No Python types here.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "spinor/dialect/Spinor.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace photon::bindings {

struct ResourceEstimate {
  std::size_t num_qubits = 0;
  std::size_t depth = 0;
  std::size_t two_qubit_count = 0;
  std::size_t t_count = 0;
};

class CompiledProgram {
 public:
  CompiledProgram() = default;

  // Compile a Phonon-text source into a CompiledProgram. Failure
  // populates `error()` and leaves `ok()` false.
  static CompiledProgram fromPhononText(std::string_view phn,
                                        std::string_view target);

  // Compile an already-built Phonon module (skips the parse step).
  // This is the path the C++ frontend (M5) and the convergence
  // check (M6) take when they construct the IR programmatically
  // rather than re-parsing printed text.
  static CompiledProgram fromPhononModule(phonon::dialect::Module mod,
                                          std::string_view target);

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  std::string dumpPhonon() const;
  std::string dumpSpinor() const;
  ResourceEstimate estimate() const;

  const phonon::dialect::Module* phonon() const {
    return ok_ ? &phn_ : nullptr;
  }
  const spinor::dialect::Module* spinor() const {
    return spn_.has_value() ? &*spn_ : nullptr;
  }

 private:
  bool ok_ = false;
  std::string error_;
  phonon::dialect::Module phn_;
  std::optional<spinor::dialect::Module> spn_;
};

}  // namespace photon::bindings

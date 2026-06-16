// photon/lang/include/photon/lang/Lower.h
//
// Lower a Photon module to a Phonon dialect module.
//
// Photon is a thin OO surface; this lowering is mechanical:
// - QReg(N) becomes N phonon.alloc_qubit ops.
// - q.gate(args) becomes a phonon gate op on the slot.
// - q.measure_int() becomes per-slot phonon.measure + a classical
//   bit-pack expression.
// - for / if are passed through to phonon.for / phonon.if.
// - lib.<name>(args) becomes phonon.call "lib.<name>" args (M2
//   inlines the body).
//
// Crucially: this pass produces NO optimization. It hands the
// resulting Phonon module to the existing engine.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "photon/lang/Module.h"

#include <optional>

namespace photon::lang {

struct LowerResult {
  std::optional<phonon::dialect::Module> module;
  Diagnostics diag;
};

LowerResult lowerToPhonon(const Module& m);

}  // namespace photon::lang

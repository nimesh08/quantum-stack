// phonon/optimizer/include/phonon/optimizer/BorrowedPasses.h
//
// Owned C++ interfaces for borrowed optimizer pieces.
// See docs/build/phaseB/M6_optimizer_borrowed.md.

#pragma once

#include "phonon/dialect/Phonon.h"
#include "phonon/optimizer/Optimizer.h"

#include <memory>
#include <string>

namespace phonon::optimizer {

// Classical-logic synthesis: inline reversible-gate sequences
// for `phonon.call`s named "oracle.*". Default impl is a no-op
// (NullImpl); the Tweedledum-backed impl is compiled when
// SPINOR_HAVE_TWEEDLEDUM is set.
struct ISynthesizer {
  virtual ~ISynthesizer() = default;
  virtual Stats synthesize(dialect::Module& m) = 0;
  virtual std::string name() const = 0;
};

// ZX-calculus simplification: replace contiguous gate blocks with
// their PyZX-simplified equivalent. Default impl is a no-op
// (NullImpl); the PyZX impl uses subprocess + cassette.
struct IZxSimplifier {
  virtual ~IZxSimplifier() = default;
  virtual Stats simplify(dialect::Module& m) = 0;
  virtual std::string name() const = 0;
};

// Factories.
std::unique_ptr<ISynthesizer> makeNullSynthesizer();
std::unique_ptr<ISynthesizer> makeTweedledumSynthesizer();
std::unique_ptr<IZxSimplifier> makeNullZxSimplifier();
std::unique_ptr<IZxSimplifier> makePyZXSimplifier();

}  // namespace phonon::optimizer

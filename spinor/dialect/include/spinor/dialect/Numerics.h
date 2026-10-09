#pragma once
#include <limits>

namespace spinor::dialect {
// Unit-norm 2x2/4x4 entries: one four-term complex product sum takes at most
// roughly thirty elementary real floating operations. Use a few dozen ulps
// only for recognizing identities/local factors/special angles. This is a
// machine-roundoff allowance, not a user-authorized approximation budget or
// a forward-error bound for a whole circuit. Reconstruction/solver residual
// tolerances are separate and must not authorize removing an interaction.
inline constexpr double kMatrixRecognitionTolerance=
    32*std::numeric_limits<double>::epsilon();
}

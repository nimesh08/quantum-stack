# cmake/Versions.cmake
#
# Single source of truth for pinned third-party versions.
# RULE 4: re-verify upstream before bumping any of these.
#
# Last verified: 2026-06-16 (UTC).

# --- Toolchain ---------------------------------------------------------------
# LLVM/MLIR 22.1.x line. 22.1.7 is the latest stable as of 2026-06-02.
# 22.1.8 is scheduled for 2026-06-16 and will be the final 22.1.x release;
# bump to 22.1.8 once it is published, then 23.1.x once stabilised.
set(QSTACK_LLVM_VERSION   "22.1.7" CACHE STRING "Pinned LLVM/MLIR version")

# CMake minimum: 3.28 (pinned in top-level CMakeLists.txt).
# Latest stable CMake at pin date: 4.3.3 (2026-05-21). Any 3.28+ works.
set(QSTACK_CMAKE_MIN      "3.28"   CACHE STRING "Minimum CMake")

# C++ standard.
set(QSTACK_CXX_STANDARD   "17"     CACHE STRING "C++ standard for the engine")

# --- Math / linear algebra ---------------------------------------------------
# Eigen 5.0.x lives on GitLab (libeigen/eigen). 5.0.1 (2025-11-11) is current.
# RULE 4 corrections: NOT tuxfamily, NOT 3.4.
set(QSTACK_EIGEN_VERSION  "5.0.1"  CACHE STRING "Pinned Eigen version (GitLab)")

# --- Quantum stack pieces (pulled in by later phases) ------------------------
# Stim (Phase A simulator wrapper). Pin verified at use site.
set(QSTACK_STIM_VERSION   ""       CACHE STRING "Pinned Stim version (set in Phase A)")

# nanobind (Phase C bindings). Pin verified at use site.
set(QSTACK_NANOBIND_VERSION ""     CACHE STRING "Pinned nanobind version (set in Phase C)")

# Lark (Phase A throwaway parser prototype, Python-only).
set(QSTACK_LARK_VERSION   ""       CACHE STRING "Pinned Lark version (set in Phase A)")

# --- Provenance --------------------------------------------------------------
set(QSTACK_VERSIONS_VERIFIED_DATE "2026-06-16" CACHE STRING
    "Date the pinned versions in this file were last verified upstream.")

mark_as_advanced(
  QSTACK_LLVM_VERSION QSTACK_CMAKE_MIN QSTACK_CXX_STANDARD
  QSTACK_EIGEN_VERSION QSTACK_STIM_VERSION QSTACK_NANOBIND_VERSION
  QSTACK_LARK_VERSION QSTACK_VERSIONS_VERIFIED_DATE
)

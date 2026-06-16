# cmake/Versions.cmake
#
# Single source of truth for pinned third-party versions.
# RULE 4: re-verify upstream before bumping any of these.
#
# Last verified: 2026-06-16 (UTC).

# --- Toolchain ---------------------------------------------------------------
# LLVM/MLIR/Clang 22.1.x line. 22.1.8 (2026-06-16) is the final 22.1.x
# release; this pin is the Phase C bump (Phase A and Phase B were on
# 22.1.7 and remain green at 22.1.8 — see docs/build/phaseC_decisions.md
# entry D15). Next bump trigger: 23.1.0 once stabilised (23.x branches
# 2026-07-14, GA 2026-08-25).
set(QSTACK_LLVM_VERSION   "22.1.8" CACHE STRING "Pinned LLVM/Clang/MLIR version")

# CMake minimum: 3.28 (pinned in top-level CMakeLists.txt).
# Latest stable CMake at pin date: 4.3.3 (2026-05-21). Any 3.28+ works.
set(QSTACK_CMAKE_MIN      "3.28"   CACHE STRING "Minimum CMake")

# C++ standard.
set(QSTACK_CXX_STANDARD   "20"     CACHE STRING "C++ standard for the engine")

# --- Math / linear algebra ---------------------------------------------------
# Eigen 5.0.x lives on GitLab (libeigen/eigen). 5.0.1 (2025-11-11) is current.
# RULE 4 corrections: NOT tuxfamily, NOT 3.4.
set(QSTACK_EIGEN_VERSION  "5.0.1"  CACHE STRING "Pinned Eigen version (GitLab)")

# --- Quantum stack pieces (pulled in by later phases) ------------------------
# Stim (Phase A simulator wrapper). Pin verified at use site.
set(QSTACK_STIM_VERSION   ""       CACHE STRING "Pinned Stim version (set in Phase A)")

# nanobind (Phase C bindings). Pinned to 2.12.0 (BSD-3-Clause, released
# 2025-02-25). The deep-dive's floor was >= 2.4; latest stable at pin
# date is 2.12.0. See docs/build/phaseC_decisions.md entry D16.
set(QSTACK_NANOBIND_VERSION "2.12.0" CACHE STRING "Pinned nanobind version")

# CPython (Phase C frontend). Target 3.13; floor 3.12 (deep-dive's
# 3.12+ ask). The decorator rejects older interpreters at import time.
# See docs/build/phaseC_decisions.md entry D17.
set(QSTACK_PYTHON_TARGET   "3.13"  CACHE STRING "Target CPython version")
set(QSTACK_PYTHON_MIN      "3.12"  CACHE STRING "Minimum supported CPython version")

# Lark (Phase A throwaway parser prototype, Python-only).
set(QSTACK_LARK_VERSION   "1.3.1"  CACHE STRING "Pinned Lark version (set in Phase A M2; throwaway, deleted at M7)")

# --- Provenance --------------------------------------------------------------
set(QSTACK_VERSIONS_VERIFIED_DATE "2026-06-16" CACHE STRING
    "Date the pinned versions in this file were last verified upstream.")

mark_as_advanced(
  QSTACK_LLVM_VERSION QSTACK_CMAKE_MIN QSTACK_CXX_STANDARD
  QSTACK_EIGEN_VERSION QSTACK_STIM_VERSION QSTACK_NANOBIND_VERSION
  QSTACK_PYTHON_TARGET QSTACK_PYTHON_MIN
  QSTACK_LARK_VERSION QSTACK_VERSIONS_VERIFIED_DATE
)

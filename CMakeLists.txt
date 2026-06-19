# quantum-stack — top-level build
#
# Heisenberg Quantum Stack: Photon · Phonon · Spinor — a four-layer
# quantum compiler. One CMake project. Compiles the C++ engine
# (Spinor + Phonon + Photon core) against one pinned LLVM/MLIR.
# Platform services (FastAPI, React) live in platform/ but are not
# built by this CMake project.
#
# RULE 4 (re-verify and pin): all third-party versions are pinned
# in cmake/Versions.cmake. Update there only after re-verifying
# upstream.
#
# Author: Nimesh Cheedella <chnimesh0808@gmail.com>

cmake_minimum_required(VERSION 3.28)

project(quantum_stack
  VERSION       0.5.0
  DESCRIPTION   "Heisenberg Quantum Stack — Photon · Phonon · Spinor compiler"
  HOMEPAGE_URL  "https://nimesh08.github.io/quantum-stack/"
  LANGUAGES     C CXX
)

# Author + license metadata (read by `spinorc --version` and the
# Doxygen build).
set(QSTACK_AUTHOR  "Nimesh Cheedella")
set(QSTACK_LICENSE "Apache-2.0")

# ----- pinned versions (single source of truth) ------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Versions.cmake)

# ----- project-wide defaults --------------------------------------------------
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS         OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# All static libraries get -fPIC so they can be linked into the
# photon._engine shared module (Phase C M3).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ----- options ----------------------------------------------------------------
option(QSTACK_BUILD_SPINOR    "Build Phase A (spinor)"           ON)
option(QSTACK_BUILD_PHONON    "Build Phase B (phonon)"           ON)
option(QSTACK_BUILD_PHOTON    "Build Phase C (photon engine)"    ON)
option(QSTACK_ENABLE_TESTS    "Build unit tests"                 ON)
option(QSTACK_BUILD_PYTHON_WHEEL "Stage chip registry + CLI binaries into the photon wheel layout (set ON when scikit-build-core drives the build)" OFF)

# ----- LLVM / MLIR (pinned to ${QSTACK_LLVM_VERSION}) -------------------------
# The user must point CMake at an installed/built LLVM tree, e.g.:
#   cmake -B build -DLLVM_DIR=/path/to/llvm/lib/cmake/llvm \
#                  -DMLIR_DIR=/path/to/llvm/lib/cmake/mlir
find_package(LLVM ${QSTACK_LLVM_VERSION} EXACT CONFIG)
find_package(MLIR ${QSTACK_LLVM_VERSION} EXACT CONFIG)

if(LLVM_FOUND AND MLIR_FOUND)
  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} (pinned ${QSTACK_LLVM_VERSION})")
  message(STATUS "Found MLIR ${MLIR_PACKAGE_VERSION} (pinned ${QSTACK_LLVM_VERSION})")
  list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}" "${MLIR_CMAKE_DIR}")
  include(TableGen)
  include(AddLLVM)
  include(AddMLIR)
  include(HandleLLVMOptions)
  include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})
  set(QSTACK_HAVE_LLVM ON)
else()
  message(WARNING
    "LLVM/MLIR ${QSTACK_LLVM_VERSION} not found. "
    "Skeleton will configure but cannot build C++ engine targets. "
    "Pass -DLLVM_DIR=<...>/lib/cmake/llvm and -DMLIR_DIR=<...>/lib/cmake/mlir.")
  set(QSTACK_HAVE_LLVM OFF)
endif()

# ----- testing ----------------------------------------------------------------
if(QSTACK_ENABLE_TESTS)
  enable_testing()
endif()

# ----- phase subdirectories (bottom-up: see RULE 1) ---------------------------
if(QSTACK_BUILD_SPINOR)
  add_subdirectory(spinor)
endif()
if(QSTACK_BUILD_PHONON)
  add_subdirectory(phonon)
endif()
if(QSTACK_BUILD_PHOTON)
  add_subdirectory(photon)
endif()

# Shared CLI library (used by spinorc, phononc, photonc). Depends on
# spinor::registry only, so it's safe to add after Spinor.
if(QSTACK_BUILD_SPINOR)
  add_subdirectory(common/cli)
endif()

# platform/ is intentionally NOT a CMake subdirectory:
# it hosts FastAPI (Python) and the React playground, built by their own
# tooling. See platform/README.md.

message(STATUS "quantum-stack configured. LLVM/MLIR: ${QSTACK_LLVM_VERSION}.")

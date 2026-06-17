# photon/ — Phase C
#
# Photon is the user-facing layer: the OO language and photon.lib, the
# Python (@photon.kernel) and C++ (Clang LibTooling) frontends, and the
# nanobind C++<->Python bindings.
#
# RULE 1: do not start Phase C until Phase B's tests pass.
# RULE 3: the production engine is C++. Python appears only in (a) the
#         throwaway Lark prototype parser in spinor/parser, and (b) the
#         thin nanobind binding in photon/bindings.
#
# This file is the empty, compiling skeleton. Phase C adds real targets.

if(NOT QSTACK_HAVE_LLVM)
  message(STATUS "photon: LLVM/MLIR not available; building lang without MLIR backend.")
endif()

add_library(photon INTERFACE)
target_include_directories(photon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(photon INTERFACE phonon_dialect)

add_subdirectory(lang)
add_subdirectory(bindings)
add_subdirectory(frontends/cpp)
add_subdirectory(cli)

if(QSTACK_ENABLE_TESTS)
  add_subdirectory(tests/m1)
  add_subdirectory(tests/m2)
  add_subdirectory(tests/m3)
  add_subdirectory(tests/m4)
  add_subdirectory(tests/m5)
  add_subdirectory(tests/m6)
endif()

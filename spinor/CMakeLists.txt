# spinor/ — Phase A
#
# Spinor is the per-chip layer: MLIR dialect, parser, placement/routing/
# decomposition passes, registry, simulator wrappers, emitters, submitters.
#
# RULE 2: Spinor specializes a program to ONE chip. The heavy optimizer
# lives in Phonon, never here.
#
# This file is the empty, compiling skeleton. Phase A adds real targets.

if(NOT QSTACK_HAVE_LLVM)
  message(STATUS "spinor: LLVM/MLIR not available; skeleton only.")
  return()
endif()

add_library(spinor INTERFACE)
target_include_directories(spinor INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Phase A subdirectories will be wired here as their CMakeLists.txt are added.
# add_subdirectory(dialect)
# add_subdirectory(parser)
# add_subdirectory(passes)
# add_subdirectory(registry)
# add_subdirectory(sim)
# add_subdirectory(emit)
# add_subdirectory(submit)

if(QSTACK_ENABLE_TESTS)
  # add_subdirectory(tests)
endif()

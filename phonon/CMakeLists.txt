# phonon/ — Phase B
#
# Phonon is the chip-independent layer: its dialect extends Spinor's, and
# this is where the heavy optimizer lives (cancellation, merging, reorder,
# ZX, synth, schedule).
#
# RULE 2: optimization lives here, not in Spinor.
# RULE 1: do not start Phase B until Phase A's tests pass.
#
# This file is the empty, compiling skeleton. Phase B adds real targets.

if(NOT QSTACK_HAVE_LLVM)
  message(STATUS "phonon: LLVM/MLIR not available; skeleton only.")
  return()
endif()

add_library(phonon INTERFACE)
target_include_directories(phonon INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# target_link_libraries(phonon INTERFACE spinor)  # phonon's dialect extends spinor's

# Phase B subdirectories will be wired here as their CMakeLists.txt are added.
# add_subdirectory(dialect)
# add_subdirectory(lower)
# add_subdirectory(types)
# add_subdirectory(optimizer)

if(QSTACK_ENABLE_TESTS)
  # add_subdirectory(tests)
endif()

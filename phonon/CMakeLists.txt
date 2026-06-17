# phonon/ — Phase B
#
# Phonon is the chip-independent layer: its dialect extends Spinor's, and
# this is where the heavy optimizer lives (cancellation, merging, reorder,
# ZX, synth, schedule).
#
# RULE 2: optimization lives here, not in Spinor.
# RULE 1: Phase A must be green before Phase B work proceeds.
#
# Layered like spinor/: the in-tree backend builds everywhere; the
# MLIR-backed bridge builds only when QSTACK_HAVE_LLVM is ON.

# Always available (no LLVM dependency).
add_subdirectory(dialect)
add_subdirectory(parser)
add_subdirectory(types)
add_subdirectory(lower)
add_subdirectory(optimizer)
add_subdirectory(cli)

if(QSTACK_ENABLE_TESTS)
  add_subdirectory(tests/m1)
  add_subdirectory(tests/m2)
  add_subdirectory(tests/m3)
  add_subdirectory(tests/m4)
  add_subdirectory(tests/m5)
  add_subdirectory(tests/m6)
  add_subdirectory(tests/m7)
  # Subsequent milestone test directories are wired here as they land.
endif()

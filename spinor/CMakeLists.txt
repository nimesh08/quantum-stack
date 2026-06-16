# spinor/ — Phase A
#
# Spinor is the per-chip layer: MLIR dialect, parser, placement/routing/
# decomposition passes, registry, simulator wrappers, emitters, submitters.
#
# RULE 2: Spinor specializes a program to ONE chip. The heavy optimizer
# lives in Phonon, never here.
#
# Layered build (Phase A decision D4):
# - The in-tree dialect / parser / verifier / passes build everywhere.
# - The MLIR-backed bridge builds only when QSTACK_HAVE_LLVM is ON.

# Always available (no LLVM dependency):
add_subdirectory(dialect)
add_subdirectory(verify)
add_subdirectory(registry)
add_subdirectory(passes)
add_subdirectory(parser/cpp)
add_subdirectory(parser/qasm)
add_subdirectory(sim)
add_subdirectory(emit)
add_subdirectory(submit)
add_subdirectory(cli)

if(QSTACK_ENABLE_TESTS)
  add_subdirectory(tests/m1)
  add_subdirectory(tests/m3)
  add_subdirectory(tests/m4)
  add_subdirectory(tests/m5)
  add_subdirectory(tests/m6)
  add_subdirectory(tests/m7)
  add_subdirectory(tests/m11)
  add_subdirectory(tests/m8)
  add_subdirectory(tests/m9)
  add_subdirectory(tests/m10)
endif()

# Subsequent milestone subdirectories are wired here as their
# CMakeLists.txt are added:
# add_subdirectory(parser)
# add_subdirectory(verify)
# add_subdirectory(registry)
# add_subdirectory(passes)
# add_subdirectory(sim)
# add_subdirectory(emit)
# add_subdirectory(submit)

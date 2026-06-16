# spinor/dialect/CMakeLists.txt
#
# M1 — Spinor dialect.
#
# The in-tree backend is always built. The MLIR-backed backend
# (lib/mlir/SpinorMLIR.cpp + the TableGen sources in td/) builds
# only when QSTACK_HAVE_LLVM is ON.

# ---- in-tree backend (always available) -------------------------------------

add_library(spinor_dialect STATIC
  lib/Spinor.cpp
  lib/Print.cpp
  lib/Parse.cpp
  lib/Verify.cpp
)
target_include_directories(spinor_dialect
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_features(spinor_dialect PUBLIC cxx_std_20)

# Public alias used by the rest of Phase A.
add_library(spinor::dialect ALIAS spinor_dialect)

# ---- MLIR-backed backend (LLVM/MLIR 22.1.x) ---------------------------------

if(QSTACK_HAVE_LLVM)
  set(LLVM_TARGET_DEFINITIONS td/SpinorOps.td)
  mlir_tablegen(SpinorOps.h.inc -gen-op-decls)
  mlir_tablegen(SpinorOps.cpp.inc -gen-op-defs)
  set(LLVM_TARGET_DEFINITIONS td/SpinorTypes.td)
  mlir_tablegen(SpinorTypes.h.inc -gen-typedef-decls)
  mlir_tablegen(SpinorTypes.cpp.inc -gen-typedef-defs)
  set(LLVM_TARGET_DEFINITIONS td/SpinorDialect.td)
  mlir_tablegen(SpinorDialect.h.inc -gen-dialect-decls)
  mlir_tablegen(SpinorDialect.cpp.inc -gen-dialect-defs)
  add_public_tablegen_target(SpinorDialectIncGen)

  add_library(spinor_dialect_mlir STATIC
    mlir/SpinorMLIR.cpp
  )
  target_include_directories(spinor_dialect_mlir
    PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/include
      ${CMAKE_CURRENT_BINARY_DIR}
  )
  target_compile_features(spinor_dialect_mlir PUBLIC cxx_std_20)
  add_dependencies(spinor_dialect_mlir SpinorDialectIncGen)
  target_link_libraries(spinor_dialect_mlir
    PUBLIC
      spinor_dialect
      MLIRIR
      MLIRSupport
      MLIRDialect
  )
  add_library(spinor::dialect_mlir ALIAS spinor_dialect_mlir)
endif()

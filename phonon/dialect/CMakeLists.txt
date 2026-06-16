# phonon/dialect/CMakeLists.txt
#
# Phonon dialect (Phase B M1).
#
# Layered like Phase A:
# - The in-tree backend builds always (no LLVM dependency).
# - The MLIR-backed bridge builds only when QSTACK_HAVE_LLVM is ON
#   (deferred to a follow-on milestone; the .td sources are committed
#   here so the bridge can pick them up.)

add_library(phonon_dialect STATIC
  lib/Phonon.cpp
  lib/Builder.cpp
  lib/Print.cpp
  lib/Parse.cpp
  lib/Verify.cpp
)
add_library(phonon::dialect ALIAS phonon_dialect)

target_include_directories(phonon_dialect
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phonon_dialect PUBLIC spinor::dialect)
target_compile_features(phonon_dialect PUBLIC cxx_std_20)

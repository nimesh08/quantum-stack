# spinor/verify/CMakeLists.txt
#
# M3 — Spinor semantic verifier (W1-W6 + type check).

add_library(spinor_verify STATIC
  lib/Verifier.cpp
)
target_include_directories(spinor_verify
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_verify PUBLIC spinor::dialect)
target_compile_features(spinor_verify PUBLIC cxx_std_20)

add_library(spinor::verify ALIAS spinor_verify)

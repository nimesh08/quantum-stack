# spinor/sim/CMakeLists.txt
add_library(spinor_sim STATIC
  lib/Simulator.cpp
)
target_include_directories(spinor_sim
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_sim
  PUBLIC
    spinor::dialect
    spinor::registry
    spinor::passes  # for Complex2x2.h
)
target_compile_features(spinor_sim PUBLIC cxx_std_20)
add_library(spinor::sim ALIAS spinor_sim)

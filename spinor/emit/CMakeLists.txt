# spinor/emit/CMakeLists.txt
add_library(spinor_emit STATIC
  lib/Qasm3.cpp
  lib/Qir.cpp
  lib/Quil.cpp
)
target_include_directories(spinor_emit
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_emit PUBLIC spinor::dialect spinor::registry)
target_compile_features(spinor_emit PUBLIC cxx_std_20)
add_library(spinor::emit ALIAS spinor_emit)

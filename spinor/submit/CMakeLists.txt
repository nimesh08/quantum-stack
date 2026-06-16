# spinor/submit/CMakeLists.txt
add_library(spinor_submit STATIC
  lib/LocalProvider.cpp
)
target_include_directories(spinor_submit
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_submit
  PUBLIC spinor::dialect spinor::registry spinor::sim
)
target_compile_features(spinor_submit PUBLIC cxx_std_20)
add_library(spinor::submit ALIAS spinor_submit)

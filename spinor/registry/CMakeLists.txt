# spinor/registry/CMakeLists.txt

add_library(spinor_registry STATIC
  lib/Yaml.cpp
  lib/Loader.cpp
)
target_include_directories(spinor_registry
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
target_link_libraries(spinor_registry
  PUBLIC
    spinor::dialect
    spinor::verify
)
target_compile_features(spinor_registry PUBLIC cxx_std_20)

add_library(spinor::registry ALIAS spinor_registry)

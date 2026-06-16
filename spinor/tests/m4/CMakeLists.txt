# spinor/tests/m4/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

add_executable(spinor_m4_yaml_test yaml_test.cpp)
target_include_directories(spinor_m4_yaml_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
  ${CMAKE_SOURCE_DIR}/spinor
)
target_link_libraries(spinor_m4_yaml_test PRIVATE spinor::registry)
target_compile_features(spinor_m4_yaml_test PRIVATE cxx_std_20)
add_test(NAME spinor_m4_yaml_test COMMAND spinor_m4_yaml_test)
set_tests_properties(spinor_m4_yaml_test PROPERTIES LABELS "M4")

add_executable(spinor_m4_loader_test loader_test.cpp)
target_include_directories(spinor_m4_loader_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m4_loader_test PRIVATE spinor::registry)
target_compile_features(spinor_m4_loader_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m4_loader_test PRIVATE
  SPINOR_REGISTRY_ROOT="${CMAKE_SOURCE_DIR}/spinor/registry"
)
add_test(NAME spinor_m4_loader_test COMMAND spinor_m4_loader_test)
set_tests_properties(spinor_m4_loader_test PROPERTIES LABELS "M4")

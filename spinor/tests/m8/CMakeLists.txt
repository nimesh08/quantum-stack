# spinor/tests/m8/CMakeLists.txt
if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()
add_executable(spinor_m8_test m8_test.cpp)
target_include_directories(spinor_m8_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m8_test PRIVATE
  spinor::sim
  spinor::passes
  spinor::registry
  spinor::parser
  spinor::dialect
)
target_compile_features(spinor_m8_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m8_test PRIVATE
  SPINOR_M8_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
  SPINOR_REGISTRY_ROOT="${CMAKE_SOURCE_DIR}/spinor/registry"
)
add_test(NAME spinor_m8_test COMMAND spinor_m8_test)
set_tests_properties(spinor_m8_test PROPERTIES LABELS "M8")

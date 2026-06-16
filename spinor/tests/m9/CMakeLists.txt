# spinor/tests/m9/CMakeLists.txt
if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()
add_executable(spinor_m9_test m9_test.cpp)
target_include_directories(spinor_m9_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m9_test PRIVATE
  spinor::emit
  spinor::passes
  spinor::registry
  spinor::parser
  spinor::parser_qasm
  spinor::dialect
)
target_compile_features(spinor_m9_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m9_test PRIVATE
  SPINOR_M9_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
)
add_test(NAME spinor_m9_test COMMAND spinor_m9_test)
set_tests_properties(spinor_m9_test PROPERTIES LABELS "M9")

# spinor/tests/m11/CMakeLists.txt
if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()
add_executable(spinor_m11_test m11_test.cpp)
target_include_directories(spinor_m11_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m11_test PRIVATE
  spinor::parser_qasm
  spinor::parser
  spinor::dialect
)
target_compile_features(spinor_m11_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m11_test PRIVATE
  SPINOR_M11_QASM_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/m11/qasm_corpus"
  SPINOR_M11_SPN_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
)
add_test(NAME spinor_m11_test COMMAND spinor_m11_test)
set_tests_properties(spinor_m11_test PROPERTIES LABELS "M11")

# spinor/tests/m7/CMakeLists.txt
if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()
add_executable(spinor_m7_test m7_test.cpp)
target_include_directories(spinor_m7_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m7_test PRIVATE
  spinor::parser
  spinor::dialect
)
target_compile_features(spinor_m7_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m7_test PRIVATE
  SPINOR_M7_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
  SPINOR_M7_GOLDENS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/m7/goldens"
)
add_test(NAME spinor_m7_test COMMAND spinor_m7_test)
set_tests_properties(spinor_m7_test PROPERTIES LABELS "M7")

add_executable(spinor_m7_inspector inspector.cpp)
target_link_libraries(spinor_m7_inspector PRIVATE spinor::parser spinor::dialect)
target_compile_features(spinor_m7_inspector PRIVATE cxx_std_20)

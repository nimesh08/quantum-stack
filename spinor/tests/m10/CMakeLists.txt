# spinor/tests/m10/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

# C++ LocalProvider tests.
add_executable(spinor_m10_test m10_test.cpp)
target_include_directories(spinor_m10_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m10_test PRIVATE
  spinor::submit
  spinor::sim
  spinor::registry
  spinor::parser
  spinor::dialect
)
target_compile_features(spinor_m10_test PRIVATE cxx_std_20)
target_compile_definitions(spinor_m10_test PRIVATE
  SPINOR_M10_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
)
add_test(NAME spinor_m10_test COMMAND spinor_m10_test)
set_tests_properties(spinor_m10_test PROPERTIES LABELS "M10")

# Python adapter tests (cassette mode).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME spinor_m10_python_pytest
    COMMAND ${Python3_EXECUTABLE} -m pytest
            ${CMAKE_SOURCE_DIR}/spinor/submit/python/tests
            -v
  )
  set_tests_properties(spinor_m10_python_pytest
                       PROPERTIES LABELS "M10;python")
endif()

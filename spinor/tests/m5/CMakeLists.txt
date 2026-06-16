# spinor/tests/m5/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

add_executable(spinor_m5_test m5_test.cpp)
target_include_directories(spinor_m5_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m5_test PRIVATE
  spinor::passes
  spinor::registry
  spinor::dialect
)
target_compile_features(spinor_m5_test PRIVATE cxx_std_20)
add_test(NAME spinor_m5_test COMMAND spinor_m5_test)
set_tests_properties(spinor_m5_test PROPERTIES LABELS "M5;TIMEOUT:30")

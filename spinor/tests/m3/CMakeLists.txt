# spinor/tests/m3/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

add_executable(spinor_m3_verifier_test verifier_test.cpp)
target_include_directories(spinor_m3_verifier_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../support
)
target_link_libraries(spinor_m3_verifier_test PRIVATE
  spinor::dialect
  spinor::verify
)
target_compile_features(spinor_m3_verifier_test PRIVATE cxx_std_20)
add_test(NAME spinor_m3_verifier_test COMMAND spinor_m3_verifier_test)
set_tests_properties(spinor_m3_verifier_test PROPERTIES LABELS "M3")

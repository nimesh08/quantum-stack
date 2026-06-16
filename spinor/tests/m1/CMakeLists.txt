# spinor/tests/m1/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

# Helper: a self-contained C++ test using the in-tree backend.
function(spinor_add_m1_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../support
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(${name} PRIVATE spinor::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    SPINOR_TEST_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "M1")
endfunction()

spinor_add_m1_test(spinor_m1_builder_test    builder_test.cpp)
spinor_add_m1_test(spinor_m1_round_trip_test round_trip_test.cpp)
spinor_add_m1_test(spinor_m1_verify_test     verify_test.cpp)
spinor_add_m1_test(spinor_m1_print_golden_test print_golden_test.cpp)

# Also a small inspector binary used during development to generate /
# update the golden files.
add_executable(spinor_m1_inspector inspector.cpp)
target_link_libraries(spinor_m1_inspector PRIVATE spinor::dialect)
target_compile_features(spinor_m1_inspector PRIVATE cxx_std_20)

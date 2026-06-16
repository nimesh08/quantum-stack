# phonon/tests/m1/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(phonon_add_m1_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(${name} PRIVATE phonon::dialect spinor::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    SPINOR_TEST_GOLDENS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/goldens"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "phonon_M1")
endfunction()

phonon_add_m1_test(phonon_m1_builder_test     builder_test.cpp)
phonon_add_m1_test(phonon_m1_round_trip_test  round_trip_test.cpp)
phonon_add_m1_test(phonon_m1_verify_test      verify_test.cpp)
phonon_add_m1_test(phonon_m1_print_golden_test print_golden_test.cpp)

# phonon/tests/m6/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(phonon_add_m6_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
  )
  target_link_libraries(${name} PRIVATE phonon::optimizer phonon::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    PHONON_CASSETTE_DIR="${CMAKE_SOURCE_DIR}/phonon/optimizer/cassettes"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "phonon_M6")
endfunction()

phonon_add_m6_test(phonon_m6_borrowed_test borrowed_test.cpp)

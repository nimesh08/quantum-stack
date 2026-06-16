# phonon/tests/m3/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(phonon_add_m3_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
  )
  target_link_libraries(${name} PRIVATE phonon::types phonon::parser phonon::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    PHONON_CORPUS_DIR="${CMAKE_SOURCE_DIR}/phonon/tests/corpus"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "phonon_M3")
endfunction()

phonon_add_m3_test(phonon_m3_linear_test linear_test.cpp)
phonon_add_m3_test(phonon_m3_corpus_test corpus_test.cpp)

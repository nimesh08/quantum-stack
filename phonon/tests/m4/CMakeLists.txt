# phonon/tests/m4/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(phonon_add_m4_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
  )
  target_link_libraries(${name} PRIVATE
    phonon::lower phonon::parser phonon::dialect
    spinor::dialect spinor::verify)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    SPINOR_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
    PHONON_CORPUS_DIR="${CMAKE_SOURCE_DIR}/phonon/tests/corpus"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "phonon_M4")
endfunction()

phonon_add_m4_test(phonon_m4_lower_test    lower_test.cpp)
phonon_add_m4_test(phonon_m4_pipeline_test pipeline_test.cpp)

# phonon/tests/m5/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(phonon_add_m5_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
  )
  target_link_libraries(${name} PRIVATE
    phonon::optimizer phonon::parser phonon::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    SPINOR_CORPUS_DIR="${CMAKE_SOURCE_DIR}/spinor/tests/corpus"
    PHONON_CORPUS_DIR="${CMAKE_SOURCE_DIR}/phonon/tests/corpus"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "phonon_M5")
endfunction()

phonon_add_m5_test(phonon_m5_cancel_test   cancel_test.cpp)
phonon_add_m5_test(phonon_m5_merge_test    merge_test.cpp)
phonon_add_m5_test(phonon_m5_pipeline_test pipeline_test.cpp)

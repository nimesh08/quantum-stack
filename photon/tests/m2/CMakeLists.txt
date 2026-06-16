# photon/tests/m2/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

function(photon_add_m2_test name source)
  add_executable(${name} ${source})
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/spinor/tests/support
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  target_link_libraries(${name} PRIVATE photon_lang phonon::dialect spinor::dialect)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_definitions(${name} PRIVATE
    PHOTON_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/photon/tests/corpus"
  )
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "photon_M2")
endfunction()

photon_add_m2_test(photon_m2_lib_test  lib_test.cpp)

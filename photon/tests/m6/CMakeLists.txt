# photon/tests/m6/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

# C++ side of the convergence check.
add_executable(photon_m6_convergence_test convergence_test.cpp)
target_include_directories(photon_m6_convergence_test PRIVATE
  ${CMAKE_SOURCE_DIR}/spinor/tests/support
)
target_link_libraries(photon_m6_convergence_test PRIVATE
  photon_engine_cpp
  photon_cppfront
  photon_lang
  phonon::dialect
  spinor::dialect)
target_compile_features(photon_m6_convergence_test PRIVATE cxx_std_20)
target_compile_definitions(photon_m6_convergence_test PRIVATE
  PHOTON_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/photon/tests/corpus")
add_test(NAME photon_m6_convergence_test COMMAND photon_m6_convergence_test)
set_tests_properties(photon_m6_convergence_test PROPERTIES LABELS "photon_M6")

# Python side.
find_package(Python COMPONENTS Interpreter REQUIRED)
add_test(
  NAME photon_m6_three_door_test
  COMMAND ${Python_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/three_door_test.py
)
set_tests_properties(photon_m6_three_door_test PROPERTIES
  LABELS "photon_M6"
  ENVIRONMENT
    "PHOTON_PY_PKG_DIR=${CMAKE_BINARY_DIR}/photon/bindings/python;PHOTON_TEST_CORPUS_DIR=${CMAKE_SOURCE_DIR}/photon/tests/corpus"
)

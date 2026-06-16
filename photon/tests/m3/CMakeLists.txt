# photon/tests/m3/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

# (a) Pure-C++ engine wrapper tests.
add_executable(photon_m3_cpp_engine_test cpp_engine_test.cpp)
target_include_directories(photon_m3_cpp_engine_test PRIVATE
  ${CMAKE_SOURCE_DIR}/spinor/tests/support
)
target_link_libraries(photon_m3_cpp_engine_test PRIVATE
  photon_engine_cpp phonon::dialect spinor::dialect)
target_compile_features(photon_m3_cpp_engine_test PRIVATE cxx_std_20)
add_test(NAME photon_m3_cpp_engine_test COMMAND photon_m3_cpp_engine_test)
set_tests_properties(photon_m3_cpp_engine_test PROPERTIES LABELS "photon_M3")

# (b) Python round-trip tests, only when the extension is built.
if(QSTACK_HAVE_NANOBIND)
  find_package(Python COMPONENTS Interpreter REQUIRED)
  add_test(
    NAME photon_m3_py_engine_test
    COMMAND ${Python_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/py_engine_test.py
  )
  set_tests_properties(photon_m3_py_engine_test PROPERTIES
    LABELS "photon_M3"
    ENVIRONMENT "PHOTON_PY_PKG_DIR=${CMAKE_BINARY_DIR}/photon/bindings/python"
  )
endif()

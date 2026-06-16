# photon/tests/m4/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

# Translator tests run without the C++ engine (pure Python).
find_package(Python COMPONENTS Interpreter REQUIRED)

add_test(
  NAME photon_m4_translator_test
  COMMAND ${Python_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/translator_test.py
)
set_tests_properties(photon_m4_translator_test PROPERTIES
  LABELS "photon_M4"
  ENVIRONMENT "PHOTON_PY_PKG_DIR=${CMAKE_BINARY_DIR}/photon/bindings/python"
)

# Decorator + e2e tests need the engine (skip themselves otherwise).
add_test(
  NAME photon_m4_decorator_test
  COMMAND ${Python_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/decorator_test.py
)
set_tests_properties(photon_m4_decorator_test PROPERTIES
  LABELS "photon_M4"
  ENVIRONMENT "PHOTON_PY_PKG_DIR=${CMAKE_BINARY_DIR}/photon/bindings/python"
)

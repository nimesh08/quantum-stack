# photon/tests/m5/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

add_executable(photon_m5_ingest_test ingest_test.cpp)
target_include_directories(photon_m5_ingest_test PRIVATE
  ${CMAKE_SOURCE_DIR}/spinor/tests/support
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(photon_m5_ingest_test PRIVATE
  photon_cppfront photon_lang phonon::dialect spinor::dialect)
target_compile_features(photon_m5_ingest_test PRIVATE cxx_std_20)
target_compile_definitions(photon_m5_ingest_test PRIVATE
  PHOTON_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/photon/tests/corpus")
add_test(NAME photon_m5_ingest_test COMMAND photon_m5_ingest_test)
set_tests_properties(photon_m5_ingest_test PROPERTIES LABELS "photon_M5")

add_executable(photon_m5_photonc_cli_test photonc_cli_test.cpp)
target_include_directories(photon_m5_photonc_cli_test PRIVATE
  ${CMAKE_SOURCE_DIR}/spinor/tests/support
)
target_compile_features(photon_m5_photonc_cli_test PRIVATE cxx_std_20)
target_compile_definitions(photon_m5_photonc_cli_test PRIVATE
  PHOTON_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/photon/tests/corpus"
  PHOTONC_CLI="$<TARGET_FILE:photonc-cxx>")
add_dependencies(photon_m5_photonc_cli_test photonc-cxx)
add_test(NAME photon_m5_photonc_cli_test COMMAND photon_m5_photonc_cli_test)
set_tests_properties(photon_m5_photonc_cli_test PROPERTIES LABELS "photon_M5")

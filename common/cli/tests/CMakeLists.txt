# common/cli/tests/CMakeLists.txt

if(NOT QSTACK_ENABLE_TESTS)
  return()
endif()

add_executable(qs_common_cli_test flags_test.cpp)
target_include_directories(qs_common_cli_test PRIVATE
  ${CMAKE_SOURCE_DIR}/spinor/tests/support
)
target_link_libraries(qs_common_cli_test PRIVATE qs::common_cli)
target_compile_features(qs_common_cli_test PRIVATE cxx_std_20)
target_compile_definitions(qs_common_cli_test PRIVATE
  SPINOR_REGISTRY_ROOT="${CMAKE_SOURCE_DIR}/spinor/registry"
)
add_test(NAME qs_common_cli_test COMMAND qs_common_cli_test)
set_tests_properties(qs_common_cli_test PROPERTIES LABELS "common-cli")

add_executable(qs_process_helper process_helper.cpp)
add_executable(qs_process_test process_test.cpp)
target_include_directories(qs_process_test PRIVATE ${CMAKE_SOURCE_DIR}/spinor/tests/support)
target_link_libraries(qs_process_test PRIVATE qs::common_cli)
target_compile_definitions(qs_process_test PRIVATE PROCESS_HELPER_PATH="$<TARGET_FILE:qs_process_helper>")
add_dependencies(qs_process_test qs_process_helper)
add_test(NAME qs_process_test COMMAND qs_process_test)
set_tests_properties(qs_process_test PROPERTIES LABELS "common-cli" TIMEOUT 20)

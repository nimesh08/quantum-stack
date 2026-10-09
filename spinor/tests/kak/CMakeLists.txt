add_executable(spinor_kak_test kak_test.cpp)
target_link_libraries(spinor_kak_test PRIVATE spinor::passes)
target_compile_features(spinor_kak_test PRIVATE cxx_std_20)
add_test(NAME spinor_kak_test COMMAND spinor_kak_test)

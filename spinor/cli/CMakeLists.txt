# spinor/cli/CMakeLists.txt

add_executable(spinorc spinorc_main.cpp)
target_link_libraries(spinorc PRIVATE
  spinor::dialect
  spinor::parser
  spinor::verify
  spinor::registry
  spinor::passes
  spinor::sim
  spinor::emit
  spinor::submit
)
target_compile_features(spinorc PRIVATE cxx_std_20)

# phonon/cli/CMakeLists.txt
#
# `phononc` - Phonon IR driver. Middle of the Heisenberg toolchain.

add_executable(phononc phononc_main.cpp)
target_link_libraries(phononc PRIVATE
  qs::common_cli
)
target_compile_features(phononc PRIVATE cxx_std_20)
target_compile_definitions(phononc PRIVATE
  PHONONC_VERSION="0.1.0"
)

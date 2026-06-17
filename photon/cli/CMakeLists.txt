# photon/cli/CMakeLists.txt
#
# `photonc` - Photon language driver. Top of the Heisenberg toolchain.

add_executable(photonc photonc_main.cpp)
target_link_libraries(photonc PRIVATE
  photon_lang
  phonon_optimizer
  qs::common_cli
  phonon_dialect
)
target_compile_features(photonc PRIVATE cxx_std_20)
target_compile_definitions(photonc PRIVATE
  PHOTONC_VERSION="0.1.0"
)

# common/cli/CMakeLists.txt
#
# Shared CLI library used by spinorc, phononc, photonc.
# Contains:
#   - Flags     : declarative flag table + parser + --help renderer
#   - Providers : chip-id -> provider lookup (reads spinor/registry)
#   - Submit    : subprocess invocation of `python -m spinor_submit`
#   - Manifest  : JSON sidecar writer for the portable artifact

add_library(qs_common_cli STATIC
  lib/Flags.cpp
  lib/Providers.cpp
  lib/Submit.cpp
  lib/Manifest.cpp
)

target_include_directories(qs_common_cli
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(qs_common_cli PUBLIC cxx_std_20)

# Stays dependency-light on purpose; depends only on the spinor registry
# loader so it can resolve chip-id -> provider without re-implementing
# the YAML reader. No LLVM/MLIR dependency.
target_link_libraries(qs_common_cli PUBLIC
  spinor::registry
)

add_library(qs::common_cli ALIAS qs_common_cli)

if(QSTACK_ENABLE_TESTS)
  add_subdirectory(tests)
endif()

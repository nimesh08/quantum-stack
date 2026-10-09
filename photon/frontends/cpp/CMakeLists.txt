# photon/frontends/cpp/CMakeLists.txt
#
# C++ ingester ("photonc-cxx") for Phase C M5.
#
# The self-hosted path (recursive-descent over a small C++ subset) is
# always built. Optional LibTooling libraries are linked only when
# the caller explicitly selects a Clang package with Clang_DIR.

add_library(photon_cppfront STATIC
  lib/Ingest.cpp
)

target_include_directories(photon_cppfront PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(photon_cppfront PUBLIC photon_lang)

add_executable(photonc-cxx cli/Photonc.cpp)
target_link_libraries(photonc-cxx PRIVATE
  photon_cppfront
  photon_engine_cpp
  photon_lang
  phonon::dialect
  spinor::dialect)
target_compile_features(photonc-cxx PRIVATE cxx_std_20)

# Optional LibTooling integration: attempt only when Clang_DIR is
# explicitly provided. A normal top-level LLVM lookup creates LLVM_DIR
# even when LLVM is absent; it must not opt into unrelated system Clang
# packages, which may contain only a subset of the development libraries.
if(Clang_DIR)
  find_package(Clang QUIET CONFIG)
  if(Clang_FOUND)
    message(STATUS "photon: Clang found; LibTooling path enabled")
    target_compile_definitions(photon_cppfront PRIVATE PHOTON_HAVE_CLANG=1)
    target_link_libraries(photon_cppfront PUBLIC clangTooling clangFrontend
                                                clangAST clangBasic LLVMSupport)
  endif()
endif()

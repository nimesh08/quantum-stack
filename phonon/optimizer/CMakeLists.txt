# phonon/optimizer/CMakeLists.txt

add_library(phonon_optimizer STATIC
  lib/Optimizer.cpp
  lib/Synthesizer.cpp
  lib/ZxSimplifier.cpp
  lib/Pipeline.cpp
)
add_library(phonon::optimizer ALIAS phonon_optimizer)

target_include_directories(phonon_optimizer
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phonon_optimizer PUBLIC phonon::dialect)
target_compile_features(phonon_optimizer PUBLIC cxx_std_20)

# Optional: enable the Tweedledum-backed synthesizer when the user
# has tweedledum installed (C++17, MIT, https://github.com/boschmitt/tweedledum).
if(SPINOR_HAVE_TWEEDLEDUM)
  target_compile_definitions(phonon_optimizer PRIVATE SPINOR_HAVE_TWEEDLEDUM)
endif()

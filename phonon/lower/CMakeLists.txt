# phonon/lower/CMakeLists.txt

add_library(phonon_lower STATIC
  lib/Lowering.cpp
)
add_library(phonon::lower ALIAS phonon_lower)

target_include_directories(phonon_lower
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phonon_lower
  PUBLIC
    phonon::dialect
    spinor::dialect
    spinor::verify
)
target_compile_features(phonon_lower PUBLIC cxx_std_20)

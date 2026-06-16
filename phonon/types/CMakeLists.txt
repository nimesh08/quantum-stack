# phonon/types/CMakeLists.txt

add_library(phonon_types STATIC
  lib/LinearTypeChecker.cpp
)
add_library(phonon::types ALIAS phonon_types)

target_include_directories(phonon_types
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(phonon_types PUBLIC phonon::dialect spinor::verify)
target_compile_features(phonon_types PUBLIC cxx_std_20)

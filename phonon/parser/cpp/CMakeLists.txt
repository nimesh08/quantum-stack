# phonon/parser/cpp/CMakeLists.txt

add_library(phonon_parser STATIC
  lib/Lexer.cpp
  lib/Parser.cpp
)
add_library(phonon::parser ALIAS phonon_parser)

target_include_directories(phonon_parser
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
target_link_libraries(phonon_parser PUBLIC phonon::dialect)
target_compile_features(phonon_parser PUBLIC cxx_std_20)

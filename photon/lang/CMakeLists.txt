# photon/lang/CMakeLists.txt
#
# Photon language core: lexer, parser, AST, and lowering to Phonon.

add_library(photon_lang STATIC
  cpp/lib/Lexer.cpp
  cpp/lib/Module.cpp
  cpp/lib/Parser.cpp
  cpp/lib/Lower.cpp
  cpp/lib/Library.cpp
)

target_include_directories(photon_lang
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/cpp/lib
)

target_link_libraries(photon_lang PUBLIC phonon_dialect)

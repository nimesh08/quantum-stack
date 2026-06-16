# spinor/parser/cpp/CMakeLists.txt

add_library(spinor_parser_cpp STATIC
  lib/Lexer.cpp
  lib/Parser.cpp
)
target_include_directories(spinor_parser_cpp
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)
target_link_libraries(spinor_parser_cpp PUBLIC spinor::dialect)
target_compile_features(spinor_parser_cpp PUBLIC cxx_std_20)

add_library(spinor::parser ALIAS spinor_parser_cpp)

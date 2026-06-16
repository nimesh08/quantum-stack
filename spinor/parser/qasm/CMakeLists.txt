# spinor/parser/qasm/CMakeLists.txt

add_library(spinor_parser_qasm STATIC
  lib/Qasm.cpp
)
target_include_directories(spinor_parser_qasm
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_parser_qasm PUBLIC spinor::dialect)
target_compile_features(spinor_parser_qasm PUBLIC cxx_std_20)
add_library(spinor::parser_qasm ALIAS spinor_parser_qasm)

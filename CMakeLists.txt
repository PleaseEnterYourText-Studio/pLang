cmake_minimum_required(VERSION 3.15)
project(Mino2 VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

add_library(lexer_core STATIC
    src/Lexer.cpp
)

target_include_directories(lexer_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(Mino2
    src/main.cpp
)

target_link_libraries(Mino2 PRIVATE
    lexer_core
)

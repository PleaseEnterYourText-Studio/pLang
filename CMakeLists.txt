cmake_minimum_required(VERSION 3.25)
project(PLang LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT LLVM_DIR OR NOT EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    set(LLVM_DIR "" CACHE PATH "Path to LLVMConfig.cmake" FORCE)
    foreach(candidate "/usr/local/opt/llvm" "/opt/homebrew/opt/llvm")
        if(EXISTS "${candidate}/lib/cmake/llvm/LLVMConfig.cmake")
            set(LLVM_DIR "${candidate}/lib/cmake/llvm")
            break()
        endif()
    endforeach()
endif()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(PLang
        src/main.cpp
        include/main.h
        include/Lexer.h
        src/Lexer.cpp)

target_include_directories(PLang PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(PLang PRIVATE
        LLVM-C
        )

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(PLang PRIVATE -Wno-unused-parameter -Wno-comment)
endif()

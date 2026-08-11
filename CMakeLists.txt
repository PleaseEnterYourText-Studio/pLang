cmake_minimum_required(VERSION 3.25)
project(PLang LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_program(LLVM_CONFIG_EXECUTABLE
    NAMES llvm-config llvm-config-18 llvm-config-17 llvm-config-16
    PATHS
        "D:/llvm/bin"
        "C:/Program Files/LLVM/bin"
        "C:/LLVM/bin"
        "$ENV{LLVM_HOME}/bin"
)

if(LLVM_CONFIG_EXECUTABLE)
    execute_process(COMMAND ${LLVM_CONFIG_EXECUTABLE} --cmakedir
        OUTPUT_VARIABLE LLVM_CMAKE_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    
    if(EXISTS "${LLVM_CMAKE_DIR}/LLVMConfig.cmake")
        set(LLVM_DIR "${LLVM_CMAKE_DIR}" CACHE PATH "LLVM cmake directory" FORCE)
        message(STATUS "Found LLVM via llvm-config: ${LLVM_DIR}")
    endif()
endif()

if(NOT LLVM_DIR OR NOT EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    set(LLVM_DIR "" CACHE PATH "LLVM cmake directory" FORCE)
    
    set(LLVM_CANDIDATE_PATHS
        # Windows
        "D:/llvm/lib/cmake/llvm"
        "C:/Program Files/LLVM/lib/cmake/llvm"
        "C:/LLVM/lib/cmake/llvm"
        "C:/msys64/mingw64/lib/cmake/llvm"
        "C:/msys64/ucrt64/lib/cmake/llvm"
        # macOS
        "/usr/local/opt/llvm/lib/cmake/llvm"
        "/opt/homebrew/opt/llvm/lib/cmake/llvm"
        # Linux
        "/usr/lib/llvm-18/lib/cmake/llvm"
        "/usr/lib/llvm-17/lib/cmake/llvm"
        "/usr/lib/llvm-16/lib/cmake/llvm"
        "/usr/lib/cmake/llvm"
        # 环境变量
        "$ENV{LLVM_DIR}"
    )
    
    foreach(candidate ${LLVM_CANDIDATE_PATHS})
        if(EXISTS "${candidate}/LLVMConfig.cmake")
            set(LLVM_DIR "${candidate}" CACHE PATH "LLVM cmake directory" FORCE)
            message(STATUS "Found LLVM at: ${LLVM_DIR}")
            break()
        endif()
    endforeach()
endif()

if(NOT LLVM_DIR OR NOT EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
    message(FATAL_ERROR "
        ================================================================
        LLVM not found!
        
        Please install LLVM or set LLVM_DIR to the directory containing
        LLVMConfig.cmake.
        
        Example:
          cmake .. -DLLVM_DIR=\"D:/llvm/lib/cmake/llvm\"
        ================================================================
    ")
endif()

find_package(LLVM REQUIRED CONFIG)

message(STATUS "✅ Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "   LLVM_DIR: ${LLVM_DIR}")
message(STATUS "   Include: ${LLVM_INCLUDE_DIRS}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(PLang
    src/main.cpp
    src/main.plang
    include/main.h
    include/Lexer.h
    include/AST.h
    include/Parser.h
    include/Sema.h
    include/SymbolTable.h
    include/CodeGenerator.h
    include/token.h
    src/Lexer.cpp
    src/AST.cpp
    src/Parser.cpp
    src/Sema.cpp
    src/SymbolTable.cpp
    src/CodeGenerator.cpp
)

target_include_directories(PLang PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

llvm_map_components_to_libnames(llvm_libs 
    support 
    core 
    irreader 
    bitwriter 
    passes 
    analysis
    mc
    codegen
    target
    native
    asmprinter
    option
)

target_link_libraries(PLang PRIVATE ${llvm_libs})

# 监控 .plang 源码：修改后自动验证语法
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/main.plang.stamp
    COMMAND $<TARGET_FILE:PLang> ${CMAKE_CURRENT_SOURCE_DIR}/src/main.plang
    COMMAND ${CMAKE_COMMAND} -E touch ${CMAKE_CURRENT_BINARY_DIR}/main.plang.stamp
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/main.plang
    COMMENT "Validating src/main.plang"
    VERBATIM
)
add_custom_target(validate_plang ALL
    DEPENDS PLang ${CMAKE_CURRENT_BINARY_DIR}/main.plang.stamp
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(PLang PRIVATE -Wno-unused-parameter -Wno-comment)
endif()

message(STATUS "✅ PLang configured with LLVM ${LLVM_PACKAGE_VERSION}")
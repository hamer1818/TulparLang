# TulparLang - Programming Language (Modern C++17 + LLVM Backend)
# Supported platforms: Linux, macOS (Windows users should use WSL)
# Minimum CMake version
cmake_minimum_required(VERSION 3.14)

# Project name and version - Now using modern C++17
project(TulparLang VERSION 2.1.0 LANGUAGES C CXX)

# C++17 Standard (required for std::variant, std::optional, std::unique_ptr)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================
# Embed Standard Libraries (lib/*.tpr)
# ============================================
include(${CMAKE_SOURCE_DIR}/cmake/EmbedLibraries.cmake)

# ============================================
# LLVM is REQUIRED for TulparLang 2.x
# ============================================
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# C++ Compiler flags (modern, strict)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

# Known non-critical noise for current architecture strategy:
# - computed goto dispatch in VM (GNU extension)
# - compatibility conversions inherited from legacy code paths
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wno-gnu-label-as-value
        -Wno-c++20-extensions
        -Wno-c99-extensions
        -Wno-writable-strings
    )
endif()

# Debug/Release configurations
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DTULPAR_DEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)

# ============================================
# Source files
# ============================================

# Common
set(COMMON_SOURCES
    src/common/localization.cpp
    src/common/localization.hpp
)

# Core Language
set(LEXER_SOURCES
    src/lexer/lexer.cpp
    src/lexer/lexer.hpp
)

set(PARSER_SOURCES
    src/parser/parser.cpp
    src/parser/parser.hpp
)

# LLVM Backend (Primary) - Now using C++ API
set(AOT_SOURCES
    src/aot/llvm_backend.cpp
    src/aot/llvm_backend.hpp
    src/aot/llvm_types.cpp
    src/aot/llvm_types.hpp
    src/aot/llvm_values.cpp
    src/aot/llvm_values.hpp
    src/aot/aot_pipeline.cpp
    src/aot/aot_pipeline.hpp
)

# Type Inference
set(TYPEINFER_SOURCES
    src/typeinfer/typeinfer.cpp
    src/typeinfer/typeinfer.hpp
)

# Runtime Support (needed for AOT binaries)
set(RUNTIME_SOURCES
    src/vm/runtime_bindings.cpp
    src/vm/vm.cpp
    src/vm/vm.hpp
    src/vm/bytecode.cpp
    src/vm/bytecode.hpp
    src/vm/compiler.cpp
    src/vm/compiler.hpp
    runtime/cJSON.c
    runtime/tulpar_arc.cpp
    runtime/tulpar_arc.h
    runtime/tulpar_native.cpp
    runtime/tulpar_native.h
)

# Legacy (kept for compatibility, may be removed in future)
set(LEGACY_SOURCES
    src/interpreter/interpreter.cpp
    src/interpreter/interpreter.hpp
    src/jit/jit.hpp
    src/jit/jit_memory.cpp
    src/jit/x64_emit.hpp
    src/jit/x64_emit.cpp
    src/jit/jit_compiler.cpp
    src/jit/jit_optimize.cpp
)

set(MAIN_SOURCES
    src/main.cpp
)

set(SQLITE_SOURCES
    lib/sqlite3/sqlite3.c
)

# ============================================
# Main Executable
# ============================================
set(ALL_SOURCES
    ${COMMON_SOURCES}
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${AOT_SOURCES}
    ${TYPEINFER_SOURCES}
    ${RUNTIME_SOURCES}
    ${LEGACY_SOURCES}
    ${MAIN_SOURCES}
    ${SQLITE_SOURCES}
)

add_executable(tulpar ${ALL_SOURCES})

# Always enable AOT
target_compile_definitions(tulpar PRIVATE TULPAR_AOT_ENABLED)

# LLVM Linking - Cross-platform support (x86_64 + AArch64)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    # ARM64/AArch64 architecture (Apple Silicon, Linux ARM)
    llvm_map_components_to_libnames(llvm_libs 
        support core irreader 
        aarch64asmparser aarch64codegen aarch64desc aarch64info
        passes asmparser asmprinter
        target mc
    )
else()
    # x86_64 architecture (Intel/AMD)
    llvm_map_components_to_libnames(llvm_libs 
        support core irreader 
        x86asmparser x86codegen x86desc x86info
        passes asmparser asmprinter
        target mc
    )
endif()
target_link_libraries(tulpar ${llvm_libs} stdc++)

# ============================================
# Runtime Library (for AOT-compiled binaries)
# ============================================
add_library(tulpar_runtime STATIC
    src/common/localization.cpp
    src/vm/runtime_bindings.cpp
    src/vm/vm.cpp
    src/vm/bytecode.cpp
    src/vm/compiler.cpp
    runtime/cJSON.c
    src/lexer/lexer.cpp
    src/parser/parser.cpp
    src/jit/jit_memory.cpp
    src/jit/x64_emit.cpp
    src/jit/jit_compiler.cpp
    src/jit/jit_optimize.cpp
    lib/sqlite3/sqlite3.c
)
target_include_directories(tulpar_runtime PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(tulpar_runtime PRIVATE TULPAR_RUNTIME_ONLY)

# ============================================
# Platform-specific settings (Linux & macOS only)
# ============================================
if(APPLE)
    message(STATUS "Configuring for macOS")
    target_compile_definitions(tulpar PRIVATE PLATFORM_MACOS)
    target_link_libraries(tulpar pthread dl)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        message(STATUS "Building for Apple Silicon (ARM64)")
    endif()
elseif(UNIX)
    message(STATUS "Configuring for Linux")
    target_compile_definitions(tulpar PRIVATE PLATFORM_LINUX)
    target_link_libraries(tulpar m pthread dl)
else()
    message(FATAL_ERROR "Unsupported platform. TulparLang only supports Linux and macOS. Windows users should use WSL.")
endif()

# ============================================
# Installation
# ============================================
install(TARGETS tulpar RUNTIME DESTINATION bin)
install(TARGETS tulpar_runtime ARCHIVE DESTINATION lib)

install(DIRECTORY examples/
    DESTINATION share/tulpar/examples
    FILES_MATCHING PATTERN "*.tpr"
)

install(FILES
    README.md
    QUICKSTART.md
    KULLANIM.md
    GELECEK_OZELLIKLER.md
    TIP_GUVENLIKLI_DIZILER.md
    ARRAYJSON_KULLANIM.md
    CHANGELOG.md
    DESTINATION share/doc/tulpar
)

# ============================================
# Build Info
# ============================================
message(STATUS "")
message(STATUS "=== TulparLang Build Configuration ===")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "LLVM Version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "======================================")
# Touch for rebuild

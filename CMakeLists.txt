# TulparLang - Programming Language (Modern C++17 + LLVM Backend)
# Supported platforms: Linux, macOS, Windows (native MSVC support)
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
# Try CONFIG mode first (preferred), fallback to MODULE mode
find_package(LLVM CONFIG QUIET)
if(NOT LLVM_FOUND)
    message(STATUS "LLVM CONFIG mode failed, trying without CONFIG requirement...")
    find_package(LLVM REQUIRED)
endif()

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")
message(STATUS "LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_LIBRARY_DIRS: ${LLVM_LIBRARY_DIRS}")

# Fallback: If LLVM_INCLUDE_DIRS is not set, try to find it manually on Windows
if(NOT LLVM_INCLUDE_DIRS AND WIN32)
    if(DEFINED ENV{LLVM_CMAKE_DIR})
        get_filename_component(LLVM_ROOT "$ENV{LLVM_CMAKE_DIR}" DIRECTORY)
        get_filename_component(LLVM_ROOT "${LLVM_ROOT}" DIRECTORY)
        get_filename_component(LLVM_ROOT "${LLVM_ROOT}" DIRECTORY)
        set(LLVM_INCLUDE_DIRS "${LLVM_ROOT}/include")
        set(LLVM_LIBRARY_DIRS "${LLVM_ROOT}/lib")
        message(STATUS "Using manual LLVM paths on Windows:")
        message(STATUS "  LLVM_ROOT: ${LLVM_ROOT}")
        message(STATUS "  LLVM_INCLUDE_DIRS: ${LLVM_INCLUDE_DIRS}")
        message(STATUS "  LLVM_LIBRARY_DIRS: ${LLVM_LIBRARY_DIRS}")
    endif()
endif()

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# C++ Compiler flags (modern, strict)
if(MSVC)
    # MSVC specific flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /EHsc /permissive-")
    # Disable specific MSVC warnings
    add_compile_options(
        /wd4244  # conversion from 'type1' to 'type2', possible loss of data
        /wd4267  # conversion from 'size_t' to 'type', possible loss of data
        /wd4996  # function was declared deprecated
        /wd4005  # macro redefinition
        /wd4146  # unary minus operator applied to unsigned type
        /wd4624  # destructor was implicitly defined as deleted
    )
    # Enable big object files for LLVM
    add_compile_options(/bigobj)
else()
    # GCC/Clang flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
endif()

# Known non-critical noise for current architecture strategy:
# - computed goto dispatch in VM (GNU extension)
# - compatibility conversions inherited from legacy code paths
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-pedantic>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-write-strings>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-but-set-variable>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-maybe-uninitialized>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-gnu-label-as-value>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-c++20-extensions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-c99-extensions>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-writable-strings>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-pedantic>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-defaulted-function-deleted>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-function>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-but-set-variable>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
    )
endif()

# Debug/Release configurations
if(MSVC)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /DTULPAR_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DTULPAR_DEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
endif()

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

# Windows MSVC specific linking requirements
if(MSVC)
    # Link against version.lib which is required by LLVM on Windows
    target_link_libraries(tulpar ${llvm_libs} version)
else()
    target_link_libraries(tulpar ${llvm_libs})
endif()

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

# Windows-specific settings for runtime
if(WIN32 AND MSVC)
    target_compile_options(tulpar_runtime PRIVATE 
        /utf-8
        /W3
        /wd4996
        /wd4244
        /wd4267
    )
    target_compile_definitions(tulpar_runtime PRIVATE 
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
        PLATFORM_WINDOWS
    )
endif()

# ============================================
# Platform-specific settings
# ============================================
if(APPLE)
    message(STATUS "Configuring for macOS")
    target_compile_definitions(tulpar PRIVATE PLATFORM_MACOS)
    target_link_options(tulpar PRIVATE "-Wl,-no_warn_duplicate_libraries")
    target_link_libraries(tulpar pthread dl)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        message(STATUS "Building for Apple Silicon (ARM64)")
    endif()
elseif(UNIX)
    message(STATUS "Configuring for Linux")
    target_compile_definitions(tulpar PRIVATE PLATFORM_LINUX)
    target_link_libraries(tulpar m pthread dl)
elseif(WIN32)
    message(STATUS "Configuring for Windows (MSVC)")
    target_compile_definitions(tulpar PRIVATE PLATFORM_WINDOWS)
    
    # Winsock2 for networking
    target_link_libraries(tulpar ws2_32 wsock32)
    
    # UTF-8 console support
    if(MSVC)
        target_compile_options(tulpar PRIVATE 
            /utf-8           # UTF-8 source files
            /W3              # Warning level 3
            /wd4996          # Disable deprecated function warnings
            /wd4244          # Disable conversion warnings
            /wd4267          # Disable size_t conversion warnings
            /EHsc            # Exception handling model
        )
        
        # Disable "unsafe" CRT warnings for standard C functions
        target_compile_definitions(tulpar PRIVATE 
            _CRT_SECURE_NO_WARNINGS
            _WINSOCK_DEPRECATED_NO_WARNINGS
        )
    endif()
else()
    message(FATAL_ERROR "Unsupported platform. TulparLang supports Linux, macOS, and Windows.")
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

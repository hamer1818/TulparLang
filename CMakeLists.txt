# TulparLang - Cross-Platform Programming Language (LLVM Backend)
# Minimum CMake version
cmake_minimum_required(VERSION 3.14)

# Project name and version
project(TulparLang VERSION 2.1.0 LANGUAGES C CXX)

# C Standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# Compiler flags
if(MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
endif()

# Debug/Release configurations
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g -DTULPAR_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)

# ============================================
# Source files
# ============================================

# Core Language
set(LEXER_SOURCES
    src/lexer/lexer.c
    src/lexer/lexer.h
)

set(PARSER_SOURCES
    src/parser/parser.c
    src/parser/parser.h
)

# LLVM Backend (Primary)
set(AOT_SOURCES
    src/aot/llvm_backend.c
    src/aot/llvm_backend.h
    src/aot/llvm_types.c
    src/aot/llvm_types.h
    src/aot/llvm_values.c
    src/aot/llvm_values.h
    src/aot/aot_pipeline.c
    src/aot/aot_pipeline.h
)

# Type Inference
set(TYPEINFER_SOURCES
    src/typeinfer/typeinfer.c
    src/typeinfer/typeinfer.h
)

# Runtime Support (needed for AOT binaries)
set(RUNTIME_SOURCES
    src/vm/runtime_bindings.c
    src/vm/vm.c
    src/vm/vm.h
    src/vm/bytecode.c
    src/vm/bytecode.h
    src/vm/compiler.c
    src/vm/compiler.h
    runtime/cJSON.c
    runtime/tulpar_arc.c
    runtime/tulpar_arc.h
    runtime/tulpar_native.c
    runtime/tulpar_native.h
)

# Legacy (kept for compatibility, may be removed in future)
set(LEGACY_SOURCES
    src/interpreter/interpreter.c
    src/interpreter/interpreter.h
    src/jit/jit.h
    src/jit/jit_memory.c
    src/jit/x64_emit.h
    src/jit/x64_emit.c
    src/jit/jit_compiler.c
    src/jit/jit_optimize.c
)

set(MAIN_SOURCES
    src/main.c
)

set(SQLITE_SOURCES
    lib/sqlite3/sqlite3.c
)

# ============================================
# Main Executable
# ============================================
set(ALL_SOURCES
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
    src/vm/runtime_bindings.c
    src/vm/vm.c
    src/vm/bytecode.c
    src/vm/compiler.c
    runtime/cJSON.c
    src/lexer/lexer.c
    src/parser/parser.c
    src/jit/jit_memory.c
    src/jit/x64_emit.c
    src/jit/jit_compiler.c
    src/jit/jit_optimize.c
    lib/sqlite3/sqlite3.c
)
target_include_directories(tulpar_runtime PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(tulpar_runtime PRIVATE TULPAR_RUNTIME_ONLY)

# ============================================
# Platform-specific settings
# ============================================
if(WIN32)
    message(STATUS "Configuring for Windows")
    target_compile_definitions(tulpar PRIVATE PLATFORM_WINDOWS)
    target_link_libraries(tulpar ws2_32)
elseif(APPLE)
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
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")
message(STATUS "======================================")

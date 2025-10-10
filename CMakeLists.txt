# OLang - Cross-Platform Programming Language
# Minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(OLang VERSION 1.2.2 LANGUAGES C)

# C Standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags
if(MSVC)
    # Windows (Visual Studio)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /W4")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    # Unix-like systems (Linux, macOS)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
endif()

# Debug/Release configurations
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)

# Source files
set(LEXER_SOURCES
    src/lexer/lexer.c
    src/lexer/lexer.h
)

set(PARSER_SOURCES
    src/parser/parser.c
    src/parser/parser.h
)

set(INTERPRETER_SOURCES
    src/interpreter/interpreter.c
    src/interpreter/interpreter.h
)

set(MAIN_SOURCES
    src/main.c
)

# All sources
set(ALL_SOURCES
    ${LEXER_SOURCES}
    ${PARSER_SOURCES}
    ${INTERPRETER_SOURCES}
    ${MAIN_SOURCES}
)

# Executable
add_executable(olang ${ALL_SOURCES})

# Platform-specific settings
if(WIN32)
    # Windows specific
    message(STATUS "Configuring for Windows")
    target_compile_definitions(olang PRIVATE PLATFORM_WINDOWS)
elseif(APPLE)
    # macOS specific
    message(STATUS "Configuring for macOS")
    target_compile_definitions(olang PRIVATE PLATFORM_MACOS)
    # Apple Silicon support
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        message(STATUS "Building for Apple Silicon (ARM64)")
    endif()
elseif(UNIX)
    # Linux specific
    message(STATUS "Configuring for Linux")
    target_compile_definitions(olang PRIVATE PLATFORM_LINUX)
    target_link_libraries(olang m)  # Link math library
endif()

# Installation
install(TARGETS olang
    RUNTIME DESTINATION bin
)

# Install examples
install(DIRECTORY examples/
    DESTINATION share/olang/examples
    FILES_MATCHING PATTERN "*.olang"
)

# Install documentation
install(FILES
    README.md
    QUICKSTART.md
    KULLANIM.md
    GELECEK_OZELLIKLER.md
    TIP_GUVENLIKLI_DIZILER.md
    ARRAYJSON_KULLANIM.md
    CHANGELOG.md
    DESTINATION share/doc/olang
)

# Print build info
message(STATUS "OLang Version: ${PROJECT_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "System: ${CMAKE_SYSTEM_NAME}")


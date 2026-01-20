# Tulpar AOT Compiler - CMake Build File

cmake_minimum_required(VERSION 3.20)
project(Tulpar_AOT C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# For now, just create a placeholder
# Full LLVM integration coming after we download proper headers

message(STATUS "Tulpar AOT Compiler - Phase 1 Setup")
message(STATUS "")
message(STATUS "Current Status:")
message(STATUS "  ✓ LLVM 21.1.8 detected at: C:/Program Files/LLVM")
message(STATUS "  ✗ LLVM C headers missing in Windows binary distribution")
message(STATUS "")
message(STATUS "Next Steps:")
message(STATUS "  1. Download LLVM source for headers")
message(STATUS "  2. Or use llvm-c headers from GitHub")
message(STATUS "  3. Then we can build tulpac compiler!")
message(STATUS "")

# Placeholder executable for now
add_executable(tulpac_placeholder
    llvm_test.c
)

target_include_directories(tulpac_placeholder PRIVATE
    "C:/Program Files/LLVM/include"
)

message(STATUS "Build system ready. Run 'cmake --build .' to test.")

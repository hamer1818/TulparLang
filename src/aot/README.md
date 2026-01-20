# Tulpar LLVM AOT Backend

## Overview

This directory contains the LLVM-based Ahead-of-Time (AOT) compiler for Tulpar.

**Goal:** Compile Tulpar source code to native machine code for maximum performance (3-5x slower than C, vs 51x for bytecode).

## Architecture

```
Tulpar Source (.tpr)
        ↓
    Parser (existing)
        ↓
      AST
        ↓
  LLVM Backend (NEW!)
        ↓
    LLVM IR
        ↓
  LLVM Optimizer
        ↓
  Native Code (x64/ARM)
```

## File Structure

```
src/aot/
├── llvm_backend.h      # Main AOT compiler interface
├── llvm_backend.c      # LLVM module/context management
├── llvm_codegen.h      # Code generation functions
├── llvm_codegen.c      # AST → LLVM IR translation
├── llvm_types.c        # Type mapping (Tulpar → LLVM)
├── llvm_optimize.c     # LLVM optimization passes
└── tulpac.c            # Compiler driver (main)

runtime/
├── tulpar_runtime.h    # Runtime library interface
├── tulpar_runtime.c    # Runtime functions (strings, arrays, etc)
└── tulpar_builtins.c   # Built-in functions (print, length, etc)
```

## Hybrid Approach

**Development Mode:**
```bash
tulpar run script.tpr
```
→ Uses bytecode VM (fast iteration, debugging)

**Production Mode:**
```bash
tulpac compile script.tpr -o app.exe
./app.exe
```
→ Uses LLVM AOT (maximum performance)

## Build Requirements

- LLVM 17+ (21+ recommended)
- CMake 3.20+
- C compiler (GCC/Clang/MSVC)

## Quick Start

```bash
# Check LLVM installation
llvm-config --version

# Build tulpac
cd src/aot
make

# Compile Tulpar program
tulpac examples/fibonacci.tpr -o fib.exe

# Run
./fib.exe
```

## Performance Goals

| Benchmark | Bytecode VM | AOT (Target) | Speedup |
|-----------|-------------|--------------|---------|
| Fibonacci(30) | 60 ms | 2-4 ms | 15-30x |
| Loop 1M | 11 ms | 1-2 ms | 5-11x |
| String 100K | 2 ms | 0.2 ms | 4-10x |
| **Overall** | **83 ms** | **5-10 ms** | **8-16x** 🚀 |

**Final Target:** 3-6x slower than C (vs current 51x!)

## Status

✅ Phase 1.1: LLVM setup (in progress)
⏳ Phase 1.2: Basic IR generation
⏳ Phase 1.3: Variables & storage
⏳ Phase 2: Control flow & functions
⏳ Phase 3: Runtime library & full language

## References

- [LLVM C API Documentation](https://llvm.org/doxygen/group__LLVMC.html)
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
- [Kaleidoscope Tutorial](https://llvm.org/docs/tutorial/)

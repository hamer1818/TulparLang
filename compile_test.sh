#!/bin/bash
set -e

echo "Finding LLVM configuration..."
LLVM_CFLAGS=$(llvm-config --cflags)
LLVM_LDFLAGS=$(llvm-config --ldflags)
LLVM_LIBS=$(llvm-config --libs core native analysis executionengine)
LLVM_SYS_LIBS=$(llvm-config --system-libs)

echo "Compiling..."
# Note: Added -fPIC and appropriate flags. Using gcc.
gcc -g -I src src/aot/llvm_test_ops.c src/aot/llvm_backend.c src/aot/llvm_types.c src/aot/llvm_values.c src/vm/vm_stub.c src/vm/runtime_bindings.c $LLVM_CFLAGS $LLVM_LDFLAGS $LLVM_LIBS $LLVM_SYS_LIBS -o llvm_test.out

echo "Compilation successful. Running test..."
./llvm_test.out

#ifndef BYTECODE_H
#define BYTECODE_H

#include <stdint.h>
#include <stdlib.h>

// ============================================================================
// TULPAR BYTECODE - FAZ 2 OPTİMİZASYON
// ============================================================================

// Bytecode instruction opcodes
typedef enum {
  // ----------------------
  // STACK OPERATIONS
  // ----------------------
  OP_NOP = 0, // No operation
  OP_POP,     // Pop top of stack
  OP_DUP,     // Duplicate top of stack

  // ----------------------
  // CONSTANTS
  // ----------------------
  OP_CONST_INT,   // Push integer constant (followed by 8-byte value)
  OP_CONST_FLOAT, // Push float constant (followed by 4-byte value)
  OP_CONST_TRUE,  // Push true
  OP_CONST_FALSE, // Push false
  OP_CONST_VOID,  // Push void/null
  OP_CONST_FUNC,  // Push function constant
  OP_CONST_STR,   // Push string constant (followed by string index)

  // ----------------------
  // ARITHMETIC
  // ----------------------
  OP_ADD, // a + b
  OP_SUB, // a - b
  OP_MUL, // a * b
  OP_DIV, // a / b
  OP_MOD, // a % b
  OP_NEG, // -a (unary negation)
  OP_INC, // a++ (increment)
  OP_DEC, // a-- (decrement)

  // ----------------------
  // COMPARISON
  // ----------------------
  OP_EQ, // a == b
  OP_NE, // a != b
  OP_LT, // a < b
  OP_LE, // a <= b
  OP_GT, // a > b
  OP_GE, // a >= b

  // ----------------------
  // LOGIC
  // ----------------------
  OP_AND, // a && b
  OP_OR,  // a || b
  OP_NOT, // !a

  // ----------------------
  // VARIABLES
  // ----------------------
  OP_LOAD_LOCAL,   // Load local variable (followed by slot index)
  OP_STORE_LOCAL,  // Store to local variable (followed by slot index)
  OP_LOAD_GLOBAL,  // Load global variable (followed by name index)
  OP_STORE_GLOBAL, // Store to global variable (followed by name index)

  // ----------------------
  // CONTROL FLOW
  // ----------------------
  OP_JUMP,          // Unconditional jump (followed by offset)
  OP_JUMP_IF_FALSE, // Jump if top of stack is false (followed by offset)
  OP_JUMP_IF_TRUE,  // Jump if top of stack is true (followed by offset)
  OP_LOOP,          // Loop back (followed by offset)

  // ----------------------
  // FUNCTIONS
  // ----------------------
  OP_CALL,         // Call function (followed by arg count)
  OP_TAIL_CALL,    // Tail call function (followed by arg count)
  OP_CALL_BUILTIN, // Call built-in function (followed by builtin id, arg count)
  OP_RETURN,       // Return from function
  OP_RETURN_VOID,  // Return void from function

  // ----------------------
  // ARRAYS & OBJECTS
  // ----------------------
  OP_ARRAY_NEW,  // Create new array (followed by initial size)
  OP_ARRAY_PUSH, // Push to array
  OP_ARRAY_GET,  // Get array element
  OP_ARRAY_SET,  // Set array element
  OP_OBJECT_NEW, // Create new object
  OP_OBJECT_GET, // Get object property
  OP_OBJECT_SET, // Set object property

  // ----------------------
  // SPECIAL
  // ----------------------
  OP_PRINT,   // Print (followed by arg count)
  OP_IMPORT,  // Import module
  OP_TRY,     // Start try block (pushes handler)
  OP_POP_TRY, // End try block (pops handler)
  OP_THROW,   // Throw exception
  OP_HALT,    // End of program

  // ----------------------
  // OPTIMIZED OPCODES (Superinstructions)
  // ----------------------
  OP_FOR_ITER,      // Optimized for loop: slot(2) + limit(8) + body_end_offset(2)
  OP_INC_LOCAL,     // Increment local variable in-place: slot(2)
  OP_DEC_LOCAL,     // Decrement local variable in-place: slot(2)
  
  // ----------------------
  // TYPE-SPECIALIZED OPCODES (No type check)
  // ----------------------
  OP_ADD_INT,       // Integer add (no type check)
  OP_SUB_INT,       // Integer subtract
  OP_MUL_INT,       // Integer multiply
  OP_DIV_INT,       // Integer divide
  OP_LT_INT,        // Integer less than
  OP_LE_INT,        // Integer less or equal
  OP_GT_INT,        // Integer greater than
  OP_GE_INT,        // Integer greater or equal
  OP_EQ_INT,        // Integer equal
  OP_NE_INT,        // Integer not equal

  // ----------------------
  // FAST CALL OPCODES (No JIT check, minimal overhead)
  // ----------------------
  OP_CALL_FAST,     // Fast function call - no JIT, no type check
  OP_RETURN_INT,    // Return integer directly to accumulator
  OP_CALL_0,        // Call with 0 args (ultra-fast)
  OP_CALL_1,        // Call with 1 arg (ultra-fast)
  OP_CALL_2,        // Call with 2 args (fibonacci pattern)
  OP_RETURN_FAST,   // Ultra-fast return - minimal stack manipulation
  
  // ----------------------
  // SUPERINSTRUCTIONS (Combined opcodes)
  // ----------------------
  OP_LOAD_LOCAL_ADD,    // LOAD_LOCAL + ADD (common in loops)
  OP_LOAD_LOCAL_SUB,    // LOAD_LOCAL + SUB
  OP_LOAD_LOCAL_LT,     // LOAD_LOCAL + LT (for loop conditions)
  OP_DEC_JUMP_NZ,       // Decrement local, jump if not zero
  OP_LOAD_ADD_STORE,    // sum = sum + i pattern
  OP_INC_LOCAL_FAST,    // var = var + 1 (common counter increment)
  
  // ----------------------
  // REGISTER-BASED OPCODES (Faz 2 - Hibrit)
  // Format: OP_XXX_RRR dst, src1, src2 (3 register operands)
  // Registers are local slot indices (r0 = slot[0], r1 = slot[1], ...)
  // ----------------------
  OP_ADD_RRR,           // dst = src1 + src2 (register-to-register add)
  OP_SUB_RRR,           // dst = src1 - src2
  OP_MUL_RRR,           // dst = src1 * src2
  OP_DIV_RRR,           // dst = src1 / src2
  OP_LT_RRR,            // dst = src1 < src2
  OP_LE_RRR,            // dst = src1 <= src2
  OP_GT_RRR,            // dst = src1 > src2
  OP_GE_RRR,            // dst = src1 >= src2
  OP_EQ_RRR,            // dst = src1 == src2
  OP_NE_RRR,            // dst = src1 != src2
  OP_MOV_RR,            // dst = src (register copy)
  OP_MOV_RI,            // dst = imm (load immediate to register)
  OP_ADD_RI,            // dst = dst + imm (add immediate)
  OP_SUB_RI,            // dst = dst - imm (subtract immediate)
  OP_LT_RI,             // dst = dst < imm (compare with immediate)
  
  OP_OPCODE_COUNT   // Total number of opcodes
} OpCode;

// ============================================================================
// BYTECODE CHUNK
// ============================================================================

// Constant pool entry types
typedef enum {
  CONST_INT,
  CONST_FLOAT,
  CONST_STRING,
  CONST_FUNCTION
} ConstantType;

// Constant value
typedef struct {
  ConstantType type;
  union {
    long long int_val;
    float float_val;
    char *string_val;
    struct CompiledFunction *func_val;
  };
} Constant;

// Bytecode chunk - bir fonksiyon veya global scope için
typedef struct {
  uint8_t *code; // Bytecode array
  int code_length;
  int code_capacity;

  // Constant pool
  Constant *constants;
  int const_count;
  int const_capacity;

  // Line number info (debugging)
  int *lines;
  int line_count;
  int line_capacity;

  // Local variable names (for error messages)
  char **local_names;
  int local_count;
} Chunk;

// Compiled function
typedef struct CompiledFunction {
  char *name;
  int arity;       // Parameter count
  Chunk *chunk;    // Function bytecode
  int local_count; // Number of local variables
} CompiledFunction;

// ============================================================================
// CHUNK FUNCTIONS
// ============================================================================

// Create new chunk
Chunk *chunk_create();

// Free chunk
void chunk_free(Chunk *chunk);

// Write byte to chunk
void chunk_write(Chunk *chunk, uint8_t byte, int line);

// Write multi-byte values
void chunk_write_int64(Chunk *chunk, long long value, int line);
void chunk_write_int32(Chunk *chunk, int value, int line);
void chunk_write_int16(Chunk *chunk, short value, int line);

// Add constant to pool and return index
int chunk_add_constant(Chunk *chunk, Constant constant);

// Add string constant
int chunk_add_string(Chunk *chunk, const char *str);

// Add integer constant
int chunk_add_int(Chunk *chunk, long long value);

// Add float constant
int chunk_add_float(Chunk *chunk, float value);

// Disassemble chunk (for debugging)
void chunk_disassemble(Chunk *chunk, const char *name);

#endif // BYTECODE_H

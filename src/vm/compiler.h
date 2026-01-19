#ifndef COMPILER_H
#define COMPILER_H

#include "../parser/parser.h"
#include "bytecode.h"


// ============================================================================
// TULPAR COMPILER - AST to Bytecode
// ============================================================================

// Local variable type hints for specialization
typedef enum {
  LOCAL_TYPE_UNKNOWN = 0,
  LOCAL_TYPE_INT,
  LOCAL_TYPE_FLOAT,
  LOCAL_TYPE_STRING,
  LOCAL_TYPE_BOOL
} LocalType;

// Local variable tracking
typedef struct {
  char *name;
  int depth;      // Scope depth
  int slot;       // Stack slot
  LocalType type; // Type hint for specialization
} Local;

// Compiler state
typedef struct Compiler {
  // Current chunk being compiled
  Chunk *chunk;

  // Local variables
  Local locals[256];
  int local_count;
  int scope_depth;

  // Loop tracking (for break/continue)
  int loop_start;      // Condition start for loop back
  int loop_depth;
  int is_for_loop;     // True if current loop is for (needs forward continue jumps)
  int break_jumps[64];     // Break jump offsets to patch
  int break_count;
  int continue_jumps[64];  // Continue jump offsets to patch (for for-loops)
  int continue_count;

  // Enclosing compiler (for nested functions)
  struct Compiler *enclosing;

  // Current function being compiled
  CompiledFunction *function;

} Compiler;

// ============================================================================
// COMPILER FUNCTIONS
// ============================================================================

// Create compiler
Compiler *compiler_create();

// Free compiler
void compiler_free(Compiler *compiler);

// Compile AST to bytecode chunk
Chunk *compile(ASTNode *ast);

// Compile expression
void compile_expression(Compiler *compiler, ASTNode *node);

// Compile statement
void compile_statement(Compiler *compiler, ASTNode *node);

// Scope management
void compiler_begin_scope(Compiler *compiler);
void compiler_end_scope(Compiler *compiler);

// Local variable management
int compiler_add_local(Compiler *compiler, const char *name);
int compiler_resolve_local(Compiler *compiler, const char *name);

// Emit bytecode
void emit_byte(Compiler *compiler, uint8_t byte, int line);
void emit_bytes(Compiler *compiler, uint8_t b1, uint8_t b2, int line);
void emit_short(Compiler *compiler, uint16_t value, int line);
void emit_constant(Compiler *compiler, Constant constant, int line);
void emit_return(Compiler *compiler, int line);

// Jump management
int emit_jump(Compiler *compiler, uint8_t op, int line);
void patch_jump(Compiler *compiler, int offset);
void emit_loop(Compiler *compiler, int loop_start, int line);

#endif // COMPILER_H

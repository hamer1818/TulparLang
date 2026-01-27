#include "compiler.h"
#include <stdio.h>
#include <string.h>

// Forward decl
void compile_statement(Compiler *compiler, ASTNode *node);

// ============================================================================
// TULPAR COMPILER IMPLEMENTATION
// ============================================================================

static Compiler *current = NULL;

// ============================================================================
// COMPILER CREATION
// ============================================================================

Compiler *compiler_create() {
  Compiler *compiler = (Compiler *)malloc(sizeof(Compiler));

  compiler->chunk = chunk_create();
  compiler->local_count = 0;
  compiler->scope_depth = 0;
  compiler->loop_start = -1;
  compiler->loop_depth = 0;
  compiler->is_for_loop = 0;
  compiler->break_count = 0;
  compiler->continue_count = 0;
  compiler->enclosing = NULL;
  compiler->function = NULL;

  return compiler;
}

void compiler_free(Compiler *compiler) {
  if (!compiler)
    return;
  // Note: chunk is returned to caller, not freed here
  free(compiler);
}

// ============================================================================
// EMIT HELPERS
// ============================================================================

void emit_byte(Compiler *compiler, uint8_t byte, int line) {
  chunk_write(compiler->chunk, byte, line);
}

void emit_bytes(Compiler *compiler, uint8_t b1, uint8_t b2, int line) {
  emit_byte(compiler, b1, line);
  emit_byte(compiler, b2, line);
}

void emit_short(Compiler *compiler, uint16_t value, int line) {
  emit_byte(compiler, value & 0xFF, line);
  emit_byte(compiler, (value >> 8) & 0xFF, line);
}

void emit_constant(Compiler *compiler, Constant constant, int line) {
  int idx = chunk_add_constant(compiler->chunk, constant);
  emit_short(compiler, (uint16_t)idx, line);
}

void emit_return(Compiler *compiler, int line) {
  emit_byte(compiler, OP_RETURN_VOID, line);
}

int emit_jump(Compiler *compiler, uint8_t op, int line) {
  emit_byte(compiler, op, line);
  emit_short(compiler, 0xFFFF, line);      // Placeholder
  return compiler->chunk->code_length - 2; // Return offset of placeholder
}

void patch_jump(Compiler *compiler, int offset) {
  int jump = compiler->chunk->code_length - offset - 2;
  if (jump > 0xFFFF) {
    printf("Compiler Error: Jump too large\n");
    return;
  }
  compiler->chunk->code[offset] = jump & 0xFF;
  compiler->chunk->code[offset + 1] = (jump >> 8) & 0xFF;
}

void emit_loop(Compiler *compiler, int loop_start, int line) {
  emit_byte(compiler, OP_LOOP, line);
  int offset = compiler->chunk->code_length - loop_start + 2;
  emit_short(compiler, (uint16_t)offset, line);
}

// ============================================================================
// SCOPE MANAGEMENT
// ============================================================================

void compiler_begin_scope(Compiler *compiler) { compiler->scope_depth++; }

void compiler_end_scope(Compiler *compiler) {
  compiler->scope_depth--;

  // Pop locals that are out of scope
  while (compiler->local_count > 0 &&
         compiler->locals[compiler->local_count - 1].depth >
             compiler->scope_depth) {
    emit_byte(compiler, OP_POP, 0);
    compiler->local_count--;
  }
}

int compiler_add_local(Compiler *compiler, const char *name) {
  if (compiler->local_count >= 256) {
    printf("Compiler Error: Too many local variables\n");
    return -1;
  }

  Local *local = &compiler->locals[compiler->local_count];
  local->name = strdup(name);
  local->depth = compiler->scope_depth;
  local->slot = compiler->local_count;
  local->type = LOCAL_TYPE_UNKNOWN; // Default to unknown

  return compiler->local_count++;
}

// Add local with type hint
int compiler_add_local_typed(Compiler *compiler, const char *name,
                             LocalType type) {
  int slot = compiler_add_local(compiler, name);
  if (slot >= 0) {
    compiler->locals[slot].type = type;
  }
  return slot;
}

// Get local variable's type hint
LocalType compiler_get_local_type(Compiler *compiler, int slot) {
  if (slot >= 0 && slot < compiler->local_count) {
    return compiler->locals[slot].type;
  }
  return LOCAL_TYPE_UNKNOWN;
}

int compiler_resolve_local(Compiler *compiler, const char *name) {
  for (int i = compiler->local_count - 1; i >= 0; i--) {
    Local *local = &compiler->locals[i];
    if (strcmp(local->name, name) == 0) {
      return local->slot;
    }
  }
  return -1; // Not found (might be global)
}

// ============================================================================
// TYPE INFERENCE FOR SPECIALIZATION
// ============================================================================

// Infer the type of an expression at compile time
// Returns LOCAL_TYPE_UNKNOWN if type cannot be determined
static LocalType infer_expression_type(Compiler *compiler, ASTNode *node) {
  if (!node)
    return LOCAL_TYPE_UNKNOWN;

  switch (node->type) {
  case AST_INT_LITERAL:
    // Integer literal
    return LOCAL_TYPE_INT;

  case AST_FLOAT_LITERAL:
    return LOCAL_TYPE_FLOAT;

  case AST_STRING_LITERAL:
    return LOCAL_TYPE_STRING;

  case AST_BOOL_LITERAL:
    return LOCAL_TYPE_BOOL;

  case AST_IDENTIFIER: {
    // Look up local variable type
    int slot = compiler_resolve_local(compiler, node->name);
    if (slot >= 0) {
      return compiler_get_local_type(compiler, slot);
    }
    return LOCAL_TYPE_UNKNOWN; // Global or unknown
  }

  case AST_BINARY_OP: {
    // Arithmetic operations preserve type if both operands match
    LocalType left = infer_expression_type(compiler, node->left);
    LocalType right = infer_expression_type(compiler, node->right);

    if (left == LOCAL_TYPE_INT && right == LOCAL_TYPE_INT) {
      // Comparisons return bool, arithmetic returns int
      switch (node->op) {
      case TOKEN_LESS:
      case TOKEN_LESS_EQUAL:
      case TOKEN_GREATER:
      case TOKEN_GREATER_EQUAL:
      case TOKEN_EQUAL:
      case TOKEN_NOT_EQUAL:
        return LOCAL_TYPE_BOOL;
      default:
        return LOCAL_TYPE_INT;
      }
    }
    return LOCAL_TYPE_UNKNOWN;
  }

  default:
    return LOCAL_TYPE_UNKNOWN;
  }
}

// Check if both operands of a binary op are integers
static int is_int_binary_op(Compiler *compiler, ASTNode *node) {
  if (!node || node->type != AST_BINARY_OP)
    return 0;
  LocalType left = infer_expression_type(compiler, node->left);
  LocalType right = infer_expression_type(compiler, node->right);
  return (left == LOCAL_TYPE_INT && right == LOCAL_TYPE_INT);
}

// ============================================================================
// EXPRESSION COMPILATION
// ============================================================================

void compile_expression(Compiler *compiler, ASTNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_INT_LITERAL: {
    emit_byte(compiler, OP_CONST_INT, node->line);
    Constant c = {.type = CONST_INT, .int_val = node->value.int_value};
    emit_constant(compiler, c, node->line);
    break;
  }

  case AST_FLOAT_LITERAL: {
    emit_byte(compiler, OP_CONST_FLOAT, node->line);
    Constant c = {.type = CONST_FLOAT, .float_val = node->value.float_value};
    emit_constant(compiler, c, node->line);
    break;
  }

  case AST_STRING_LITERAL: {
    emit_byte(compiler, OP_CONST_STR, node->line);
    Constant c = {.type = CONST_STRING,
                  .string_val = strdup(node->value.string_value)};
    emit_constant(compiler, c, node->line);
    break;
  }

  case AST_BOOL_LITERAL:
    emit_byte(compiler, node->value.bool_value ? OP_CONST_TRUE : OP_CONST_FALSE,
              node->line);
    break;

  case AST_ARRAY_LITERAL:
    emit_byte(compiler, OP_ARRAY_NEW, node->line);
    for (int i = 0; i < node->element_count; i++) {
      compile_expression(compiler, node->elements[i]);
      emit_byte(compiler, OP_ARRAY_PUSH, node->line);
    }
    break;

  case AST_OBJECT_LITERAL:
    emit_byte(compiler, OP_OBJECT_NEW, node->line);
    for (int i = 0; i < node->object_count; i++) {
      // Push Key
      emit_byte(compiler, OP_CONST_STR, node->line);
      Constant c = {.type = CONST_STRING,
                    .string_val = strdup(node->object_keys[i])};
      emit_constant(compiler, c, node->line);

      // Push Value
      compile_expression(compiler, node->object_values[i]);

      // Set
      emit_byte(compiler, OP_OBJECT_SET, node->line);
    }
    break;

  case AST_ARRAY_ACCESS:
    // İlk erişim için name kullanılıyor (g[0]), zincirleme için left (g[0][1])
    if (node->left) {
      compile_expression(compiler, node->left); // Zincirleme erişim
    } else if (node->name) {
      // İlk erişim - name'i identifier olarak yükle
      int slot = compiler_resolve_local(compiler, node->name);
      if (slot != -1) {
        emit_byte(compiler, OP_LOAD_LOCAL, node->line);
        emit_short(compiler, (uint16_t)slot, node->line);
      } else {
        emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
        int idx = chunk_add_string(compiler->chunk, node->name);
        emit_short(compiler, (uint16_t)idx, node->line);
        emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      }
    }
    // Index compilation: check if identifier is a variable or property name
    if (node->index->type == AST_IDENTIFIER) {
      // Check if it's a known variable (local or will be resolved as global)
      int slot = compiler_resolve_local(compiler, node->index->name);
      if (slot != -1) {
        // It's a local variable - compile as expression (loads variable value)
        compile_expression(compiler, node->index);
      } else {
        // Could be a global variable or property name
        // Try to compile as expression first (handles variables)
        // For bracket notation arr[varname], we want the variable value
        // For dot notation obj.prop, we'd want string key (but Tulpar uses
        // bracket notation)
        compile_expression(compiler, node->index);
      }
    } else {
      compile_expression(
          compiler, node->index); // Index value (literal, expression, etc.)
    }
    emit_byte(compiler, OP_ARRAY_GET, node->line); // Generic get
    break;

  case AST_IDENTIFIER: {
    int slot = compiler_resolve_local(compiler, node->name);
    if (slot != -1) {
      emit_byte(compiler, OP_LOAD_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
    } else {
      // Global variable
      emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
      int idx = chunk_add_string(compiler->chunk, node->name);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
    }
    break;
  }

  case AST_BINARY_OP: {
    // OPTIMIZATION: Try register-immediate pattern first (local OP const)
    // Pattern: variable - constant  OR  variable < constant etc.
    // These are VERY common in fibonacci and loops
    if (node->left && node->left->type == AST_IDENTIFIER && node->right &&
        node->right->type == AST_INT_LITERAL) {

      // Check if it's a local variable
      int local_slot = compiler_resolve_local(compiler, node->left->name);

      if (local_slot != -1 && node->right->value.int_value >= -32768 &&
          node->right->value.int_value <= 32767) {

        int16_t imm = (int16_t)node->right->value.int_value;

        switch (node->op) {
        case TOKEN_MINUS:
          // Pattern: n - 1, n - 2 (fibonacci!)
          // Use: LOAD_LOCAL + OP_SUB_RI (saves one stack operation)
          emit_byte(compiler, OP_LOAD_LOCAL, node->line);
          emit_short(compiler, local_slot, node->line);
          emit_byte(compiler, OP_SUB_RI, node->line);
          emit_byte(compiler, (uint8_t)0,
                    node->line); // result goes back to stack (special case)
          emit_short(compiler, imm, node->line);
          break;

        case TOKEN_LESS:
          // Pattern: n < 1, i < 1000000 (loop conditions!)
          emit_byte(compiler, OP_LT_RI, node->line);
          emit_byte(compiler, (uint8_t)local_slot, node->line);
          emit_short(compiler, imm, node->line);
          break;

        case TOKEN_LESS_EQUAL:
          // Pattern: n <= 1 (fibonacci base case!)
          // Implement as: n < (imm + 1) if imm < 32767
          if (imm < 32767) {
            emit_byte(compiler, OP_LT_RI, node->line);
            emit_byte(compiler, (uint8_t)local_slot, node->line);
            emit_short(compiler, imm + 1, node->line);
          } else {
            // Fallback to stack-based
            compile_expression(compiler, node->left);
            compile_expression(compiler, node->right);
            emit_byte(compiler, OP_LE_INT, node->line);
          }
          break;

        default:
          // Other ops: fallback to stack-based
          goto stack_based_binary;
        }
        break; // Exit AST_BINARY_OP case
      }
    }

  stack_based_binary:
    compile_expression(compiler, node->left);
    compile_expression(compiler, node->right);

    // Check if we can use type-specialized opcodes
    int use_int_ops = is_int_binary_op(compiler, node);

    switch (node->op) {
    case TOKEN_PLUS:
      emit_byte(compiler, use_int_ops ? OP_ADD_INT : OP_ADD, node->line);
      break;
    case TOKEN_MINUS:
      emit_byte(compiler, use_int_ops ? OP_SUB_INT : OP_SUB, node->line);
      break;
    case TOKEN_MULTIPLY:
      emit_byte(compiler, use_int_ops ? OP_MUL_INT : OP_MUL, node->line);
      break;
    case TOKEN_DIVIDE:
      emit_byte(compiler, use_int_ops ? OP_DIV_INT : OP_DIV, node->line);
      break;
    case TOKEN_EQUAL:
      emit_byte(compiler, use_int_ops ? OP_EQ_INT : OP_EQ, node->line);
      break;
    case TOKEN_NOT_EQUAL:
      emit_byte(compiler, use_int_ops ? OP_NE_INT : OP_NE, node->line);
      break;
    case TOKEN_LESS:
      emit_byte(compiler, use_int_ops ? OP_LT_INT : OP_LT, node->line);
      break;
    case TOKEN_LESS_EQUAL:
      emit_byte(compiler, use_int_ops ? OP_LE_INT : OP_LE, node->line);
      break;
    case TOKEN_GREATER:
      emit_byte(compiler, use_int_ops ? OP_GT_INT : OP_GT, node->line);
      break;
    case TOKEN_GREATER_EQUAL:
      emit_byte(compiler, use_int_ops ? OP_GE_INT : OP_GE, node->line);
      break;
    case TOKEN_AND: {
      // Short-circuit AND: if left is false, skip right
      int end_jump = emit_jump(compiler, OP_AND, node->line);
      // If we get here, left was true, result is now 'right' value
      patch_jump(compiler, end_jump);
      break;
    }
    case TOKEN_OR: {
      // Short-circuit OR: if left is true, skip right
      int end_jump = emit_jump(compiler, OP_OR, node->line);
      // If we get here, left was false, result is now 'right' value
      patch_jump(compiler, end_jump);
      break;
    }
    // Note: Modulo operator (%) not yet in lexer - fallback to default
    default:
      printf("Compiler Error: Unsupported binary operator\n");
      break;
    }
    break;
  }

  case AST_UNARY_OP: {
    compile_expression(compiler, node->left);

    switch (node->op) {
    case TOKEN_MINUS:
      emit_byte(compiler, OP_NEG, node->line);
      break;
    case TOKEN_BANG:
      emit_byte(compiler, OP_NOT, node->line);
      break;
    default:
      printf("Compiler Error: Unsupported unary operator\n");
      break;
    }
    break;
  }

  case AST_FUNCTION_CALL: {
    // Check for built-ins first
    if (strcmp(node->name, "print") == 0) {
      // Print expects arguments on stack
      for (int i = 0; i < node->argument_count; i++) {
        compile_expression(compiler, node->arguments[i]);
      }
      emit_byte(compiler, OP_PRINT, node->line);
      emit_byte(compiler, (uint8_t)node->argument_count, node->line);
    } else if (strcmp(node->name, "clock") == 0 ||
               strcmp(node->name, "clock_ms") == 0) {
      // Clock expects 0 arguments
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 0, node->line); // ID 0
    } else if (strcmp(node->name, "length") == 0 ||
               strcmp(node->name, "len") == 0) {
      // Length expects 1 argument
      if (node->argument_count > 0) {
        compile_expression(compiler, node->arguments[0]);
      } else {
        emit_byte(compiler, OP_CONST_VOID, node->line); // Safety
      }
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 1, node->line); // ID 1
    }
    // Math functions
    else if (strcmp(node->name, "sqrt") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 10, node->line);
    } else if (strcmp(node->name, "sin") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 11, node->line);
    } else if (strcmp(node->name, "cos") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 12, node->line);
    } else if (strcmp(node->name, "floor") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 13, node->line);
    } else if (strcmp(node->name, "ceil") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 14, node->line);
    } else if (strcmp(node->name, "abs") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 15, node->line);
    } else if (strcmp(node->name, "tan") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 16, node->line);
    } else if (strcmp(node->name, "log") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 17, node->line);
    } else if (strcmp(node->name, "log10") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 18, node->line);
    } else if (strcmp(node->name, "socket_server") == 0) {
      compile_expression(compiler, node->arguments[0]); // host
      compile_expression(compiler, node->arguments[1]); // port
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 30, node->line);
    } else if (strcmp(node->name, "socket_accept") == 0) {
      compile_expression(compiler, node->arguments[0]); // server_fd
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 31, node->line);
    } else if (strcmp(node->name, "socket_receive") == 0 ||
               strcmp(node->name, "socket_recv") == 0) {
      compile_expression(compiler, node->arguments[0]); // client_fd
      compile_expression(compiler, node->arguments[1]); // size
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 32, node->line);
    } else if (strcmp(node->name, "socket_send") == 0) {
      compile_expression(compiler, node->arguments[0]); // client_fd
      compile_expression(compiler, node->arguments[1]); // data
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 33, node->line);
    } else if (strcmp(node->name, "socket_close") == 0) {
      compile_expression(compiler, node->arguments[0]); // fd
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 34, node->line);
    } else if (strcmp(node->name, "thread_create") == 0) {
      compile_expression(compiler, node->arguments[0]); // func_name
      compile_expression(compiler, node->arguments[1]); // arg
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 40, node->line);
    } else if (strcmp(node->name, "sleep") == 0) {
      compile_expression(compiler, node->arguments[0]); // ms
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 41, node->line);
    } else if (strcmp(node->name, "toString") == 0) {
      compile_expression(compiler, node->arguments[0]); // val
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 50, node->line);
    } else if (strcmp(node->name, "toJson") == 0) {
      compile_expression(compiler, node->arguments[0]); // val
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 51, node->line);
    } else if (strcmp(node->name, "fromJson") == 0) {
      compile_expression(compiler, node->arguments[0]); // str
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 52, node->line);
    } else if (strcmp(node->name, "toInt") == 0) {
      compile_expression(compiler, node->arguments[0]); // val
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 53, node->line);
    } else if (strcmp(node->name, "toFloat") == 0) {
      compile_expression(compiler, node->arguments[0]); // val
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 54, node->line);
    } else if (strcmp(node->name, "toBool") == 0) {
      compile_expression(compiler, node->arguments[0]); // val
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 55, node->line);
    } else if (strcmp(node->name, "exit") == 0) {
      if (node->argument_count > 0)
        compile_expression(compiler, node->arguments[0]);
      else
        emit_byte(compiler, OP_CONST_INT,
                  node->line); // default 0? No, need explicit.
      // Simplify: exit(code)
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 60, node->line);
    } else if (strcmp(node->name, "input") == 0) {
      // input() -> reads a line from stdin
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 61, node->line);
    }
    // String Utils
    else if (strcmp(node->name, "split") == 0) {
      // split(str, delim)
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 70, node->line);
    } else if (strcmp(node->name, "replace") == 0) {
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      compile_expression(compiler, node->arguments[2]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 71, node->line);
    } else if (strcmp(node->name, "substring") == 0) {
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      compile_expression(compiler, node->arguments[2]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 72, node->line);
    } else if (strcmp(node->name, "indexOf") == 0) {
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 73, node->line);
    } else if (strcmp(node->name, "contains") == 0) {
      compile_expression(compiler, node->arguments[0]); // haystack
      compile_expression(compiler, node->arguments[1]); // needle
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 77, node->line);
    } else if (strcmp(node->name, "startsWith") == 0) {
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 74, node->line);
    } else if (strcmp(node->name, "endsWith") == 0) {
      compile_expression(compiler, node->arguments[0]);
      compile_expression(compiler, node->arguments[1]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 75, node->line);
    } else if (strcmp(node->name, "trim") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 76, node->line);
    }
    // File I/O
    else if (strcmp(node->name, "write_file") == 0) {
      compile_expression(compiler, node->arguments[0]); // filename
      compile_expression(compiler, node->arguments[1]); // content
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 80, node->line);
    } else if (strcmp(node->name, "read_file") == 0) {
      compile_expression(compiler, node->arguments[0]); // filename
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 81, node->line);
    } else if (strcmp(node->name, "append_file") == 0) {
      compile_expression(compiler, node->arguments[0]); // filename
      compile_expression(compiler, node->arguments[1]); // content
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 82, node->line);
    } else if (strcmp(node->name, "file_exists") == 0) {
      compile_expression(compiler, node->arguments[0]); // filename
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 83, node->line);
    }
    // Socket I/O
    else if (strcmp(node->name, "socket_server") == 0) {
      compile_expression(compiler, node->arguments[0]); // host
      compile_expression(compiler, node->arguments[1]); // port
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 90, node->line);
    } else if (strcmp(node->name, "socket_client") == 0) {
      compile_expression(compiler, node->arguments[0]); // host
      compile_expression(compiler, node->arguments[1]); // port
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 91, node->line);
    } else if (strcmp(node->name, "socket_accept") == 0) {
      compile_expression(compiler, node->arguments[0]); // server_fd
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 92, node->line);
    } else if (strcmp(node->name, "socket_send") == 0) {
      compile_expression(compiler, node->arguments[0]); // fd
      compile_expression(compiler, node->arguments[1]); // data
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 93, node->line);
    } else if (strcmp(node->name, "socket_receive") == 0) {
      compile_expression(compiler, node->arguments[0]); // fd
      compile_expression(compiler, node->arguments[1]); // size
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 94, node->line);
    } else if (strcmp(node->name, "socket_close") == 0) {
      compile_expression(compiler, node->arguments[0]); // fd
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 95, node->line);
    } else if (strcmp(node->name, "socket_select") == 0) {
      compile_expression(compiler, node->arguments[0]); // fds array
      compile_expression(compiler, node->arguments[1]); // timeout_ms
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 96, node->line);
    }
    // --- Database (SQLite) ---
    else if (strcmp(node->name, "db_open") == 0) {
      compile_expression(compiler, node->arguments[0]); // path
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 100, node->line);
    } else if (strcmp(node->name, "db_close") == 0) {
      compile_expression(compiler, node->arguments[0]); // db handle
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 101, node->line);
    } else if (strcmp(node->name, "db_query") == 0) {
      compile_expression(compiler, node->arguments[0]); // db handle
      compile_expression(compiler, node->arguments[1]); // sql
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 102, node->line);
    } else if (strcmp(node->name, "push") == 0) {
      // Built-in push(arr, val)
      compile_expression(compiler, node->arguments[0]); // Arr
      compile_expression(compiler, node->arguments[1]); // Val
      emit_byte(compiler, OP_ARRAY_PUSH, node->line);
      // OP_ARRAY_PUSH leaves array on stack (for literal chaining).
      // For push() function, we must pop it to avoid leak.
      emit_byte(compiler, OP_POP, node->line);

      // push returns void
      emit_byte(compiler, OP_CONST_VOID, node->line);
    } else if (strcmp(node->name, "exp") == 0) {
      compile_expression(compiler, node->arguments[0]);
      emit_byte(compiler, OP_CALL_BUILTIN, node->line);
      emit_byte(compiler, 19, node->line);
    } else {
      // Regular function call
      // 1. Load function variable (callee)
      int slot = compiler_resolve_local(compiler, node->name);
      if (slot != -1) {
        emit_byte(compiler, OP_LOAD_LOCAL, node->line);
        emit_short(compiler, (uint16_t)slot, node->line);
      } else {
        emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
        int idx = chunk_add_string(compiler->chunk, node->name);
        emit_short(compiler, (uint16_t)idx, node->line);
        emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      }

      for (int i = 0; i < node->argument_count; i++) {
        compile_expression(compiler, node->arguments[i]);
      }

      // 3. Call - OPTIMIZATION: Use cached calls for recursive patterns!
      int is_recursive = 0;
      static int global_cache_id = 0; // Static counter for cache IDs

      // Check if calling the same function we're currently in (recursive call)
      if (compiler->function && compiler->function->name &&
          node->name && // Function name is in node->name for function calls
          strcmp(compiler->function->name, node->name) == 0) { // Both are char*
        is_recursive = 1;
      }

      if (is_recursive && node->argument_count <= 2) {
        // RECURSIVE CALL! Use inline cached call for massive speedup!
        if (node->argument_count == 1) {
          emit_byte(compiler, OP_CALL_1_CACHED, node->line);
          // Allocate a unique cache ID for this call site
          emit_short(compiler, global_cache_id++, node->line);
        } else if (node->argument_count == 2) {
          emit_byte(compiler, OP_CALL_2_CACHED, node->line);
          emit_short(compiler, global_cache_id++, node->line);
        } else {
          // Fallback for other arities
          emit_byte(compiler, OP_CALL_FAST, node->line);
          emit_byte(compiler, (uint8_t)node->argument_count, node->line);
        }
      } else {
        // Normal call (non-recursive or unknown)
        switch (node->argument_count) {
        case 0:
          emit_byte(compiler, OP_CALL_0, node->line);
          break;
        case 1:
          emit_byte(compiler, OP_CALL_1, node->line);
          break;
        case 2:
          emit_byte(compiler, OP_CALL_2, node->line);
          break;
        default:
          emit_byte(compiler, OP_CALL_FAST, node->line);
          emit_byte(compiler, (uint8_t)node->argument_count, node->line);
          break;
        }
      }
    }

    // Pop the return value (statement, not expression)
    // WAIT: This function is inside compile_expression!
    // AST_FUNCTION_CALL is an expression. It should verify result value.
    // However, compile_expression handles expressions.
    // If AST_FUNCTION_CALL was called from compile_statement (via expression
    // statement wrapper?), compile_statement(AST_FUNCTION_CALL) calls
    // compile_expression(AST_FUNCTION_CALL) then POP.

    // Check line 439 (AST_FUNCTION_CALL inside compile_statement).
    // Yes, compile_statement calls compile_expression then POP.
    // So compile_expression is responsible for pushing result.
    // OP_CALL pushes result. OP_CALL_BUILTIN pushes result. OP_PRINT does NOT
    // push result? OP_PRINT implementation: vm->stack_top -= argc. No push. So
    // print returns "void" implicitly (nothing pushed). But compile_statement
    // emits POP. If nothing pushed, POP will underflow stack!

    // We should ensure OP_PRINT pushes NULL/Void or compile_statement handles
    // it. Standard approach: print returns nil/void. Let's make OP_PRINT push
    // VOID.

    // OR change compile_statement logic for print?
    // print is an expression in AST?
    // Yes.

    // I should modify vm.c OP_PRINT to push VOID.

    break;
  }

  default:
    printf("Compiler Warning: Unhandled expression type %d\n", node->type);
    break;
  }
}

// ============================================================================
// STATEMENT COMPILATION
// ============================================================================

void compile_statement(Compiler *compiler, ASTNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_PROGRAM:
  case AST_BLOCK: {
    compiler_begin_scope(compiler);
    for (int i = 0; i < node->statement_count; i++) {
      compile_statement(compiler, node->statements[i]);
    }
    compiler_end_scope(compiler);
    break;
  }

  case AST_VARIABLE_DECL: {
    // Compile initializer if present
    if (node->right) {
      compile_expression(compiler, node->right);
    } else if (node->data_type == TYPE_CUSTOM) {
      // Custom type (like Point) - create empty object instance
      emit_byte(compiler, OP_OBJECT_NEW, node->line);
    } else {
      emit_byte(compiler, OP_CONST_VOID, node->line);
    }

    // Determine local type for specialization
    LocalType local_type = LOCAL_TYPE_UNKNOWN;
    switch (node->data_type) {
    case TYPE_INT:
      local_type = LOCAL_TYPE_INT;
      break;
    case TYPE_FLOAT:
      local_type = LOCAL_TYPE_FLOAT;
      break;
    case TYPE_STRING:
      local_type = LOCAL_TYPE_STRING;
      break;
    case TYPE_BOOL:
      local_type = LOCAL_TYPE_BOOL;
      break;
    default:
      local_type = LOCAL_TYPE_UNKNOWN;
      break;
    }

    if (compiler->scope_depth == 0) {
      // Global variable
      emit_byte(compiler, OP_STORE_GLOBAL, node->line);
      int idx = chunk_add_string(compiler->chunk, node->name);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      emit_byte(compiler, OP_POP, node->line);
    } else {
      // Local variable - value stays on stack, will be popped by scope end
      int slot = compiler_add_local_typed(compiler, node->name, local_type);
      emit_byte(compiler, OP_STORE_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
      // NOT: OP_POP burada yapılmaz! Değer stack'te kalmalı.
      // compiler_end_scope local'ları otomatik temizler.
    }
    break;
  }

  case AST_TYPE_DECL:
    // Ignore type declarations for now (Phase 2 VM is dynamic/generic objects)
    break;

  case AST_ASSIGNMENT: {
    // OPTIMIZATION 1: Detect "var = var + 1" pattern (common counter)
    if (node->name && node->right && node->right->type == AST_BINARY_OP &&
        node->right->op == TOKEN_PLUS && node->right->left &&
        node->right->left->type == AST_IDENTIFIER &&
        strcmp(node->right->left->name, node->name) == 0 &&
        node->right->right && node->right->right->type == AST_INT_LITERAL &&
        node->right->right->value.int_value == 1) {

      // Pattern detected: var = var + 1
      int slot = compiler_resolve_local(compiler, node->name);
      if (slot != -1) {
        // Use ultra-fast increment
        emit_byte(compiler, OP_INC_LOCAL_FAST, node->line);
        emit_short(compiler, (uint16_t)slot, node->line);
        emit_byte(compiler, OP_POP, node->line); // Maintain stack balance
        break;                                   // Done, skip normal path
      }
    }

    // OPTIMIZATION 2: Detect "sum = sum + x" pattern for superinstruction
    if (node->name && node->right && node->right->type == AST_BINARY_OP &&
        node->right->op == TOKEN_PLUS && node->right->left &&
        node->right->left->type == AST_IDENTIFIER &&
        strcmp(node->right->left->name, node->name) == 0) {

      // Pattern detected: var = var + expr
      int slot = compiler_resolve_local(compiler, node->name);
      if (slot != -1) {
        // Compile the right operand (expr in "var + expr")
        compile_expression(compiler, node->right->right);
        // Use superinstruction: LOAD_ADD_STORE
        emit_byte(compiler, OP_LOAD_ADD_STORE, node->line);
        emit_short(compiler, (uint16_t)slot, node->line);
        emit_byte(compiler, OP_POP, node->line); // Maintain stack balance
        break;                                   // Done, skip normal path
      }
    }

    if (node->name) {
      compile_expression(compiler,
                         node->right); // Compile RHS for variable assignment
      int slot = compiler_resolve_local(compiler, node->name);
      if (slot != -1) {
        emit_byte(compiler, OP_STORE_LOCAL, node->line);
        emit_short(compiler, (uint16_t)slot, node->line);
      } else {
        emit_byte(compiler, OP_STORE_GLOBAL, node->line);
        int idx = chunk_add_string(compiler->chunk, node->name);
        emit_short(compiler, (uint16_t)idx, node->line);
        emit_short(compiler, 0xFFFF, node->line);
      }
    } else if (node->left && node->left->type == AST_ARRAY_ACCESS) {
      // arr[index] = value
      // Stack: [value]
      // We need: [arr, index, value] for OP_ARRAY_SET.
      // But we already compiled value.
      // So:
      // 1. Compile arr (push)
      // 2. Compile index (push)
      // 3. Swap/Rotate?
      // [val, arr, idx]
      // VM Opcode: OP_ARRAY_SET expects [arr, idx, val] or similar?
      // Let's implement OP_ARRAY_SET to expect [arr, idx, val].
      // So we need to compile arr and index BEFORE value.
      // But `compile_statement` runs `compile_expression(node->right)` first!
      // This order is problematic for array set.
      // Unless we rearrange.

      // REWRITE:
      // 1. Compile Left (Array + Index)
      // İlk erişim için name kullanılıyor (g[0] = x), zincirleme için left
      // (g[0][1] = x)
      if (node->left->left) {
        compile_expression(compiler, node->left->left); // Zincirleme erişim
      } else if (node->left->name) {
        // İlk erişim - name'i identifier olarak yükle
        int slot = compiler_resolve_local(compiler, node->left->name);
        if (slot != -1) {
          emit_byte(compiler, OP_LOAD_LOCAL, node->line);
          emit_short(compiler, (uint16_t)slot, node->line);
        } else {
          emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
          int idx = chunk_add_string(compiler->chunk, node->left->name);
          emit_short(compiler, (uint16_t)idx, node->line);
          emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
        }
      }
      // Compile the index expression normally
      // Note: For object property access with identifier like obj.property,
      // the parser should create a different AST structure (AST_MEMBER_ACCESS)
      // Here we just compile whatever index expression is given
      compile_expression(compiler, node->left->index); // Index
      // 2. Compile Value
      compile_expression(compiler, node->right); // Val
      // Stack: [Arr, Index, Val]
      emit_byte(compiler, OP_ARRAY_SET, node->line);
      // OP_ARRAY_SET pops arr, idx, val and pushes val as result
      // We need to pop the result since this is a statement
      emit_byte(compiler, OP_POP, node->line);
      return; // Done
    }

    emit_byte(compiler, OP_POP, node->line); // Discard result
    break;
  }

  case AST_IF: {
    // Compile condition
    compile_expression(compiler, node->condition);

    // Jump over then-branch if false
    int then_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
    emit_byte(compiler, OP_POP, node->line); // Pop condition

    // Compile then-branch
    compile_statement(compiler, node->then_branch);

    // Jump over else-branch
    int else_jump = emit_jump(compiler, OP_JUMP, node->line);

    // Patch then-jump
    patch_jump(compiler, then_jump);
    emit_byte(compiler, OP_POP, node->line); // Pop condition

    // Compile else-branch if present
    if (node->else_branch) {
      compile_statement(compiler, node->else_branch);
    }

    // Patch else-jump
    patch_jump(compiler, else_jump);
    break;
  }

  case AST_WHILE: {
    // Save outer loop state
    int outer_loop_start = compiler->loop_start;
    int outer_is_for_loop = compiler->is_for_loop;
    int outer_break_count = compiler->break_count;
    int outer_continue_count = compiler->continue_count;

    compiler->loop_start = compiler->chunk->code_length;
    compiler->is_for_loop = 0; // While loop - continue jumps back directly
    compiler->break_count = 0;
    compiler->continue_count = 0;
    compiler->loop_depth++;

    // Compile condition
    compile_expression(compiler, node->condition);

    // Jump out if false
    int exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
    emit_byte(compiler, OP_POP, node->line);

    // Compile body
    compile_statement(compiler, node->body);

    // Loop back
    emit_loop(compiler, compiler->loop_start, node->line);

    // Patch exit jump
    patch_jump(compiler, exit_jump);
    emit_byte(compiler, OP_POP, node->line);

    // Patch all break jumps (to after loop)
    for (int i = 0; i < compiler->break_count; i++) {
      patch_jump(compiler, compiler->break_jumps[i]);
    }

    // Note: Continue jumps for while use emit_loop directly in AST_CONTINUE

    // Restore outer loop state
    compiler->loop_start = outer_loop_start;
    compiler->is_for_loop = outer_is_for_loop;
    compiler->break_count = outer_break_count;
    compiler->continue_count = outer_continue_count;
    compiler->loop_depth--;
    break;
  }

  case AST_FOR: {
    compiler_begin_scope(compiler);

    // Compile init
    if (node->init) {
      compile_statement(compiler, node->init);
    }

    // Save outer loop state
    int outer_loop_start = compiler->loop_start;
    int outer_is_for_loop = compiler->is_for_loop;
    int outer_break_count = compiler->break_count;
    int outer_continue_count = compiler->continue_count;

    compiler->loop_start = compiler->chunk->code_length;
    compiler->is_for_loop =
        1; // For loop - continue needs forward jump to increment
    compiler->break_count = 0;
    compiler->continue_count = 0;
    compiler->loop_depth++;

    // Compile condition
    int exit_jump = -1;
    if (node->condition) {
      compile_expression(compiler, node->condition);
      exit_jump = emit_jump(compiler, OP_JUMP_IF_FALSE, node->line);
      emit_byte(compiler, OP_POP, node->line);
    }

    // Compile body
    compile_statement(compiler, node->body);

    // Patch continue jumps to here (before increment)
    for (int i = 0; i < compiler->continue_count; i++) {
      patch_jump(compiler, compiler->continue_jumps[i]);
    }
    compiler->continue_count = 0;

    // Compile increment
    if (node->increment) {
      compile_statement(compiler, node->increment);
    }

    // Loop back
    emit_loop(compiler, compiler->loop_start, node->line);

    // Patch exit jump
    if (exit_jump != -1) {
      patch_jump(compiler, exit_jump);
      emit_byte(compiler, OP_POP, node->line);
    }

    // Patch all break jumps
    for (int i = 0; i < compiler->break_count; i++) {
      patch_jump(compiler, compiler->break_jumps[i]);
    }

    // Restore outer loop state
    compiler->loop_start = outer_loop_start;
    compiler->is_for_loop = outer_is_for_loop;
    compiler->break_count = outer_break_count;
    compiler->continue_count = outer_continue_count;
    compiler->loop_depth--;

    compiler_end_scope(compiler);
    break;
  }

  case AST_BREAK: {
    if (compiler->loop_depth == 0) {
      fprintf(stderr, "Compiler Error: 'break' outside of loop at line %d\n",
              node->line);
    } else {
      // Emit jump and save offset for later patching
      int jump = emit_jump(compiler, OP_JUMP, node->line);
      if (compiler->break_count < 64) {
        compiler->break_jumps[compiler->break_count++] = jump;
      }
    }
    break;
  }

  case AST_CONTINUE: {
    if (compiler->loop_depth == 0) {
      fprintf(stderr, "Compiler Error: 'continue' outside of loop at line %d\n",
              node->line);
    } else if (compiler->is_for_loop) {
      // For loop: emit forward jump - will be patched to increment
      int jump = emit_jump(compiler, OP_JUMP, node->line);
      if (compiler->continue_count < 64) {
        compiler->continue_jumps[compiler->continue_count++] = jump;
      }
    } else {
      // While loop: jump back to condition start
      emit_loop(compiler, compiler->loop_start, node->line);
    }
    break;
  }

  case AST_RETURN: {
    if (node->return_value) {
      compile_expression(compiler, node->return_value);

      // Tail Call Optimization Detection
      // Check if the last instruction was OP_CALL
      if (compiler->chunk->code_length >= 2 &&
          compiler->chunk->code[compiler->chunk->code_length - 2] == OP_CALL) {
        // Optimization: Replace OP_CALL with OP_TAIL_CALL
        compiler->chunk->code[compiler->chunk->code_length - 2] = OP_TAIL_CALL;
        // Do NOT emit OP_RETURN; OP_TAIL_CALL handles the jump/return
      } else {
        // Use ultra-fast return
        emit_byte(compiler, OP_RETURN_FAST, node->line);
      }
    } else {
      emit_return(compiler, node->line);
    }
    break;
  }

  case AST_COMPOUND_ASSIGN: {
    // 1. Load variable
    int slot = compiler_resolve_local(compiler, node->name);
    int global_idx = -1;

    if (slot != -1) {
      emit_byte(compiler, OP_LOAD_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
    } else {
      emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
      global_idx = chunk_add_string(compiler->chunk, node->name);
      emit_short(compiler, (uint16_t)global_idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
    }

    // 2. Compile right-hand side
    compile_expression(compiler, node->right);

    // 3. Perform operation
    switch (node->op) {
    case TOKEN_PLUS_EQUAL:
      emit_byte(compiler, OP_ADD, node->line);
      break;
    case TOKEN_MINUS_EQUAL:
      emit_byte(compiler, OP_SUB, node->line);
      break;
    case TOKEN_MULTIPLY_EQUAL:
      emit_byte(compiler, OP_MUL, node->line);
      break;
    case TOKEN_DIVIDE_EQUAL:
      emit_byte(compiler, OP_DIV, node->line);
      break;
    default:
      break; // Should not happen
    }

    // 4. Store result
    if (slot != -1) {
      emit_byte(compiler, OP_STORE_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
    } else {
      emit_byte(compiler, OP_STORE_GLOBAL, node->line);
      emit_short(compiler, (uint16_t)global_idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
    }

    // 5. Pop result (statement)
    emit_byte(compiler, OP_POP, node->line);
    break;
  }

  case AST_INCREMENT: {
    int slot = compiler_resolve_local(compiler, node->name);
    if (slot != -1) {
      // OPTIMIZED: Use INC_LOCAL for local variables (single instruction!)
      emit_byte(compiler, OP_INC_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
    } else {
      emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
      int idx = chunk_add_string(compiler->chunk, node->name);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      emit_byte(compiler, OP_INC, node->line);
      emit_byte(compiler, OP_STORE_GLOBAL, node->line);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      emit_byte(compiler, OP_POP, node->line);
    }
    break;
  }

  case AST_DECREMENT: {
    int slot = compiler_resolve_local(compiler, node->name);
    if (slot != -1) {
      // OPTIMIZED: Use DEC_LOCAL for local variables (single instruction!)
      emit_byte(compiler, OP_DEC_LOCAL, node->line);
      emit_short(compiler, (uint16_t)slot, node->line);
    } else {
      emit_byte(compiler, OP_LOAD_GLOBAL, node->line);
      int idx = chunk_add_string(compiler->chunk, node->name);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      emit_byte(compiler, OP_DEC, node->line);
      emit_byte(compiler, OP_STORE_GLOBAL, node->line);
      emit_short(compiler, (uint16_t)idx, node->line);
      emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot
      emit_byte(compiler, OP_POP, node->line);
    }
    break;
  }

  case AST_FUNCTION_CALL: {
    compile_expression(compiler, node);
    // Pop the return value (statement, not expression)
    emit_byte(compiler, OP_POP, node->line);
    break;
  }

  case AST_FUNCTION_DECL: {
    // 1. Create compiled function object
    CompiledFunction *func =
        (CompiledFunction *)malloc(sizeof(CompiledFunction));
    func->name = strdup(node->name);
    func->arity = node->param_count;
    func->local_count = 0; // Will be set after compilation
    // func->chunk will be created by the new compiler

    // 2. Create new compiler for this function
    Compiler *func_compiler = compiler_create();
    func_compiler->enclosing = compiler;
    func_compiler->function = func; // Link function to compiler

    // 3. Compile parameters (add as locals)
    compiler_begin_scope(func_compiler);

    // Reserve slot 0 for the function setup (or 'this')
    // We add a dummy local that cannot be referenced by name
    compiler_add_local(func_compiler, "");

    for (int i = 0; i < node->param_count; i++) {
      // Add argument to locals with type information if available
      ASTNode *param = node->parameters[i];
      LocalType param_type = LOCAL_TYPE_UNKNOWN;

      // If parameter has type annotation, use it
      if (param->data_type == TYPE_INT)
        param_type = LOCAL_TYPE_INT;
      else if (param->data_type == TYPE_FLOAT)
        param_type = LOCAL_TYPE_FLOAT;
      else if (param->data_type == TYPE_STRING)
        param_type = LOCAL_TYPE_STRING;
      else if (param->data_type == TYPE_BOOL)
        param_type = LOCAL_TYPE_BOOL;

      compiler_add_local_typed(func_compiler, param->name, param_type);
      // We don't need to emit STORE because arguments are already on stack when
      // called
    }

    // 4. Compile function body
    if (node->body->type == AST_BLOCK) {
      // We manually compile block content to avoid creating an extra scope
      // level if desired, but strictly speaking, arguments are in top scope of
      // function. The block will create another scope. That's fine.
      compile_statement(func_compiler, node->body);
    } else {
      compile_statement(func_compiler, node->body);
    }

    // Implicit return at end
    emit_byte(func_compiler, OP_RETURN_VOID, node->line);

    // 5. Finalize function
    func->chunk = func_compiler->chunk;
    func->local_count = func_compiler->local_count;

    // Note: We free the PROXY compiler struct, but we KEEP the chunk (it
    // belongs to func now) compiler_free usually frees the chunk too if we are
    // not careful? Let's check compiler_free: "Note: chunk is returned to
    // caller, not freed here" -> OK.
    compiler_free(func_compiler);

    // 6. Add function to constant pool
    Constant c;
    c.type = CONST_FUNCTION;
    c.func_val = func;

    int index = chunk_add_constant(compiler->chunk, c);

    // 7. Emit code to load function
    emit_byte(compiler, OP_CONST_FUNC, node->line);
    emit_short(compiler, (uint16_t)index, node->line);

    // 8. Define as global variable (functions are global for now)
    emit_byte(compiler, OP_STORE_GLOBAL, node->line);
    int name_idx = chunk_add_string(compiler->chunk, node->name);
    emit_short(compiler, (uint16_t)name_idx, node->line);
    emit_short(compiler, 0xFFFF, node->line); // Inline Cache Slot

    break;
  }

  case AST_IMPORT:
    emit_byte(compiler, OP_IMPORT, node->line);
    // Expect node->name to be the filename string? Or node->value.string_value?
    // AST_IMPORT usually: import "file"; -> node name might be filename.
    // Let's use node->name assuming parser puts it there.
    // Also valid: import name = "file" (variable decl).
    // Simple import "str".
    int idx = chunk_add_string(compiler->chunk, node->value.string_value);
    emit_short(compiler, (uint16_t)idx, node->line);
    break;

  case AST_TRY_CATCH: {
    // 1. Emit OP_TRY <offset>
    int catch_jump = emit_jump(compiler, OP_TRY, node->line);

    // 2. Compile Try Block
    compile_statement(compiler, node->try_block);

    // 3. Pop Try (Success path only)
    emit_byte(compiler, OP_POP_TRY, node->line);

    // 4. Finally (Success path)
    if (node->finally_block) {
      compile_statement(compiler, node->finally_block);
    }

    // 5. Jump over Catch
    int end_jump = emit_jump(compiler, OP_JUMP, node->line);

    // 6. Start Catch
    patch_jump(compiler, catch_jump);
    compiler_begin_scope(compiler);

    // 7. Bind Exception to Variable
    if (node->catch_var) {
      compiler_add_local(compiler, node->catch_var);
      // Exception is already on stack, so it becomes this local
    } else {
      // If no variable captured, pop the exception?
      // OP_TRY/THROW logic pushes exception.
      // If catch block doesn't capture it (e.g. catch { ... }),
      // we should pop it to keep stack clean?
      // But user might not use it.
      // If we don't bind local, stack has extra value.
      // We should emit OP_POP if catch_var is null.
      emit_byte(compiler, OP_POP, node->line);
    }

    // 8. Compile Catch Block
    if (node->catch_block) {
      compile_statement(compiler, node->catch_block);
    }

    // 9. End Catch Scope (Pops locals, including catch_var if any)
    compiler_end_scope(compiler);
    // If we manually OP_POP above, catch scope has 0 locals?
    // add_local increments local_count.
    // end_scope generates OP_POPs for locals.
    // If we didn't add local, we manually emitted OP_POP.
    // Logic:
    // if catch_var: add_local (end_scope pops it).
    // else: emit OP_POP (exception popped). end_scope pops nothing.
    // Correct.

    // 10. Finally (Catch path)
    if (node->finally_block) {
      compile_statement(compiler, node->finally_block);
    }

    // 11. End
    patch_jump(compiler, end_jump);
    break;
  }

  case AST_THROW: {
    compile_expression(compiler, node->throw_expr);
    emit_byte(compiler, OP_THROW, node->line);
    break;
  }

  default:
    printf("Compiler Warning: Unhandled statement type %d\n", node->type);
    break;
  }
}

// ============================================================================
// MAIN COMPILE FUNCTION
// ============================================================================

Chunk *compile(ASTNode *ast) {
  Compiler *compiler = compiler_create();
  current = compiler;

  // Compile the AST
  if (ast->type == AST_PROGRAM) {
    for (int i = 0; i < ast->statement_count; i++) {
      compile_statement(compiler, ast->statements[i]);
    }
  } else {
    compile_statement(compiler, ast);
  }

  // Emit halt
  emit_byte(compiler, OP_RETURN_VOID, 0);

  Chunk *chunk = compiler->chunk;
  compiler_free(compiler);
  current = NULL;

  return chunk;
}

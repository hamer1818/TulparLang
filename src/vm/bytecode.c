#include "bytecode.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// CHUNK IMPLEMENTATION
// ============================================================================

Chunk *chunk_create() {
  Chunk *chunk = (Chunk *)malloc(sizeof(Chunk));

  chunk->code = NULL;
  chunk->code_length = 0;
  chunk->code_capacity = 0;

  chunk->constants = NULL;
  chunk->const_count = 0;
  chunk->const_capacity = 0;

  chunk->lines = NULL;
  chunk->line_count = 0;
  chunk->line_capacity = 0;

  chunk->local_names = NULL;
  chunk->local_count = 0;

  return chunk;
}

void chunk_free(Chunk *chunk) {
  if (!chunk)
    return;

  // Free bytecode
  if (chunk->code)
    free(chunk->code);

  // Free constants
  for (int i = 0; i < chunk->const_count; i++) {
    if (chunk->constants[i].type == CONST_STRING &&
        chunk->constants[i].string_val) {
      free(chunk->constants[i].string_val);
    }
  }
  if (chunk->constants)
    free(chunk->constants);

  // Free line info
  if (chunk->lines)
    free(chunk->lines);

  // Free local names
  for (int i = 0; i < chunk->local_count; i++) {
    if (chunk->local_names[i])
      free(chunk->local_names[i]);
  }
  if (chunk->local_names)
    free(chunk->local_names);

  free(chunk);
}

void chunk_write(Chunk *chunk, uint8_t byte, int line) {
  // Grow code array if needed
  if (chunk->code_length >= chunk->code_capacity) {
    chunk->code_capacity =
        chunk->code_capacity == 0 ? 256 : chunk->code_capacity * 2;
    chunk->code = (uint8_t *)realloc(chunk->code, chunk->code_capacity);
  }

  chunk->code[chunk->code_length++] = byte;

  // Track line numbers (RLE encoding for efficiency)
  if (chunk->line_count > 0 && chunk->lines[chunk->line_count - 1] == line) {
    // Same line, no need to add
  } else {
    if (chunk->line_count >= chunk->line_capacity) {
      chunk->line_capacity =
          chunk->line_capacity == 0 ? 64 : chunk->line_capacity * 2;
      chunk->lines =
          (int *)realloc(chunk->lines, chunk->line_capacity * sizeof(int));
    }
    chunk->lines[chunk->line_count++] = line;
  }
}

void chunk_write_int64(Chunk *chunk, long long value, int line) {
  // Write 8 bytes, little-endian
  for (int i = 0; i < 8; i++) {
    chunk_write(chunk, (value >> (i * 8)) & 0xFF, line);
  }
}

void chunk_write_int32(Chunk *chunk, int value, int line) {
  // Write 4 bytes, little-endian
  for (int i = 0; i < 4; i++) {
    chunk_write(chunk, (value >> (i * 8)) & 0xFF, line);
  }
}

void chunk_write_int16(Chunk *chunk, short value, int line) {
  // Write 2 bytes, little-endian
  chunk_write(chunk, value & 0xFF, line);
  chunk_write(chunk, (value >> 8) & 0xFF, line);
}

int chunk_add_constant(Chunk *chunk, Constant constant) {
  // Grow constant array if needed
  if (chunk->const_count >= chunk->const_capacity) {
    chunk->const_capacity =
        chunk->const_capacity == 0 ? 64 : chunk->const_capacity * 2;
    chunk->constants = (Constant *)realloc(
        chunk->constants, chunk->const_capacity * sizeof(Constant));
  }

  chunk->constants[chunk->const_count] = constant;
  return chunk->const_count++;
}

int chunk_add_string(Chunk *chunk, const char *str) {
  // Check if string already exists
  for (int i = 0; i < chunk->const_count; i++) {
    if (chunk->constants[i].type == CONST_STRING &&
        strcmp(chunk->constants[i].string_val, str) == 0) {
      return i; // Reuse existing string
    }
  }

  Constant c;
  c.type = CONST_STRING;
  c.string_val = strdup(str);
  return chunk_add_constant(chunk, c);
}

int chunk_add_int(Chunk *chunk, long long value) {
  // Check if int already exists
  for (int i = 0; i < chunk->const_count; i++) {
    if (chunk->constants[i].type == CONST_INT &&
        chunk->constants[i].int_val == value) {
      return i; // Reuse existing int
    }
  }

  Constant c;
  c.type = CONST_INT;
  c.int_val = value;
  return chunk_add_constant(chunk, c);
}

int chunk_add_float(Chunk *chunk, float value) {
  Constant c;
  c.type = CONST_FLOAT;
  c.float_val = value;
  return chunk_add_constant(chunk, c);
}

// ============================================================================
// DISASSEMBLER (for debugging)
// ============================================================================

static const char *opcode_names[] = {
    "NOP",          "POP",          "DUP",         "CONST_INT",
    "CONST_FLOAT",  "CONST_TRUE",   "CONST_FALSE", "CONST_VOID",
    "CONST_STR",    "ADD",          "SUB",         "MUL",
    "DIV",          "MOD",          "NEG",         "INC",
    "DEC",          "EQ",           "NE",          "LT",
    "LE",           "GT",           "GE",          "AND",
    "OR",           "NOT",          "LOAD_LOCAL",  "STORE_LOCAL",
    "LOAD_GLOBAL",  "STORE_GLOBAL", "JUMP",        "JUMP_IF_FALSE",
    "JUMP_IF_TRUE", "LOOP",         "CALL",        "CALL_BUILTIN",
    "RETURN",       "RETURN_VOID",  "ARRAY_NEW",   "ARRAY_PUSH",
    "ARRAY_GET",    "ARRAY_SET",    "OBJECT_NEW",  "OBJECT_GET",
    "OBJECT_SET",   "PRINT",        "HALT"};

void chunk_disassemble(Chunk *chunk, const char *name) {
  printf("== %s ==\n", name);

  int offset = 0;
  while (offset < chunk->code_length) {
    printf("%04d ", offset);

    uint8_t op = chunk->code[offset++];
    printf("%-16s", opcode_names[op]);

    switch (op) {
    case OP_CONST_INT:
    case OP_LOAD_LOCAL:
    case OP_STORE_LOCAL:
    case OP_LOAD_GLOBAL:
    case OP_STORE_GLOBAL:
    case OP_CONST_STR: {
      int idx = 0;
      for (int i = 0; i < 2; i++) {
        idx |= chunk->code[offset++] << (i * 8);
      }
      printf(" %d", idx);
      if (op == OP_CONST_STR && idx < chunk->const_count) {
        printf(" (\"%s\")", chunk->constants[idx].string_val);
      }
      break;
    }
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP: {
      int off = 0;
      for (int i = 0; i < 2; i++) {
        off |= chunk->code[offset++] << (i * 8);
      }
      printf(" -> %d", offset + off);
      break;
    }
    case OP_CALL:
    case OP_PRINT: {
      int argc = chunk->code[offset++];
      printf(" argc=%d", argc);
      break;
    }
    case OP_CALL_BUILTIN: {
      int bid = chunk->code[offset++];
      int argc = chunk->code[offset++];
      printf(" builtin=%d argc=%d", bid, argc);
      break;
    }
    default:
      break;
    }

    printf("\n");
  }
}

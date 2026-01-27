// Tulpar Native Runtime - Type-Specific Functions
// Direct native type operations without VMValue boxing

#ifndef TULPAR_NATIVE_H
#define TULPAR_NATIVE_H

#include <stdint.h>
#include <stdio.h>

// ============================================================================
// Type Definitions for Native Operations
// ============================================================================

typedef int64_t TulparInt;
typedef double TulparFloat;
typedef int8_t TulparBool;
typedef char* TulparString;

// Native Array Types (no VMValue boxing)
typedef struct {
  TulparInt *data;
  int32_t count;
  int32_t capacity;
} TulparArrayInt;

typedef struct {
  TulparFloat *data;
  int32_t count;
  int32_t capacity;
} TulparArrayFloat;

typedef struct {
  TulparString *data;
  int32_t count;
  int32_t capacity;
} TulparArrayStr;

typedef struct {
  TulparBool *data;
  int32_t count;
  int32_t capacity;
} TulparArrayBool;

// ============================================================================
// Print Functions - No VMValue, direct types
// ============================================================================

void tulpar_print_int(TulparInt val);
void tulpar_print_float(TulparFloat val);
void tulpar_print_bool(TulparBool val);
void tulpar_print_string(TulparString val);
void tulpar_print_newline(void);

// Print with format (for println)
void tulpar_println_int(TulparInt val);
void tulpar_println_float(TulparFloat val);
void tulpar_println_bool(TulparBool val);
void tulpar_println_string(TulparString val);

// ============================================================================
// String Functions
// ============================================================================

TulparString tulpar_string_new(const char *chars);
TulparString tulpar_string_concat(TulparString a, TulparString b);
TulparInt tulpar_string_length(TulparString s);
TulparString tulpar_string_substring(TulparString s, TulparInt start, TulparInt end);
TulparBool tulpar_string_equals(TulparString a, TulparString b);
void tulpar_string_free(TulparString s);

// ============================================================================
// Array Functions - Type-Specific
// ============================================================================

// Int Array
TulparArrayInt* tulpar_array_int_new(void);
void tulpar_array_int_push(TulparArrayInt *arr, TulparInt val);
TulparInt tulpar_array_int_get(TulparArrayInt *arr, TulparInt index);
void tulpar_array_int_set(TulparArrayInt *arr, TulparInt index, TulparInt val);
TulparInt tulpar_array_int_length(TulparArrayInt *arr);
void tulpar_array_int_free(TulparArrayInt *arr);

// Float Array
TulparArrayFloat* tulpar_array_float_new(void);
void tulpar_array_float_push(TulparArrayFloat *arr, TulparFloat val);
TulparFloat tulpar_array_float_get(TulparArrayFloat *arr, TulparInt index);
void tulpar_array_float_set(TulparArrayFloat *arr, TulparInt index, TulparFloat val);
TulparInt tulpar_array_float_length(TulparArrayFloat *arr);
void tulpar_array_float_free(TulparArrayFloat *arr);

// String Array
TulparArrayStr* tulpar_array_str_new(void);
void tulpar_array_str_push(TulparArrayStr *arr, TulparString val);
TulparString tulpar_array_str_get(TulparArrayStr *arr, TulparInt index);
void tulpar_array_str_set(TulparArrayStr *arr, TulparInt index, TulparString val);
TulparInt tulpar_array_str_length(TulparArrayStr *arr);
void tulpar_array_str_free(TulparArrayStr *arr);

// ============================================================================
// Type Conversion
// ============================================================================

TulparInt tulpar_float_to_int(TulparFloat val);
TulparFloat tulpar_int_to_float(TulparInt val);
TulparString tulpar_int_to_string(TulparInt val);
TulparString tulpar_float_to_string(TulparFloat val);
TulparString tulpar_bool_to_string(TulparBool val);
TulparInt tulpar_string_to_int(TulparString s);
TulparFloat tulpar_string_to_float(TulparString s);

// ============================================================================
// Math Functions - Native Types
// ============================================================================

TulparInt tulpar_abs_int(TulparInt val);
TulparFloat tulpar_abs_float(TulparFloat val);
TulparFloat tulpar_sqrt(TulparFloat val);
TulparFloat tulpar_pow(TulparFloat base, TulparFloat exp);
TulparFloat tulpar_floor(TulparFloat val);
TulparFloat tulpar_ceil(TulparFloat val);
TulparFloat tulpar_round(TulparFloat val);
TulparInt tulpar_min_int(TulparInt a, TulparInt b);
TulparInt tulpar_max_int(TulparInt a, TulparInt b);
TulparFloat tulpar_min_float(TulparFloat a, TulparFloat b);
TulparFloat tulpar_max_float(TulparFloat a, TulparFloat b);

// ============================================================================
// Input/Output
// ============================================================================

TulparString tulpar_input(void);
TulparString tulpar_read_file(TulparString path);
void tulpar_write_file(TulparString path, TulparString content);

// ============================================================================
// Time Functions
// ============================================================================

TulparFloat tulpar_clock_ms(void);

#endif // TULPAR_NATIVE_H

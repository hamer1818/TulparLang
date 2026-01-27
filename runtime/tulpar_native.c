// Tulpar Native Runtime - Implementation
// Direct native type operations without VMValue boxing

#include "tulpar_native.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

// ============================================================================
// Print Functions
// ============================================================================

void tulpar_print_int(TulparInt val) {
  printf("%lld", (long long)val);
}

void tulpar_print_float(TulparFloat val) {
  printf("%g", val);
}

void tulpar_print_bool(TulparBool val) {
  printf("%s", val ? "true" : "false");
}

void tulpar_print_string(TulparString val) {
  if (val) printf("%s", val);
}

void tulpar_print_newline(void) {
  printf("\n");
}

void tulpar_println_int(TulparInt val) {
  printf("%lld\n", (long long)val);
}

void tulpar_println_float(TulparFloat val) {
  printf("%g\n", val);
}

void tulpar_println_bool(TulparBool val) {
  printf("%s\n", val ? "true" : "false");
}

void tulpar_println_string(TulparString val) {
  if (val) printf("%s\n", val);
  else printf("\n");
}

// ============================================================================
// String Functions
// ============================================================================

TulparString tulpar_string_new(const char *chars) {
  if (!chars) return NULL;
  size_t len = strlen(chars);
  char *s = (char *)malloc(len + 1);
  memcpy(s, chars, len + 1);
  return s;
}

TulparString tulpar_string_concat(TulparString a, TulparString b) {
  if (!a && !b) return NULL;
  if (!a) return tulpar_string_new(b);
  if (!b) return tulpar_string_new(a);
  
  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  char *result = (char *)malloc(len_a + len_b + 1);
  memcpy(result, a, len_a);
  memcpy(result + len_a, b, len_b + 1);
  return result;
}

TulparInt tulpar_string_length(TulparString s) {
  return s ? (TulparInt)strlen(s) : 0;
}

TulparString tulpar_string_substring(TulparString s, TulparInt start, TulparInt end) {
  if (!s) return NULL;
  size_t len = strlen(s);
  if (start < 0) start = 0;
  if (end > (TulparInt)len) end = len;
  if (start >= end) return tulpar_string_new("");
  
  size_t sub_len = end - start;
  char *result = (char *)malloc(sub_len + 1);
  memcpy(result, s + start, sub_len);
  result[sub_len] = '\0';
  return result;
}

TulparBool tulpar_string_equals(TulparString a, TulparString b) {
  if (a == b) return 1;
  if (!a || !b) return 0;
  return strcmp(a, b) == 0 ? 1 : 0;
}

void tulpar_string_free(TulparString s) {
  if (s) free(s);
}

// ============================================================================
// Int Array Functions
// ============================================================================

TulparArrayInt* tulpar_array_int_new(void) {
  TulparArrayInt *arr = (TulparArrayInt *)malloc(sizeof(TulparArrayInt));
  arr->capacity = 8;
  arr->count = 0;
  arr->data = (TulparInt *)malloc(sizeof(TulparInt) * arr->capacity);
  return arr;
}

void tulpar_array_int_push(TulparArrayInt *arr, TulparInt val) {
  if (!arr) return;
  if (arr->count >= arr->capacity) {
    arr->capacity *= 2;
    arr->data = (TulparInt *)realloc(arr->data, sizeof(TulparInt) * arr->capacity);
  }
  arr->data[arr->count++] = val;
}

TulparInt tulpar_array_int_get(TulparArrayInt *arr, TulparInt index) {
  if (!arr || index < 0 || index >= arr->count) return 0;
  return arr->data[index];
}

void tulpar_array_int_set(TulparArrayInt *arr, TulparInt index, TulparInt val) {
  if (!arr || index < 0 || index >= arr->count) return;
  arr->data[index] = val;
}

TulparInt tulpar_array_int_length(TulparArrayInt *arr) {
  return arr ? arr->count : 0;
}

void tulpar_array_int_free(TulparArrayInt *arr) {
  if (!arr) return;
  if (arr->data) free(arr->data);
  free(arr);
}

// ============================================================================
// Float Array Functions
// ============================================================================

TulparArrayFloat* tulpar_array_float_new(void) {
  TulparArrayFloat *arr = (TulparArrayFloat *)malloc(sizeof(TulparArrayFloat));
  arr->capacity = 8;
  arr->count = 0;
  arr->data = (TulparFloat *)malloc(sizeof(TulparFloat) * arr->capacity);
  return arr;
}

void tulpar_array_float_push(TulparArrayFloat *arr, TulparFloat val) {
  if (!arr) return;
  if (arr->count >= arr->capacity) {
    arr->capacity *= 2;
    arr->data = (TulparFloat *)realloc(arr->data, sizeof(TulparFloat) * arr->capacity);
  }
  arr->data[arr->count++] = val;
}

TulparFloat tulpar_array_float_get(TulparArrayFloat *arr, TulparInt index) {
  if (!arr || index < 0 || index >= arr->count) return 0.0;
  return arr->data[index];
}

void tulpar_array_float_set(TulparArrayFloat *arr, TulparInt index, TulparFloat val) {
  if (!arr || index < 0 || index >= arr->count) return;
  arr->data[index] = val;
}

TulparInt tulpar_array_float_length(TulparArrayFloat *arr) {
  return arr ? arr->count : 0;
}

void tulpar_array_float_free(TulparArrayFloat *arr) {
  if (!arr) return;
  if (arr->data) free(arr->data);
  free(arr);
}

// ============================================================================
// String Array Functions
// ============================================================================

TulparArrayStr* tulpar_array_str_new(void) {
  TulparArrayStr *arr = (TulparArrayStr *)malloc(sizeof(TulparArrayStr));
  arr->capacity = 8;
  arr->count = 0;
  arr->data = (TulparString *)malloc(sizeof(TulparString) * arr->capacity);
  return arr;
}

void tulpar_array_str_push(TulparArrayStr *arr, TulparString val) {
  if (!arr) return;
  if (arr->count >= arr->capacity) {
    arr->capacity *= 2;
    arr->data = (TulparString *)realloc(arr->data, sizeof(TulparString) * arr->capacity);
  }
  arr->data[arr->count++] = tulpar_string_new(val);
}

TulparString tulpar_array_str_get(TulparArrayStr *arr, TulparInt index) {
  if (!arr || index < 0 || index >= arr->count) return NULL;
  return arr->data[index];
}

void tulpar_array_str_set(TulparArrayStr *arr, TulparInt index, TulparString val) {
  if (!arr || index < 0 || index >= arr->count) return;
  if (arr->data[index]) tulpar_string_free(arr->data[index]);
  arr->data[index] = tulpar_string_new(val);
}

TulparInt tulpar_array_str_length(TulparArrayStr *arr) {
  return arr ? arr->count : 0;
}

void tulpar_array_str_free(TulparArrayStr *arr) {
  if (!arr) return;
  for (int i = 0; i < arr->count; i++) {
    if (arr->data[i]) tulpar_string_free(arr->data[i]);
  }
  if (arr->data) free(arr->data);
  free(arr);
}

// ============================================================================
// Type Conversion
// ============================================================================

TulparInt tulpar_float_to_int(TulparFloat val) {
  return (TulparInt)val;
}

TulparFloat tulpar_int_to_float(TulparInt val) {
  return (TulparFloat)val;
}

TulparString tulpar_int_to_string(TulparInt val) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", (long long)val);
  return tulpar_string_new(buf);
}

TulparString tulpar_float_to_string(TulparFloat val) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%g", val);
  return tulpar_string_new(buf);
}

TulparString tulpar_bool_to_string(TulparBool val) {
  return tulpar_string_new(val ? "true" : "false");
}

TulparInt tulpar_string_to_int(TulparString s) {
  if (!s) return 0;
  return (TulparInt)strtoll(s, NULL, 10);
}

TulparFloat tulpar_string_to_float(TulparString s) {
  if (!s) return 0.0;
  return strtod(s, NULL);
}

// ============================================================================
// Math Functions
// ============================================================================

TulparInt tulpar_abs_int(TulparInt val) {
  return val < 0 ? -val : val;
}

TulparFloat tulpar_abs_float(TulparFloat val) {
  return fabs(val);
}

TulparFloat tulpar_sqrt(TulparFloat val) {
  return sqrt(val);
}

TulparFloat tulpar_pow(TulparFloat base, TulparFloat exp) {
  return pow(base, exp);
}

TulparFloat tulpar_floor(TulparFloat val) {
  return floor(val);
}

TulparFloat tulpar_ceil(TulparFloat val) {
  return ceil(val);
}

TulparFloat tulpar_round(TulparFloat val) {
  return round(val);
}

TulparInt tulpar_min_int(TulparInt a, TulparInt b) {
  return a < b ? a : b;
}

TulparInt tulpar_max_int(TulparInt a, TulparInt b) {
  return a > b ? a : b;
}

TulparFloat tulpar_min_float(TulparFloat a, TulparFloat b) {
  return a < b ? a : b;
}

TulparFloat tulpar_max_float(TulparFloat a, TulparFloat b) {
  return a > b ? a : b;
}

// ============================================================================
// Input/Output
// ============================================================================

TulparString tulpar_input(void) {
  char buf[1024];
  if (fgets(buf, sizeof(buf), stdin)) {
    // Remove trailing newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
      buf[len - 1] = '\0';
    }
    return tulpar_string_new(buf);
  }
  return tulpar_string_new("");
}

TulparString tulpar_read_file(TulparString path) {
  if (!path) return NULL;
  
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  
  char *content = (char *)malloc(size + 1);
  fread(content, 1, size, f);
  content[size] = '\0';
  fclose(f);
  
  return content;
}

void tulpar_write_file(TulparString path, TulparString content) {
  if (!path) return;
  
  FILE *f = fopen(path, "wb");
  if (!f) return;
  
  if (content) {
    fwrite(content, 1, strlen(content), f);
  }
  fclose(f);
}

// ============================================================================
// Time Functions
// ============================================================================

TulparFloat tulpar_clock_ms(void) {
#ifdef _WIN32
  LARGE_INTEGER freq, count;
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&count);
  return (TulparFloat)count.QuadPart * 1000.0 / (TulparFloat)freq.QuadPart;
#else
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (TulparFloat)tv.tv_sec * 1000.0 + (TulparFloat)tv.tv_usec / 1000.0;
#endif
}

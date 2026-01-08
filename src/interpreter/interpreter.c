#include "interpreter.h"
#include "../../lib/sqlite3/sqlite3.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Thread Arguments Structure
typedef struct {
  Interpreter *parent_interp;
  char *func_name;
  Value *arg;
} ThreadArgs;

// Forward Declarations
static Interpreter *interpreter_clone(Interpreter *src);
#ifdef _WIN32
DWORD WINAPI thread_entry_point(LPVOID lpParam);
#else
void *thread_entry_point(void *arg);
#endif

// SQLite Callback
static int tulpar_sqlite_callback(void *data, int argc, char **argv,
                                  char **azColName) {
  // printf("DEBUG: Callback called with %d columns\n", argc);
  // fflush(stdout);
  Value *list = (Value *)data;
  Value *row_obj = value_create_object();

  for (int i = 0; i < argc; i++) {
    // printf("DEBUG: Column: %s\n", azColName[i]);
    // fflush(stdout);
    if (argv[i]) {
      hash_table_set(row_obj->data.object_val, azColName[i],
                     value_create_string(argv[i]));
    } else {
      hash_table_set(row_obj->data.object_val, azColName[i],
                     value_create_string("NULL"));
    }
  }

  array_push(list->data.array_val, row_obj);
  value_free(row_obj); // Fix memory leak as array_push copies the value
  return 0;
}

// ============================================================================
static int utf8_char_length(unsigned char c) {
  if ((c & 0x80) == 0) {
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    return 2;
  }
  if ((c & 0xF0) == 0xE0) {
    return 3;
  }
  if ((c & 0xF8) == 0xF0) {
    return 4;
  }
  return 1;
}

// Returns the number of characters in a UTF-8 string
static int utf8_strlen_cp(const char *str) {
  int length = 0;
  int i = 0;

  while (str[i] != '\0') {
    int char_len = utf8_char_length((unsigned char)str[i]);
    i += char_len;
    length++;
  }

  return length;
}

// Returns the character at the specified index in a UTF-8 string (as a string)
static char *utf8_char_at(const char *str, int index) {
  int i = 0;
  int current = 0;

  while (str[i] != '\0' && current < index) {
    int char_len = utf8_char_length((unsigned char)str[i]);
    i += char_len;
    current++;
  }

  if (str[i] == '\0') {
    return NULL; // Out of bounds
  }

  int char_len = utf8_char_length((unsigned char)str[i]);
  char *buffer = (char *)malloc((size_t)char_len + 1);
  memcpy(buffer, str + i, (size_t)char_len);
  buffer[char_len] = '\0';
  return buffer;
}

// ============================================================================
// FILE I/O HELPERS
// ============================================================================

static char *read_file_content(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *content = (char *)malloc(size + 1);
  if (content) {
    fread(content, 1, size, f);
    content[size] = '\0';
  }
  fclose(f);
  return content;
}

static int write_file_content(const char *path, const char *content) {
  FILE *f = fopen(path, "wb");
  if (!f)
    return 0;
  fprintf(f, "%s", content);
  fclose(f);
  return 1;
}

static int append_file_content(const char *path, const char *content) {
  FILE *f = fopen(path, "ab");
  if (!f)
    return 0;
  fprintf(f, "%s", content);
  fclose(f);
  return 1;
}

static int file_exists_check(const char *path) {
  FILE *f = fopen(path, "r");
  if (f) {
    fclose(f);
    return 1;
  }
  return 0;
}

// ============================================================================
// STRING HELPERS
// ============================================================================

static char *string_trim(const char *s) {
  while (isspace((unsigned char)*s))
    s++;
  if (*s == 0)
    return strdup("");
  const char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end))
    end--;
  int len = (int)(end - s) + 1;
  char *out = (char *)malloc(len + 1);
  memcpy(out, s, len);
  out[len] = '\0';
  return out;
}

static char *string_replace(const char *s, const char *old,
                            const char *new_val) {
  char *result;
  int i, cnt = 0;
  int newWlen = strlen(new_val);
  int oldWlen = strlen(old);

  if (oldWlen == 0)
    return strdup(s);

  for (i = 0; s[i] != '\0'; i++) {
    if (strstr(&s[i], old) == &s[i]) {
      cnt++;
      i += oldWlen - 1;
    }
  }

  result = (char *)malloc(i + cnt * (newWlen - oldWlen) + 1);

  i = 0;
  while (*s) {
    if (strstr(s, old) == s) {
      strcpy(&result[i], new_val);
      i += newWlen;
      s += oldWlen;
    } else
      result[i++] = *s++;
  }

  result[i] = '\0';
  return result;
}

static char *string_to_upper(const char *s) {
  char *out = strdup(s);
  for (int i = 0; out[i]; i++) {
    out[i] = toupper((unsigned char)out[i]);
  }
  return out;
}

static char *string_to_lower(const char *s) {
  char *out = strdup(s);
  for (int i = 0; out[i]; i++) {
    out[i] = tolower((unsigned char)out[i]);
  }
  return out;
}

// ============================================================================
// VALUE FONKSİYONLARI
// ============================================================================

Value *value_create_int(long long val) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_INT;
  value->data.int_val = val;
  return value;
}

Value *value_create_float(float val) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_FLOAT;
  value->data.float_val = val;
  return value;
}

Value *value_create_string(char *val) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_STRING;
  value->data.string_val = strdup(val);
  return value;
}

Value *value_create_bool(int val) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_BOOL;
  value->data.bool_val = val;
  return value;
}

Value *value_create_void() {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_VOID;
  return value;
}

// ==========================
// BigInt helpers (positive integers)
// Representation: decimal string of digits only, leading zeros trimmed
// ==========================

static char *bigint_trim(const char *s) {
  while (*s == '0' && s[1] != '\0')
    s++;
  return strdup(s);
}

// ==========================
// JSON Serde Helpers
// ==========================

static void sb_append(char **buf, int *cap, int *len, const char *s) {
  int sl = (int)strlen(s);
  if (*len + sl + 1 > *cap) {
    while (*len + sl + 1 > *cap)
      *cap *= 2;
    *buf = (char *)realloc(*buf, (size_t)*cap);
  }
  memcpy(*buf + *len, s, (size_t)sl);
  *len += sl;
  (*buf)[*len] = '\0';
}

static void json_escape_and_append(char **buf, int *cap, int *len,
                                   const char *s) {
  for (const char *p = s; *p; p++) {
    char c = *p;
    switch (c) {
    case '"':
      sb_append(buf, cap, len, "\\\"");
      break;
    case '\\':
      sb_append(buf, cap, len, "\\\\");
      break;
    case '\n':
      sb_append(buf, cap, len, "\\n");
      break;
    case '\r':
      sb_append(buf, cap, len, "\\r");
      break;
    case '\t':
      sb_append(buf, cap, len, "\\t");
      break;
    default: {
      char tmp[2] = {c, 0};
      sb_append(buf, cap, len, tmp);
    }
    }
  }
}

static void value_to_json_internal(Value *v, char **buf, int *cap, int *len);

static void object_to_json(HashTable *obj, char **buf, int *cap, int *len) {
  sb_append(buf, cap, len, "{");
  int first = 1;
  for (int i = 0; i < obj->bucket_count; i++) {
    HashEntry *e = obj->buckets[i];
    while (e) {
      // Skip internal markers
      if (strcmp(e->key, "__type") != 0) {
        if (!first)
          sb_append(buf, cap, len, ",");
        sb_append(buf, cap, len, "\"");
        json_escape_and_append(buf, cap, len, e->key);
        sb_append(buf, cap, len, "\":");
        value_to_json_internal(e->value, buf, cap, len);
        first = 0;
      }
      e = e->next;
    }
  }
  sb_append(buf, cap, len, "}");
}

static void array_to_json(Array *arr, char **buf, int *cap, int *len) {
  sb_append(buf, cap, len, "[");
  for (int i = 0; i < arr->length; i++) {
    if (i > 0)
      sb_append(buf, cap, len, ",");
    value_to_json_internal(arr->elements[i], buf, cap, len);
  }
  sb_append(buf, cap, len, "]");
}

static void value_to_json_internal(Value *v, char **buf, int *cap, int *len) {
  switch (v->type) {
  case VAL_INT: {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%lld", v->data.int_val);
    sb_append(buf, cap, len, tmp);
    break;
  }
  case VAL_FLOAT: {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", v->data.float_val);
    sb_append(buf, cap, len, tmp);
    break;
  }
  case VAL_BOOL: {
    sb_append(buf, cap, len, v->data.bool_val ? "true" : "false");
    break;
  }
  case VAL_STRING: {
    sb_append(buf, cap, len, "\"");
    json_escape_and_append(buf, cap, len, v->data.string_val);
    sb_append(buf, cap, len, "\"");
    break;
  }
  case VAL_ARRAY: {
    array_to_json(v->data.array_val, buf, cap, len);
    break;
  }
  case VAL_OBJECT: {
    object_to_json(v->data.object_val, buf, cap, len);
    break;
  }
  case VAL_BIGINT: {
    sb_append(buf, cap, len, v->data.bigint_val);
    break;
  }
  default: {
    sb_append(buf, cap, len, "null");
    break;
  }
  }
}

static char *value_to_json_string(Value *v) {
  int cap = 256, len = 0;
  char *buf = (char *)malloc((size_t)cap);
  buf[0] = '\0';
  value_to_json_internal(v, &buf, &cap, &len);
  return buf;
}

// Simple JSON parser (limited)
typedef struct {
  const char *s;
  int i;
} JsonCur;
static void json_skip_ws(JsonCur *c) {
  while (c->s[c->i] && isspace((unsigned char)c->s[c->i]))
    c->i++;
}
static int json_match(JsonCur *c, char ch) {
  if (c->s[c->i] == ch) {
    c->i++;
    return 1;
  }
  return 0;
}

static char *json_parse_string(JsonCur *c) {
  if (!json_match(c, '"'))
    return NULL;
  char *out = (char *)malloc(1);
  int cap = 1;
  int len = 0;
  out[0] = '\0';
  while (c->s[c->i] && c->s[c->i] != '"') {
    char ch = c->s[c->i++];
    if (ch == '\\') {
      char esc = c->s[c->i++];
      switch (esc) {
      case 'n':
        ch = '\n';
        break;
      case 'r':
        ch = '\r';
        break;
      case 't':
        ch = '\t';
        break;
      case '"':
        ch = '"';
        break;
      case '\\':
        ch = '\\';
        break;
      default:
        ch = esc;
        break;
      }
    }
    if (len + 1 >= cap) {
      cap *= 2;
      out = (char *)realloc(out, (size_t)cap);
    }
    out[len++] = ch;
    out[len] = '\0';
  }
  json_match(c, '"');
  return out;
}

static Value *json_parse_value(JsonCur *c);

static Value *json_parse_array(JsonCur *c) {
  if (!json_match(c, '['))
    return NULL;
  Value *arrv = value_create_array(4);
  json_skip_ws(c);
  if (json_match(c, ']'))
    return arrv;
  while (1) {
    json_skip_ws(c);
    Value *v = json_parse_value(c);
    array_push(arrv->data.array_val, v);
    value_free(v);
    json_skip_ws(c);
    if (json_match(c, ']'))
      break;
    json_match(c, ',');
  }
  return arrv;
}

static Value *json_parse_object(JsonCur *c) {
  if (!json_match(c, '{'))
    return NULL;
  Value *obj = value_create_object();
  json_skip_ws(c);
  if (json_match(c, '}'))
    return obj;
  while (1) {
    json_skip_ws(c);
    char *key = json_parse_string(c);
    json_skip_ws(c);
    json_match(c, ':');
    json_skip_ws(c);
    Value *v = json_parse_value(c);
    hash_table_set(obj->data.object_val, key, v);
    free(key);
    json_skip_ws(c);
    if (json_match(c, '}'))
      break;
    json_match(c, ',');
  }
  return obj;
}

static Value *json_parse_number(JsonCur *c) {
  int start = c->i;
  int dot = 0;
  if (c->s[c->i] == '-')
    c->i++;
  while (isdigit((unsigned char)c->s[c->i]))
    c->i++;
  if (c->s[c->i] == '.') {
    dot = 1;
    c->i++;
    while (isdigit((unsigned char)c->s[c->i]))
      c->i++;
  }
  int len = c->i - start;
  char *buf = (char *)malloc((size_t)len + 1);
  memcpy(buf, c->s + start, (size_t)len);
  buf[len] = '\0';
  Value *v = dot ? value_create_float((float)atof(buf))
                 : value_create_int((long long)atoll(buf));
  free(buf);
  return v;
}

static Value *json_parse_value(JsonCur *c) {
  json_skip_ws(c);
  char ch = c->s[c->i];
  if (ch == '"') {
    char *s = json_parse_string(c);
    Value *v = value_create_string(s);
    free(s);
    return v;
  }
  if (ch == '{')
    return json_parse_object(c);
  if (ch == '[')
    return json_parse_array(c);
  if (ch == 't' && strncmp(c->s + c->i, "true", 4) == 0) {
    c->i += 4;
    return value_create_bool(1);
  }
  if (ch == 'f' && strncmp(c->s + c->i, "false", 5) == 0) {
    c->i += 5;
    return value_create_bool(0);
  }
  if (ch == 'n' && strncmp(c->s + c->i, "null", 4) == 0) {
    c->i += 4;
    return value_create_void();
  }
  return json_parse_number(c);
}
static char *bigint_from_ll_str(long long x) {
  if (x <= 0) {
    if (x == 0)
      return strdup("0");
    // Negative not supported: convert to absolute value (sign may be added
    // later)
    unsigned long long ux = (unsigned long long)(-(x + 1)) + 1ULL;
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", ux);
    return bigint_trim(buf);
  }
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", x);
  return bigint_trim(buf);
}

static char *bigint_add_str(const char *a, const char *b) {
  int la = (int)strlen(a), lb = (int)strlen(b);
  int L = (la > lb ? la : lb) + 1;
  char *out = (char *)malloc((size_t)L + 1);
  int ia = la - 1, ib = lb - 1, io = L;
  int carry = 0;
  out[io] = '\0';
  while (ia >= 0 || ib >= 0 || carry) {
    int da = (ia >= 0) ? (a[ia] - '0') : 0;
    int db = (ib >= 0) ? (b[ib] - '0') : 0;
    int s = da + db + carry;
    out[--io] = (char)('0' + (s % 10));
    carry = s / 10;
    ia--;
    ib--;
  }
  while (io > 0)
    out[--io] = '0';
  char *trimmed = bigint_trim(out + io);
  free(out);
  return trimmed;
}

static char *bigint_mul_str(const char *a, const char *b) {
  if (a[0] == '0' || b[0] == '0')
    return strdup("0");
  int la = (int)strlen(a), lb = (int)strlen(b);
  int L = la + lb;
  // Optimize: use char array directly instead of int array for small numbers
  int *tmp = (int *)calloc((size_t)L, sizeof(int));
  for (int i = la - 1; i >= 0; i--) {
    int da = a[i] - '0';
    for (int j = lb - 1; j >= 0; j--) {
      int db = b[j] - '0';
      tmp[i + j + 1] += da * db;
    }
  }
  // Carry propagation - optimized loop
  for (int k = L - 1; k > 0; k--) {
    if (tmp[k] >= 10) {
      tmp[k - 1] += tmp[k] / 10;
      tmp[k] %= 10;
    }
  }
  // Find first non-zero digit to avoid unnecessary trimming
  int start = 0;
  while (start < L && tmp[start] == 0)
    start++;
  if (start == L) {
    free(tmp);
    return strdup("0");
  }
  int result_len = L - start;
  char *out = (char *)malloc((size_t)result_len + 1);
  for (int i = 0; i < result_len; i++) {
    out[i] = (char)('0' + tmp[start + i]);
  }
  out[result_len] = '\0';
  free(tmp);
  return out;
}

static char *bigint_mul_small(const char *a, long long b) {
  if (b == 0)
    return strdup("0");
  if (b < 0)
    b = -b; // sign not supported (may be added later)
  // Optimize: for small multipliers, use direct multiplication instead of
  // string conversion
  int la = (int)strlen(a);
  if (b < 10 && la < 100) {
    // Fast path for single-digit multiplier and short numbers
    int digit = (int)b;
    int L = la + 1;
    int *tmp = (int *)calloc((size_t)L, sizeof(int));
    int carry = 0;
    for (int i = la - 1; i >= 0; i--) {
      int da = a[i] - '0';
      int prod = da * digit + carry;
      tmp[i + 1] = prod % 10;
      carry = prod / 10;
    }
    if (carry > 0)
      tmp[0] = carry;
    int start = (carry == 0) ? 1 : 0;
    int result_len = L - start;
    char *out = (char *)malloc((size_t)result_len + 1);
    for (int i = 0; i < result_len; i++) {
      out[i] = (char)('0' + tmp[start + i]);
    }
    out[result_len] = '\0';
    free(tmp);
    return out;
  }
  // Fallback to string-based multiplication for larger numbers
  char buf[32];
  snprintf(buf, sizeof(buf), "%lld", b);
  return bigint_mul_str(a, buf);
}

static int bigint_cmp(const char *a, const char *b) {
  int la = (int)strlen(a), lb = (int)strlen(b);
  if (la != lb)
    return (la < lb) ? -1 : 1;
  int c = strcmp(a, b);
  if (c < 0)
    return -1;
  if (c > 0)
    return 1;
  return 0;
}

static char *bigint_sub_str(const char *a, const char *b) {
  // assumes a >= b (non-negative)
  int la = (int)strlen(a), lb = (int)strlen(b);
  int L = la;
  char *out = (char *)malloc((size_t)L + 1);
  int ia = la - 1, ib = lb - 1, io = L - 1;
  int borrow = 0;
  while (ia >= 0) {
    int da = a[ia] - '0' - borrow;
    int db = (ib >= 0) ? (b[ib] - '0') : 0;
    if (da < db) {
      da += 10;
      borrow = 1;
    } else
      borrow = 0;
    int d = da - db;
    out[io--] = (char)('0' + d);
    ia--;
    ib--;
  }
  out[L] = '\0';
  char *trimmed = bigint_trim(out);
  free(out);
  return trimmed;
}

static void bigint_divmod(const char *a, const char *b, char **q_out,
                          char **r_out) {
  // Optimized long division: a / b, both positive decimal strings
  if (b[0] == '0') {
    *q_out = strdup("0");
    *r_out = strdup("0");
    return;
  }
  if (bigint_cmp(a, b) < 0) {
    *q_out = strdup("0");
    *r_out = strdup(a);
    return;
  }
  int la = (int)strlen(a);
  // Optimize: use pre-allocated buffer for remainder to avoid repeated
  // malloc/free
  int rem_cap = la + 2;
  char *rem = (char *)malloc((size_t)rem_cap);
  rem[0] = '0';
  rem[1] = '\0';
  int rem_len = 1;
  char *quo = (char *)malloc((size_t)la + 2);
  int qi = 0;
  for (int i = 0; i < la; i++) {
    // Optimize: rem*10 + a[i] without malloc/free
    if (rem_len + 1 >= rem_cap) {
      rem_cap = rem_cap * 2;
      rem = (char *)realloc(rem, (size_t)rem_cap);
    }
    rem[rem_len] = a[i];
    rem[rem_len + 1] = '\0';
    rem_len++;
    // Trim leading zeros efficiently
    int start = 0;
    while (start < rem_len - 1 && rem[start] == '0')
      start++;
    if (start > 0) {
      int new_len = rem_len - start;
      memmove(rem, rem + start, (size_t)new_len);
      rem[new_len] = '\0';
      rem_len = new_len;
    }
    // Find digit using binary search
    int d = 0;
    int lo = 0, hi = 9;
    while (lo <= hi) {
      int mid = (lo + hi) / 2;
      char *prod = bigint_mul_small(b, mid);
      int cmp = bigint_cmp(prod, rem);
      free(prod);
      if (cmp <= 0) {
        d = mid;
        lo = mid + 1;
      } else {
        hi = mid - 1;
      }
    }
    quo[qi++] = (char)('0' + d);
    if (d > 0) {
      char *prod = bigint_mul_small(b, d);
      char *newrem = bigint_sub_str(rem, prod);
      free(prod);
      // Update rem buffer
      int newrem_len = (int)strlen(newrem);
      if (newrem_len + 1 > rem_cap) {
        rem_cap = newrem_len + 2;
        rem = (char *)realloc(rem, (size_t)rem_cap);
      }
      strcpy(rem, newrem);
      rem_len = newrem_len;
      free(newrem);
    }
  }
  quo[qi] = '\0';
  // Trim quotient
  int qstart = 0;
  while (qstart < qi && quo[qstart] == '0')
    qstart++;
  if (qstart == qi) {
    free(quo);
    *q_out = strdup("0");
  } else {
    int qlen = qi - qstart;
    char *qtrim = (char *)malloc((size_t)qlen + 1);
    memcpy(qtrim, quo + qstart, (size_t)qlen);
    qtrim[qlen] = '\0';
    free(quo);
    *q_out = qtrim;
  }
  if (rem_len == 0 || (rem_len == 1 && rem[0] == '0')) {
    free(rem);
    *r_out = strdup("0");
  } else {
    *r_out = rem; // Transfer ownership
  }
}

static char *bigint_pow_str(const char *a, long long e) {
  // exponentiation by squaring (non-negative e)
  char *result = strdup("1");
  char *base = strdup(a);
  long long expv = e;
  while (expv > 0) {
    if (expv & 1LL) {
      char *tmp = bigint_mul_str(result, base);
      free(result);
      result = tmp;
    }
    expv >>= 1LL;
    if (expv) {
      char *sq = bigint_mul_str(base, base);
      free(base);
      base = sq;
    }
  }
  free(base);
  return result;
}

Value *value_create_array(int capacity) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_ARRAY;

  Array *arr = (Array *)malloc(sizeof(Array));
  arr->elements = (Value **)malloc(sizeof(Value *) * capacity);
  arr->length = 0;
  arr->capacity = capacity;
  arr->elem_type = VAL_VOID; // Mixed type

  value->data.array_val = arr;
  return value;
}

Value *value_create_typed_array(int capacity, ValueType elem_type) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_ARRAY;

  Array *arr = (Array *)malloc(sizeof(Array));
  arr->elements = (Value **)malloc(sizeof(Value *) * capacity);
  arr->length = 0;
  arr->capacity = capacity;
  arr->elem_type = elem_type; // Typed array

  value->data.array_val = arr;
  return value;
}

Value *value_create_object() {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_OBJECT;
  value->data.object_val = hash_table_create(16); // 16 buckets initial
  return value;
}

Value *value_create_bigint(const char *digits) {
  Value *value = (Value *)malloc(sizeof(Value));
  value->type = VAL_BIGINT;
  value->data.bigint_val = bigint_trim(digits);
  return value;
}

// ============================================================================
// HASH TABLE FUNCTIONS
// ============================================================================

// Simple hash function (djb2 algorithm)
unsigned int hash_function(const char *key, int bucket_count) {
  unsigned long hash = 5381;
  int c;

  while ((c = *key++)) {
    hash = ((hash << 5) + hash) + c; // hash * 33 + c
  }

  return hash % bucket_count;
}

// Create hash table
HashTable *hash_table_create(int bucket_count) {
  HashTable *table = (HashTable *)malloc(sizeof(HashTable));
  table->bucket_count = bucket_count;
  table->size = 0;
  table->buckets = (HashEntry **)calloc(bucket_count, sizeof(HashEntry *));
  return table;
}

// Hash table temizle
void hash_table_free(HashTable *table) {
  if (!table)
    return;

  for (int i = 0; i < table->bucket_count; i++) {
    HashEntry *entry = table->buckets[i];
    while (entry) {
      HashEntry *next = entry->next;
      free(entry->key);
      value_free(entry->value);
      free(entry);
      entry = next;
    }
  }

  free(table->buckets);
  free(table);
}

// Add/update key-value
void hash_table_set(HashTable *table, const char *key, Value *value) {
  unsigned int index = hash_function(key, table->bucket_count);
  HashEntry *entry = table->buckets[index];

  // Check if key already exists
  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      // Update
      value_free(entry->value);
      entry->value = value;
      return;
    }
    entry = entry->next;
  }

  // Create new entry
  HashEntry *new_entry = (HashEntry *)malloc(sizeof(HashEntry));
  new_entry->key = strdup(key);
  new_entry->value = value;
  new_entry->next = table->buckets[index];
  table->buckets[index] = new_entry;
  table->size++;
}

// Key ile value al
Value *hash_table_get(HashTable *table, const char *key) {
  unsigned int index = hash_function(key, table->bucket_count);
  HashEntry *entry = table->buckets[index];

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      return entry->value;
    }
    entry = entry->next;
  }

  return NULL; // Not found
}

// Check if key exists
int hash_table_has(HashTable *table, const char *key) {
  return hash_table_get(table, key) != NULL;
}

// Key sil
void hash_table_delete(HashTable *table, const char *key) {
  unsigned int index = hash_function(key, table->bucket_count);
  HashEntry *entry = table->buckets[index];
  HashEntry *prev = NULL;

  while (entry) {
    if (strcmp(entry->key, key) == 0) {
      if (prev) {
        prev->next = entry->next;
      } else {
        table->buckets[index] = entry->next;
      }

      free(entry->key);
      value_free(entry->value);
      free(entry);
      table->size--;
      return;
    }
    prev = entry;
    entry = entry->next;
  }
}

// Print hash table (debug)
void hash_table_print(HashTable *table) {
  printf("{");
  int first = 1;

  for (int i = 0; i < table->bucket_count; i++) {
    HashEntry *entry = table->buckets[i];
    while (entry) {
      if (!first)
        printf(", ");
      printf("\"%s\": ", entry->key);
      value_print(entry->value);
      first = 0;
      entry = entry->next;
    }
  }

  printf("}");
}

// ============================================================================
// ARRAY FUNCTIONS
// ============================================================================

void array_push(Array *arr, Value *val) {
  // Type check (if typed array)
  if (arr->elem_type != VAL_VOID && arr->elem_type != val->type) {
    printf("Error: Array only accepts elements of type ");
    switch (arr->elem_type) {
    case VAL_INT:
      printf("int");
      break;
    case VAL_FLOAT:
      printf("float");
      break;
    case VAL_STRING:
      printf("str");
      break;
    case VAL_BOOL:
      printf("bool");
      break;
    default:
      printf("unknown type");
      break;
    }
    printf("!\n");
    return;
  }

  if (arr->length >= arr->capacity) {
    arr->capacity = (arr->capacity == 0) ? 8 : arr->capacity * 2;
    arr->elements =
        (Value **)realloc(arr->elements, sizeof(Value *) * arr->capacity);
  }
  arr->elements[arr->length++] = value_copy(val);
}

Value *array_pop(Array *arr) {
  if (arr->length == 0) {
    return value_create_void();
  }
  Value *val = arr->elements[--arr->length];
  return val; // Ownership transfer
}

Value *array_get(Array *arr, int index) {
  if (index < 0 || index >= arr->length) {
    printf("Error: Array index out of bounds: %d (length: %d)\n", index,
           arr->length);
    return value_create_void();
  }
  return value_copy(arr->elements[index]);
}

void array_set(Array *arr, int index, Value *val) {
  if (index < 0 || index >= arr->length) {
    printf("Error: Array index out of bounds: %d (length: %d)\n", index,
           arr->length);
    return;
  }

  // Type check (if typed array)
  if (arr->elem_type != VAL_VOID && arr->elem_type != val->type) {
    printf("Error: Array only accepts elements of type ");
    switch (arr->elem_type) {
    case VAL_INT:
      printf("int");
      break;
    case VAL_FLOAT:
      printf("float");
      break;
    case VAL_STRING:
      printf("str");
      break;
    case VAL_BOOL:
      printf("bool");
      break;
    default:
      printf("unknown type");
      break;
    }
    printf("!\n");
    return;
  }

  value_free(arr->elements[index]);
  arr->elements[index] = value_copy(val);
}

// ============================================================================
// VALUE FONKSİYONLARI
// ============================================================================

Value *value_copy(Value *val) {
  if (!val)
    return NULL;

  Value *copy = (Value *)malloc(sizeof(Value));
  copy->type = val->type;

  switch (val->type) {
  case VAL_INT:
    copy->data.int_val = val->data.int_val;
    break;
  case VAL_FLOAT:
    copy->data.float_val = val->data.float_val;
    break;
  case VAL_BIGINT:
    copy->data.bigint_val = strdup(val->data.bigint_val);
    break;
  case VAL_STRING:
    copy->data.string_val = strdup(val->data.string_val);
    break;
  case VAL_BOOL:
    copy->data.bool_val = val->data.bool_val;
    break;
  case VAL_ARRAY: {
    Array *src = val->data.array_val;
    Array *dst = (Array *)malloc(sizeof(Array));
    dst->capacity = src->capacity;
    dst->length = src->length;
    dst->elem_type = src->elem_type;
    dst->elements = (Value **)malloc(sizeof(Value *) * dst->capacity);

    for (int i = 0; i < src->length; i++) {
      dst->elements[i] = value_copy(src->elements[i]);
    }

    copy->data.array_val = dst;
    break;
  }
  case VAL_OBJECT: {
    // Deep copy hash table
    HashTable *src = val->data.object_val;
    HashTable *dst = hash_table_create(src->bucket_count);

    for (int i = 0; i < src->bucket_count; i++) {
      HashEntry *entry = src->buckets[i];
      while (entry) {
        hash_table_set(dst, entry->key, value_copy(entry->value));
        entry = entry->next;
      }
    }

    copy->data.object_val = dst;
    break;
  }
  case VAL_THREAD: {
#ifdef _WIN32
    HANDLE hTarget;
    // Thread handle'ını kopyala (Duplicate) - böylece her value kendi
    // handle'ına sahip olur
    if (DuplicateHandle(GetCurrentProcess(), val->data.thread_val,
                        GetCurrentProcess(), &hTarget, 0, FALSE,
                        DUPLICATE_SAME_ACCESS)) {
      copy->data.thread_val = hTarget;
    } else {
      copy->data.thread_val = NULL;
    }
#else
    copy->data.thread_val =
        val->data.thread_val; // POSIX thread id kopyalanabilir
#endif
    break;
  }
  case VAL_MUTEX: {
    // Mutex handle kopyala (Shallow copy - aynı mutex'i işaret eder)
    copy->data.mutex_val = val->data.mutex_val;
    break;
  }
  default:
    break;
  }

  return copy;
}

void value_free(Value *val) {
  if (!val)
    return;

  if (val->type == VAL_STRING && val->data.string_val) {
    free(val->data.string_val);
  }

  if (val->type == VAL_ARRAY && val->data.array_val) {
    Array *arr = val->data.array_val;
    for (int i = 0; i < arr->length; i++) {
      value_free(arr->elements[i]);
    }
    free(arr->elements);
    free(arr);
  }

  if (val->type == VAL_OBJECT && val->data.object_val) {
    hash_table_free(val->data.object_val);
  }
  if (val->type == VAL_BIGINT && val->data.bigint_val) {
    free(val->data.bigint_val);
  }

  if (val->type == VAL_THREAD) {
#ifdef _WIN32
    if (val->data.thread_val) {
      CloseHandle(val->data.thread_val);
    }
#endif
  }

  free(val);
}

void value_print(Value *val) {
  if (!val) {
    printf("NULL");
    return;
  }

  switch (val->type) {
  case VAL_INT:
    printf("%lld", val->data.int_val);
    break;
  case VAL_FLOAT:
    printf("%g", val->data.float_val);
    break;
  case VAL_BIGINT:
    printf("%s", val->data.bigint_val);
    break;
  case VAL_STRING:
    printf("%s", val->data.string_val);
    break;
  case VAL_BOOL:
    printf("%s", val->data.bool_val ? "true" : "false");
    break;
  case VAL_ARRAY: {
    Array *arr = val->data.array_val;
    printf("[");
    for (int i = 0; i < arr->length; i++) {
      value_print(arr->elements[i]);
      if (i < arr->length - 1) {
        printf(", ");
      }
    }
    printf("]");
    break;
  }
  case VAL_OBJECT:
    hash_table_print(val->data.object_val);
    break;
  case VAL_VOID:
    printf("void");
    break;
  case VAL_THREAD:
    printf("<Thread>");
    break;
  case VAL_MUTEX:
    printf("<Mutex>");
    break;
  default:
    printf("unknown");
    break;
  }
}

int value_is_truthy(Value *val) {
  if (!val)
    return 0;

  switch (val->type) {
  case VAL_BOOL:
    return val->data.bool_val;
  case VAL_INT:
    return val->data.int_val != 0;
  case VAL_FLOAT:
    return val->data.float_val != 0.0f;
  case VAL_STRING:
    return val->data.string_val != NULL && strlen(val->data.string_val) > 0;
  default:
    return 0;
  }
}

// ============================================================================
// SYMBOL TABLE FUNCTIONS
// ============================================================================

SymbolTable *symbol_table_create(SymbolTable *parent) {
  SymbolTable *table = (SymbolTable *)malloc(sizeof(SymbolTable));
  table->var_capacity = 16;
  table->var_count = 0;
  table->variables =
      (Variable **)malloc(sizeof(Variable *) * table->var_capacity);
  table->parent = parent;
  return table;
}

void symbol_table_free(SymbolTable *table) {
  if (!table)
    return;

  for (int i = 0; i < table->var_count; i++) {
    free(table->variables[i]->name);
    value_free(table->variables[i]->value);
    free(table->variables[i]);
  }
  free(table->variables);
  free(table);
}

void symbol_table_set(SymbolTable *table, char *name, Value *value) {
  // First search for existing variable in current scope
  for (int i = 0; i < table->var_count; i++) {
    if (strcmp(table->variables[i]->name, name) == 0) {
      // Update existing variable
      value_free(table->variables[i]->value);
      table->variables[i]->value = value_copy(value);
      return;
    }
  }

  // If not found in current scope, check parent scope
  if (table->parent) {
    SymbolTable *parent = table->parent;
    for (int i = 0; i < parent->var_count; i++) {
      if (strcmp(parent->variables[i]->name, name) == 0) {
        // Update variable in parent scope
        value_free(parent->variables[i]->value);
        parent->variables[i]->value = value_copy(value);
        return;
      }
    }
    // Recursively check parent's parent
    if (parent->parent) {
      symbol_table_set(parent, name, value);
      return;
    }
  }

  // Add new variable to current scope
  if (table->var_count >= table->var_capacity) {
    table->var_capacity *= 2;
    table->variables = (Variable **)realloc(
        table->variables, sizeof(Variable *) * table->var_capacity);
  }

  Variable *var = (Variable *)malloc(sizeof(Variable));
  var->name = strdup(name);
  var->value = value_copy(value);
  table->variables[table->var_count++] = var;
}

Value *symbol_table_get(SymbolTable *table, char *name) {
  // Search in current scope
  for (int i = 0; i < table->var_count; i++) {
    if (strcmp(table->variables[i]->name, name) == 0) {
      return table->variables[i]->value;
    }
  }

  // Parent scope'ta ara
  if (table->parent) {
    return symbol_table_get(table->parent, name);
  }

  return NULL;
}

// ============================================================================
// INTERPRETER FUNCTIONS
// ============================================================================

Interpreter *interpreter_create() {
#ifdef _WIN32
  WSADATA wsaData;
  if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
    fprintf(stderr, "WSAStartup failed.\n");
    exit(1);
  }
#endif
  Interpreter *interp = (Interpreter *)malloc(sizeof(Interpreter));
  interp->global_scope = symbol_table_create(NULL);
  interp->current_scope = interp->global_scope;
  interp->function_capacity = 16;
  interp->function_count = 0;
  interp->functions =
      (Function **)malloc(sizeof(Function *) * interp->function_capacity);
  interp->type_capacity = 8;
  interp->type_count = 0;
  interp->types = (TypeDef **)malloc(sizeof(TypeDef *) * interp->type_capacity);
  interp->return_value = NULL;
  interp->should_return = 0;
  interp->should_break = 0;
  interp->should_continue = 0;
  interp->retained_modules = NULL;
  interp->retained_count = 0;
  interp->retained_capacity = 0;
  // Exception handling initialization
  interp->exception_handler = NULL;
  interp->has_exception = 0;
  interp->current_exception = NULL;
  return interp;
}

void interpreter_free(Interpreter *interp) {
  if (!interp)
    return;

  for (int i = 0; i < interp->retained_count; i++) {
    ast_node_free(interp->retained_modules[i]);
  }
  free(interp->retained_modules);

  symbol_table_free(interp->global_scope);

  for (int i = 0; i < interp->function_count; i++) {
    free(interp->functions[i]->name);
    free(interp->functions[i]);
  }
  free(interp->functions);
  for (int i = 0; i < interp->type_count; i++) {
    for (int j = 0; j < interp->types[i]->field_count; j++)
      free(interp->types[i]->field_names[j]);
    free(interp->types[i]->field_names);
    free(interp->types[i]->field_types);
    free(interp->types[i]->name);
    free(interp->types[i]);
  }
  free(interp->types);

  if (interp->return_value) {
    value_free(interp->return_value);
  }

  free(interp);
#ifdef _WIN32
  WSACleanup();
#endif
}

void interpreter_register_function(Interpreter *interp, char *name,
                                   ASTNode *node) {
  if (interp->function_count >= interp->function_capacity) {
    interp->function_capacity *= 2;
    interp->functions = (Function **)realloc(
        interp->functions, sizeof(Function *) * interp->function_capacity);
  }

  Function *func = (Function *)malloc(sizeof(Function));
  // If type method, create record name as TypeName.method
  if (node->receiver_type_name) {
    char fullname[256];
    snprintf(fullname, sizeof(fullname), "%s.%s", node->receiver_type_name,
             name);
    func->name = strdup(fullname);
  } else {
    func->name = strdup(name);
  }
  func->node = node;
  interp->functions[interp->function_count++] = func;
}

Function *interpreter_get_function(Interpreter *interp, char *name) {
  for (int i = 0; i < interp->function_count; i++) {
    if (strcmp(interp->functions[i]->name, name) == 0) {
      return interp->functions[i];
    }
  }
  return NULL;
}

TypeDef *interpreter_get_type(Interpreter *interp, const char *name) {
  for (int i = 0; i < interp->type_count; i++) {
    if (strcmp(interp->types[i]->name, name) == 0)
      return interp->types[i];
  }
  return NULL;
}

void interpreter_register_type(Interpreter *interp, TypeDef *t) {
  if (interp->type_count >= interp->type_capacity) {
    interp->type_capacity *= 2;
    interp->types = (TypeDef **)realloc(
        interp->types, sizeof(TypeDef *) * interp->type_capacity);
  }
  interp->types[interp->type_count++] = t;
}

// Forward declarations
Value *interpreter_eval_expression(Interpreter *interp, ASTNode *node);
void interpreter_execute_statement(Interpreter *interp, ASTNode *node);

// ============================================================================
// EXPRESSION EVALUATION
// ============================================================================

Value *interpreter_eval_expression(Interpreter *interp, ASTNode *node) {
  if (!node)
    return value_create_void();

  switch (node->type) {
  case AST_INT_LITERAL:
    return value_create_int(node->value.int_value);

  case AST_FLOAT_LITERAL:
    return value_create_float(node->value.float_value);

  case AST_STRING_LITERAL:
    return value_create_string(node->value.string_value);

  case AST_BOOL_LITERAL:
    return value_create_bool(node->value.bool_value);

  case AST_ARRAY_LITERAL: {
    // Dizi literal: [1, 2, 3]
    Value *arr =
        value_create_array(node->element_count > 0 ? node->element_count : 4);

    for (int i = 0; i < node->element_count; i++) {
      Value *elem = interpreter_eval_expression(interp, node->elements[i]);
      array_push(arr->data.array_val, elem);
      value_free(elem);
    }

    return arr;
  }

  case AST_OBJECT_LITERAL: {
    // Object literal: { "key": value, "key2": value2 }
    Value *obj = value_create_object();

    for (int i = 0; i < node->object_count; i++) {
      const char *key = node->object_keys[i];
      Value *value =
          interpreter_eval_expression(interp, node->object_values[i]);
      hash_table_set(obj->data.object_val, key, value);
    }

    return obj;
  }

  case AST_ARRAY_ACCESS: {
    // Array/Object erişimi: arr[0] or obj["key"] or nested arr[0][1]["key"]
    Value *container = NULL;

    // Zincirleme erişim mi? (left var mı?)
    if (node->left) {
      // Nested access: önce left'i değerlendir
      container = interpreter_eval_expression(interp, node->left);
    } else {
      // İlk erişim: değişkenden al
      container = symbol_table_get(interp->current_scope, node->name);
      if (!container) {
        printf("Error: Undefined variable '%s'\n", node->name);
        exit(1);
      }
      // Don't copy from symbol table (reference)
      // Ama nested'de eval'den gelirse zaten yeni value
    }

    Value *index_val = interpreter_eval_expression(interp, node->index);
    Value *result = NULL;

    // Array access (integer index)
    if (container->type == VAL_ARRAY) {
      if (index_val->type != VAL_INT) {
        printf("Error: Array index must be integer (line %d)\n", node->line);
        value_free(index_val);
        if (node->left)
          value_free(container);
        exit(1);
      }

      int index = index_val->data.int_val;
      value_free(index_val);
      result = array_get(container->data.array_val, index);

      // Left'ten gelen container'ı temizle
      if (node->left)
        value_free(container);
      return result;
    }

    // Object access (string key)
    if (container->type == VAL_OBJECT) {
      if (index_val->type != VAL_STRING) {
        printf("Error: Object key must be string (line %d)\n", node->line);
        value_free(index_val);
        if (node->left)
          value_free(container);
        exit(1);
      }

      const char *key = index_val->data.string_val;
      Value *found = hash_table_get(container->data.object_val, key);
      value_free(index_val);

      if (!found) {
        printf("Error: Key '%s' not found in object\n", key);
        if (node->left)
          value_free(container);
        return value_create_void();
      }

      result = value_copy(found);

      // Left'ten gelen container'ı temizle
      if (node->left)
        value_free(container);
      return result;
    }

    // String access (character by index)
    if (container->type == VAL_STRING) {
      if (index_val->type != VAL_INT) {
        printf("Error: String index must be integer (line %d)\n", node->line);
        value_free(index_val);
        if (node->left)
          value_free(container);
        exit(1);
      }

      int idx = index_val->data.int_val;
      const char *str = container->data.string_val;
      int len = utf8_strlen_cp(str);

      // Index bounds check
      if (idx < 0 || idx >= len) {
        printf("Error: String index out of bounds (0-%d, given %d) (line %d)\n",
               len - 1, idx, node->line);
        value_free(index_val);
        if (node->left)
          value_free(container);
        exit(1);
      }

      char *char_str = utf8_char_at(str, idx);
      if (!char_str) {
        printf("Error: UTF-8 character could not be decoded\n");
        value_free(index_val);
        if (node->left)
          value_free(container);
        exit(1);
      }

      result = value_create_string(char_str);
      free(char_str);

      value_free(index_val);
      if (node->left)
        value_free(container);
      return result;
    }

    printf(
        "Error: Accessed value is not an array, object, or string (line %d)\n",
        node->line);
    value_free(index_val);
    if (node->left)
      value_free(container);
    exit(1);
  }

  case AST_IDENTIFIER: {
    Value *val = symbol_table_get(interp->current_scope, node->name);
    if (!val) {
      printf("Error: Undefined variable '%s' (line %d)\n", node->name,
             node->line);
      exit(1);
    }
    return value_copy(val);
  }

  case AST_BINARY_OP: {
    Value *left = interpreter_eval_expression(interp, node->left);
    Value *right = interpreter_eval_expression(interp, node->right);
    Value *result = NULL;

    // Aritmetik operatörler
    if (node->op == TOKEN_PLUS) {
      if (left->type == VAL_BIGINT || right->type == VAL_BIGINT) {
        const char *la = (left->type == VAL_BIGINT)
                             ? left->data.bigint_val
                             : bigint_from_ll_str(left->data.int_val);
        const char *rb = (right->type == VAL_BIGINT)
                             ? right->data.bigint_val
                             : bigint_from_ll_str(right->data.int_val);
        char *sum = bigint_add_str(la, rb);
        if (left->type != VAL_BIGINT)
          free((char *)la);
        if (right->type != VAL_BIGINT)
          free((char *)rb);
        result = value_create_bigint(sum);
        free(sum);
      } else if (left->type == VAL_INT && right->type == VAL_INT) {
        long long a = left->data.int_val;
        long long b = right->data.int_val;
        if ((b > 0 && a > (LLONG_MAX - b)) || (b < 0 && a < (LLONG_MIN - b))) {
          char *sa = bigint_from_ll_str(a);
          char *sb = bigint_from_ll_str(b);
          char *sum = bigint_add_str(sa, sb);
          free(sa);
          free(sb);
          result = value_create_bigint(sum);
          free(sum);
        } else {
          result = value_create_int(a + b);
        }
      } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_float(l + r);
      } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
        // String birleştirme
        char *str = (char *)malloc(strlen(left->data.string_val) +
                                   strlen(right->data.string_val) + 1);
        strcpy(str, left->data.string_val);
        strcat(str, right->data.string_val);
        result = value_create_string(str);
        free(str);
      }
    } else if (node->op == TOKEN_MINUS) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        long long a = left->data.int_val;
        long long b = right->data.int_val;
        // Taşma kontrolü: a - b == a + (-b)
        if ((-b > 0 && a > (LLONG_MAX + b)) ||
            (-b < 0 && a < (LLONG_MIN + b))) {
          float l = (float)a;
          float r = (float)b;
          result = value_create_float(l - r);
        } else {
          result = value_create_int(a - b);
        }
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_float(l - r);
      }
    } else if (node->op == TOKEN_MULTIPLY) {
      if (left->type == VAL_BIGINT || right->type == VAL_BIGINT) {
        if (left->type == VAL_BIGINT && right->type == VAL_BIGINT) {
          char *prod =
              bigint_mul_str(left->data.bigint_val, right->data.bigint_val);
          result = value_create_bigint(prod);
          free(prod);
        } else {
          const char *big = (left->type == VAL_BIGINT) ? left->data.bigint_val
                                                       : right->data.bigint_val;
          long long small = (left->type == VAL_INT) ? left->data.int_val
                                                    : right->data.int_val;
          char *prod = bigint_mul_small(big, small);
          result = value_create_bigint(prod);
          free(prod);
        }
      } else if (left->type == VAL_INT && right->type == VAL_INT) {
        long long a = left->data.int_val;
        long long b = right->data.int_val;
        if (a != 0 && ((b > 0 && (a > LLONG_MAX / b || a < LLONG_MIN / b)) ||
                       (b < 0 && (a == LLONG_MIN || -a > LLONG_MAX / -b)))) {
          char *sa = bigint_from_ll_str(a);
          char *sb = bigint_from_ll_str(b);
          char *prod = bigint_mul_str(sa, sb);
          free(sa);
          free(sb);
          result = value_create_bigint(prod);
          free(prod);
        } else {
          result = value_create_int(a * b);
        }
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_float(l * r);
      }
    } else if (node->op == TOKEN_DIVIDE) {
      if ((left->type == VAL_BIGINT) || (right->type == VAL_BIGINT)) {
        const char *la = (left->type == VAL_BIGINT)
                             ? left->data.bigint_val
                             : bigint_from_ll_str(left->data.int_val);
        const char *rb = (right->type == VAL_BIGINT)
                             ? right->data.bigint_val
                             : bigint_from_ll_str(right->data.int_val);
        // Division by zero check
        if (strcmp(rb, "0") == 0 || (strlen(rb) == 1 && rb[0] == '0')) {
          printf("Error: Division by zero! (line %d)\n", node->line);
          if (left->type != VAL_BIGINT)
            free((char *)la);
          if (right->type != VAL_BIGINT)
            free((char *)rb);
          value_free(left);
          value_free(right);
          exit(1);
        }
        char *q;
        char *r;
        bigint_divmod(la, rb, &q, &r);
        if (left->type != VAL_BIGINT)
          free((char *)la);
        if (right->type != VAL_BIGINT)
          free((char *)rb);
        Value *res = value_create_bigint(q);
        free(q);
        free(r);
        result = res;
      } else if (left->type == VAL_INT && right->type == VAL_INT) {
        if (right->data.int_val == 0) {
          printf("Error: Division by zero! (line %d)\n", node->line);
          exit(1);
        }
        result = value_create_int(left->data.int_val / right->data.int_val);
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        if (r == 0.0f) {
          printf("Error: Division by zero! (line %d)\n", node->line);
          exit(1);
        }
        result = value_create_float(l / r);
      }
    }
    // Karşılaştırma operatörleri
    else if (node->op == TOKEN_EQUAL) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val == right->data.int_val);
      } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l == r);
      } else if (left->type == VAL_BOOL && right->type == VAL_BOOL) {
        result = value_create_bool(left->data.bool_val == right->data.bool_val);
      } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
        result = value_create_bool(
            strcmp(left->data.string_val, right->data.string_val) == 0);
      }
    } else if (node->op == TOKEN_NOT_EQUAL) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val != right->data.int_val);
      } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l != r);
      } else if (left->type == VAL_BOOL && right->type == VAL_BOOL) {
        result = value_create_bool(left->data.bool_val != right->data.bool_val);
      } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
        result = value_create_bool(
            strcmp(left->data.string_val, right->data.string_val) != 0);
      }
    } else if (node->op == TOKEN_LESS) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val < right->data.int_val);
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l < r);
      }
    } else if (node->op == TOKEN_GREATER) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val > right->data.int_val);
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l > r);
      }
    } else if (node->op == TOKEN_LESS_EQUAL) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val <= right->data.int_val);
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l <= r);
      }
    } else if (node->op == TOKEN_GREATER_EQUAL) {
      if (left->type == VAL_INT && right->type == VAL_INT) {
        result = value_create_bool(left->data.int_val >= right->data.int_val);
      } else {
        float l = (left->type == VAL_FLOAT) ? left->data.float_val
                                            : left->data.int_val;
        float r = (right->type == VAL_FLOAT) ? right->data.float_val
                                             : right->data.int_val;
        result = value_create_bool(l >= r);
      }
    }
    // Mantıksal operatörler
    else if (node->op == TOKEN_AND) {
      int left_truthy = value_is_truthy(left);
      int right_truthy = value_is_truthy(right);
      result = value_create_bool(left_truthy && right_truthy);
    } else if (node->op == TOKEN_OR) {
      int left_truthy = value_is_truthy(left);
      int right_truthy = value_is_truthy(right);
      result = value_create_bool(left_truthy || right_truthy);
    }

    value_free(left);
    value_free(right);

    if (!result) {
      printf("Error: Unsupported operator!\n");
      exit(1);
    }

    return result;
  }

  case AST_UNARY_OP: {
    Value *operand = interpreter_eval_expression(interp, node->left);
    Value *result = NULL;

    if (node->op == TOKEN_BANG) {
      // Logical NOT
      int truthy = value_is_truthy(operand);
      result = value_create_bool(!truthy);
    } else if (node->op == TOKEN_MINUS) {
      // Unary minus
      if (operand->type == VAL_INT) {
        result = value_create_int(-operand->data.int_val);
      } else if (operand->type == VAL_FLOAT) {
        result = value_create_float(-operand->data.float_val);
      }
    }

    value_free(operand);

    if (!result) {
      printf("Error: Unsupported unary operator!\n");
      exit(1);
    }

    return result;
  }

  case AST_FUNCTION_CALL: {
    // Built-in fonksiyonları kontrol et

    // ========================================================================
    // SOCKET FUNCTIONS

    // SOCKET FUNCTIONS
    // ========================================================================

    // socket_create(domain, type, protocol)
    if (strcmp(node->name, "socket_create") == 0) {
      SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
      if (sock == INVALID_SOCKET) {
        return value_create_int(-1);
      }
      return value_create_int((long long)sock);
    }

    // socket_bind(sockfd, host, port)
    if (strcmp(node->name, "socket_bind") == 0 && node->argument_count >= 3) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *host_val = interpreter_eval_expression(interp, node->arguments[1]);
      Value *port_val = interpreter_eval_expression(interp, node->arguments[2]);

      if (sock_val->type == VAL_INT && host_val->type == VAL_STRING &&
          port_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(host_val->data.string_val);
        server.sin_port = htons((unsigned short)port_val->data.int_val);

        if (bind(sock, (struct sockaddr *)&server, sizeof(server)) ==
            SOCKET_ERROR) {
          value_free(sock_val);
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }
        value_free(sock_val);
        value_free(host_val);
        value_free(port_val);
        return value_create_int(0);
      }
      value_free(sock_val);
      value_free(host_val);
      value_free(port_val);
      return value_create_int(-1);
    }

    // socket_listen(sockfd, backlog)
    if (strcmp(node->name, "socket_listen") == 0 && node->argument_count >= 2) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *backlog_val =
          interpreter_eval_expression(interp, node->arguments[1]);

      if (sock_val->type == VAL_INT && backlog_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        if (listen(sock, (int)backlog_val->data.int_val) == SOCKET_ERROR) {
          value_free(sock_val);
          value_free(backlog_val);
          return value_create_int(-1);
        }
        value_free(sock_val);
        value_free(backlog_val);
        return value_create_int(0);
      }
      value_free(sock_val);
      value_free(backlog_val);
      return value_create_int(-1);
    }

    // socket_accept(sockfd)
    if (strcmp(node->name, "socket_accept") == 0 && node->argument_count >= 1) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      if (sock_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        struct sockaddr_in client;
        int c = sizeof(struct sockaddr_in);
        SOCKET new_socket = accept(sock, (struct sockaddr *)&client, &c);

        value_free(sock_val);
        if (new_socket == INVALID_SOCKET) {
          return value_create_int(-1);
        }
        return value_create_int((long long)new_socket);
      }
      value_free(sock_val);
      return value_create_int(-1);
    }

    // socket_connect(sockfd, host, port)
    if (strcmp(node->name, "socket_connect") == 0 &&
        node->argument_count >= 3) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *host_val = interpreter_eval_expression(interp, node->arguments[1]);
      Value *port_val = interpreter_eval_expression(interp, node->arguments[2]);

      if (sock_val->type == VAL_INT && host_val->type == VAL_STRING &&
          port_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(host_val->data.string_val);
        server.sin_port = htons((unsigned short)port_val->data.int_val);

        if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
          value_free(sock_val);
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }
        value_free(sock_val);
        value_free(host_val);
        value_free(port_val);
        return value_create_int(0);
      }
      value_free(sock_val);
      value_free(host_val);
      value_free(port_val);
      return value_create_int(-1);
    }

    // socket_send(sockfd, data)
    if (strcmp(node->name, "socket_send") == 0 && node->argument_count >= 2) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *data_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (sock_val->type == VAL_INT && data_val->type == VAL_STRING) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        const char *msg = data_val->data.string_val;
        if (send(sock, msg, strlen(msg), 0) < 0) {
          value_free(sock_val);
          value_free(data_val);
          return value_create_int(-1);
        }
        value_free(sock_val);
        value_free(data_val);
        return value_create_int(0);
      }
      value_free(sock_val);
      value_free(data_val);
      return value_create_int(-1);
    }

    // socket_receive(sockfd, buffer_size)
    if (strcmp(node->name, "socket_receive") == 0 &&
        node->argument_count >= 2) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *size_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (sock_val->type == VAL_INT && size_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
        int size = (int)size_val->data.int_val;
        char *buffer = (char *)malloc(size + 1);
        int recv_size;

        if ((recv_size = recv(sock, buffer, size, 0)) == SOCKET_ERROR) {
          free(buffer);
          value_free(sock_val);
          value_free(size_val);
          return value_create_string("");
        }
        buffer[recv_size] = '\0';
        Value *res = value_create_string(buffer);
        free(buffer);
        value_free(sock_val);
        value_free(size_val);
        return res;
      }
      value_free(sock_val);
      value_free(size_val);
      return value_create_string("");
    }

    // socket_close(sockfd)
    if (strcmp(node->name, "socket_close") == 0 && node->argument_count >= 1) {
      Value *sock_val = interpreter_eval_expression(interp, node->arguments[0]);
      if (sock_val->type == VAL_INT) {
        SOCKET sock = (SOCKET)sock_val->data.int_val;
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        value_free(sock_val);
        return value_create_void();
      }
      value_free(sock_val);
      return value_create_void();
    }

    // socket_server(host, port)
    if (strcmp(node->name, "socket_server") == 0 && node->argument_count >= 2) {
      Value *host_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *port_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (host_val->type == VAL_STRING && port_val->type == VAL_INT) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }

        // Allow address reuse
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt));

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(host_val->data.string_val);
        server.sin_port = htons((unsigned short)port_val->data.int_val);

        if (bind(sock, (struct sockaddr *)&server, sizeof(server)) ==
            SOCKET_ERROR) {
#ifdef _WIN32
          closesocket(sock);
#else
          close(sock);
#endif
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }

        if (listen(sock, 5) == SOCKET_ERROR) {
#ifdef _WIN32
          closesocket(sock);
#else
          close(sock);
#endif
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }

        value_free(host_val);
        value_free(port_val);
        return value_create_int((long long)sock);
      }
      value_free(host_val);
      value_free(port_val);
      return value_create_int(-1);
    }

    // socket_client(host, port)
    if (strcmp(node->name, "socket_client") == 0 && node->argument_count >= 2) {
      Value *host_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *port_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (host_val->type == VAL_STRING && port_val->type == VAL_INT) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }

        struct sockaddr_in server;
        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(host_val->data.string_val);
        server.sin_port = htons((unsigned short)port_val->data.int_val);

        if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
#ifdef _WIN32
          closesocket(sock);
#else
          close(sock);
#endif
          value_free(host_val);
          value_free(port_val);
          return value_create_int(-1);
        }

        value_free(host_val);
        value_free(port_val);
        return value_create_int((long long)sock);
      }
      value_free(host_val);
      value_free(port_val);
      return value_create_int(-1);
    }

    // socket_select(read_fds, timeout_ms)
    // read_fds: array of integers (socket fds)
    // timeout_ms: int (milliseconds)
    // returns: array of integers (ready socket fds)
    if (strcmp(node->name, "socket_select") == 0 && node->argument_count >= 2) {
      Value *fds_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *timeout_val =
          interpreter_eval_expression(interp, node->arguments[1]);

      if (fds_val->type == VAL_ARRAY && timeout_val->type == VAL_INT) {
        fd_set readfds;
        FD_ZERO(&readfds);
        int max_fd = 0;

        // Add fds to set
        for (int i = 0; i < fds_val->data.array_val->length; i++) {
          Value *fd_v = fds_val->data.array_val->elements[i];
          if (fd_v->type == VAL_INT) {
            SOCKET s = (SOCKET)fd_v->data.int_val;
            FD_SET(s, &readfds);
            if ((int)s > max_fd)
              max_fd = (int)s;
          }
        }

        struct timeval tv;
        tv.tv_sec = timeout_val->data.int_val / 1000;
        tv.tv_usec = (timeout_val->data.int_val % 1000) * 1000;

        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0) {
          value_free(fds_val);
          value_free(timeout_val);
          return value_create_array(0); // Error or interrupt
        }

        // Create result array
        Value *res_array = value_create_array(0);
        for (int i = 0; i < fds_val->data.array_val->length; i++) {
          Value *fd_v = fds_val->data.array_val->elements[i];
          if (fd_v->type == VAL_INT) {
            SOCKET s = (SOCKET)fd_v->data.int_val;
            if (FD_ISSET(s, &readfds)) {
              array_push(res_array->data.array_val,
                         value_create_int((long long)s));
            }
          }
        }

        value_free(fds_val);
        value_free(timeout_val);
        return res_array;
      }
      value_free(fds_val);
      value_free(timeout_val);
      return value_create_array(0);
    }

    // ========================================================================
    // FILE I/O FUNCTIONS
    // ========================================================================

    // read_file(path)
    if (strcmp(node->name, "read_file") == 0) {
      if (node->argument_count >= 1) {
        Value *path_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        if (path_val->type == VAL_STRING) {
          char *content = read_file_content(path_val->data.string_val);
          value_free(path_val);
          if (content) {
            Value *res = value_create_string(content);
            free(content);
            return res;
          }
          return value_create_string("");
        }
        value_free(path_val);
      }
      return value_create_string("");
    }

    // write_file(path, content)
    if (strcmp(node->name, "write_file") == 0) {
      if (node->argument_count >= 2) {
        Value *path_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        Value *content_val =
            interpreter_eval_expression(interp, node->arguments[1]);

        if (path_val->type == VAL_STRING && content_val->type == VAL_STRING) {
          write_file_content(path_val->data.string_val,
                             content_val->data.string_val);
        }

        value_free(path_val);
        value_free(content_val);
      }
      return value_create_void();
    }

    // append_file(path, content)
    if (strcmp(node->name, "append_file") == 0) {
      if (node->argument_count >= 2) {
        Value *path_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        Value *content_val =
            interpreter_eval_expression(interp, node->arguments[1]);

        if (path_val->type == VAL_STRING && content_val->type == VAL_STRING) {
          append_file_content(path_val->data.string_val,
                              content_val->data.string_val);
        }

        value_free(path_val);
        value_free(content_val);
      }
      return value_create_void();
    }

    // file_exists(path)
    if (strcmp(node->name, "file_exists") == 0) {
      if (node->argument_count >= 1) {
        Value *path_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        if (path_val->type == VAL_STRING) {
          int exists = file_exists_check(path_val->data.string_val);
          value_free(path_val);
          return value_create_bool(exists);
        }
        value_free(path_val);
      }
      return value_create_bool(0);
    }

    // ========================================================================
    // STRING FUNCTIONS
    // ========================================================================

    // trim(str)
    if (strcmp(node->name, "trim") == 0) {
      if (node->argument_count >= 1) {
        Value *str_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        if (str_val->type == VAL_STRING) {
          char *trimmed = string_trim(str_val->data.string_val);
          value_free(str_val);
          Value *res = value_create_string(trimmed);
          free(trimmed);
          return res;
        }
        value_free(str_val);
      }
      return value_create_string("");
    }

    // replace(str, old, new)
    if (strcmp(node->name, "replace") == 0) {
      if (node->argument_count >= 3) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        Value *old_val =
            interpreter_eval_expression(interp, node->arguments[1]);
        Value *new_val =
            interpreter_eval_expression(interp, node->arguments[2]);

        if (s_val->type == VAL_STRING && old_val->type == VAL_STRING &&
            new_val->type == VAL_STRING) {
          char *replaced =
              string_replace(s_val->data.string_val, old_val->data.string_val,
                             new_val->data.string_val);
          value_free(s_val);
          value_free(old_val);
          value_free(new_val);
          Value *res = value_create_string(replaced);
          free(replaced);
          return res;
        }
        value_free(s_val);
        value_free(old_val);
        value_free(new_val);
      }
      return value_create_string("");
    }

    // split(str, delimiter)
    if (strcmp(node->name, "split") == 0) {
      if (node->argument_count >= 2) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        Value *d_val = interpreter_eval_expression(interp, node->arguments[1]);

        if (s_val->type == VAL_STRING && d_val->type == VAL_STRING) {
          Value *arr = value_create_array(4);
          char *s = s_val->data.string_val;
          char *d = d_val->data.string_val;

          if (strlen(d) == 0) {
            // Empty delimiter: split by chars
            int len = utf8_strlen_cp(s);
            for (int i = 0; i < len; i++) {
              char *ch = utf8_char_at(s, i);
              array_push(arr->data.array_val, value_create_string(ch));
              free(ch);
            }
          } else {
            // Split by delimiter
            char *str_copy = strdup(s);
            char *token = strtok(str_copy, d);
            while (token) {
              array_push(arr->data.array_val, value_create_string(token));
              token = strtok(NULL, d);
            }
            free(str_copy);
          }

          value_free(s_val);
          value_free(d_val);
          return arr;
        }
        value_free(s_val);
        value_free(d_val);
      }
      return value_create_array(0);
    }

    // toUpper(str)
    if (strcmp(node->name, "toUpper") == 0) {
      if (node->argument_count >= 1) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        if (s_val->type == VAL_STRING) {
          char *upper = string_to_upper(s_val->data.string_val);
          value_free(s_val);
          Value *res = value_create_string(upper);
          free(upper);
          return res;
        }
        value_free(s_val);
      }
      return value_create_string("");
    }

    // toLower(str)
    if (strcmp(node->name, "toLower") == 0) {
      if (node->argument_count >= 1) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        if (s_val->type == VAL_STRING) {
          char *lower = string_to_lower(s_val->data.string_val);
          value_free(s_val);
          Value *res = value_create_string(lower);
          free(lower);
          return res;
        }
        value_free(s_val);
      }
      return value_create_string("");
    }

    // contains(str, sub)
    if (strcmp(node->name, "contains") == 0) {
      if (node->argument_count >= 2) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        Value *sub_val =
            interpreter_eval_expression(interp, node->arguments[1]);

        if (s_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
          int found = (strstr(s_val->data.string_val,
                              sub_val->data.string_val) != NULL);
          value_free(s_val);
          value_free(sub_val);
          return value_create_bool(found);
        }
        value_free(s_val);
        value_free(sub_val);
      }
      return value_create_bool(0);
    }

    // index_of(str, sub)
    if (strcmp(node->name, "index_of") == 0) {
      if (node->argument_count >= 2) {
        Value *s_val = interpreter_eval_expression(interp, node->arguments[0]);
        Value *sub_val =
            interpreter_eval_expression(interp, node->arguments[1]);

        if (s_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
          char *pos = strstr(s_val->data.string_val, sub_val->data.string_val);
          int idx = -1;
          if (pos) {
            idx = (int)(pos - s_val->data.string_val);
          }
          value_free(s_val);
          value_free(sub_val);
          return value_create_int(idx);
        }
        value_free(s_val);
        value_free(sub_val);
      }
      return value_create_int(-1);
    }

    // print() fonksiyonu
    if (strcmp(node->name, "print") == 0) {
      for (int i = 0; i < node->argument_count; i++) {
        Value *val = interpreter_eval_expression(interp, node->arguments[i]);
        value_print(val);
        if (i < node->argument_count - 1) {
          printf(" ");
        }
        value_free(val);
      }
      printf("\n");
      return value_create_void();
    }

    // input() fonksiyonu - string okur
    if (strcmp(node->name, "input") == 0) {
      // Prompt varsa yazdır
      if (node->argument_count > 0) {
        Value *prompt = interpreter_eval_expression(interp, node->arguments[0]);
        if (prompt->type == VAL_STRING) {
          printf("%s", prompt->data.string_val);
          fflush(stdout);
        }
        value_free(prompt);
      }

      // Kullanıcıdan input al
      char buffer[1024];
      if (fgets(buffer, sizeof(buffer), stdin)) {
        // Satır sonu karakterini kaldır
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
          buffer[len - 1] = '\0';
        }
        return value_create_string(buffer);
      }
      return value_create_string("");
    }

    // inputInt() fonksiyonu - integer okur
    if (strcmp(node->name, "inputInt") == 0) {
      // Prompt varsa yazdır
      if (node->argument_count > 0) {
        Value *prompt = interpreter_eval_expression(interp, node->arguments[0]);
        if (prompt->type == VAL_STRING) {
          printf("%s", prompt->data.string_val);
          fflush(stdout);
        }
        value_free(prompt);
      }

      // Kullanıcıdan input al
      long long num;
      if (scanf("%lld", &num) == 1) {
        // Buffer'ı temizle
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
          ;
        return value_create_int(num);
      }
      return value_create_int(0);
    }

    // inputFloat() fonksiyonu - float okur
    if (strcmp(node->name, "inputFloat") == 0) {
      // Prompt varsa yazdır
      if (node->argument_count > 0) {
        Value *prompt = interpreter_eval_expression(interp, node->arguments[0]);
        if (prompt->type == VAL_STRING) {
          printf("%s", prompt->data.string_val);
          fflush(stdout);
        }
        value_free(prompt);
      }

      // Kullanıcıdan input al
      float num;
      if (scanf("%f", &num) == 1) {
        // Buffer'ı temizle
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
          ;
        return value_create_float(num);
      }
      return value_create_float(0.0f);
    }

    // range() function - for foreach
    if (strcmp(node->name, "range") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        if (arg->type == VAL_INT) {
          long long count = arg->data.int_val;
          value_free(arg);
          return value_create_int(count);
        }
        value_free(arg);
      }
      return value_create_int(0);
    }

    // sleep() function - delay execution in milliseconds
    if (strcmp(node->name, "sleep") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        if (arg->type == VAL_INT) {
          long long ms = arg->data.int_val;
#ifdef _WIN32
          Sleep((DWORD)ms);
#else
          usleep(ms * 1000);
#endif
          value_free(arg);
          return value_create_void();
        }
        value_free(arg);
      }
      return value_create_void();
    }

    // ============================================
    // THREADING FUNCTIONS (Built-in)
    // ============================================

    // thread_create(func_name, arg)
    if (strcmp(node->name, "thread_create") == 0) {
      if (node->argument_count >= 1) {
        Value *func_name_val =
            interpreter_eval_expression(interp, node->arguments[0]);
        Value *arg_val = NULL;
        if (node->argument_count > 1) {
          arg_val = interpreter_eval_expression(interp, node->arguments[1]);
        } else {
          arg_val = value_create_void();
        }

        if (func_name_val->type == VAL_STRING) {
          // Thread Args hazırla
          ThreadArgs *args = (ThreadArgs *)malloc(sizeof(ThreadArgs));
          args->parent_interp = interp;
          args->func_name = strdup(func_name_val->data.string_val);
          // Argümanı kopyala (deep copy safe değilse bile value_copy yap)
          // Aslında arg_val ownership'i ThreadArgs'a geçecek, çünkü burada free
          // etmeyeceğiz Ama ThreadArgs içinde deep copy yapmazsak, ana thread
          // free ederse sorun olur? value_create_xxx ile oluşturulan değer
          // malloc'ludur. arg_val burada free edilmeyecek, thread içinde free
          // edilecek. Ancak interpreter_eval_expression yeni bir value
          // döndürür. Bu value'nun sahipliğini args'a veriyoruz.
          args->arg = arg_val; // Sahiplik devri

#ifdef _WIN32
          HANDLE hThread =
              CreateThread(NULL,               // default security attributes
                           0,                  // use default stack size
                           thread_entry_point, // thread function name
                           args,               // argument to thread function
                           0,                  // use default creation flags
                           NULL);              // returns the thread identifier

          if (hThread == NULL) {
            printf("Error creating thread\n");
            free(args->func_name);
            free(args);
            value_free(arg_val);
          } else {
            // Thread handle döndür (VAL_THREAD eklenmeli value_create_thread)
            // Manuel oluştur:
            Value *val_t = (Value *)malloc(sizeof(Value));
            val_t->type = VAL_THREAD;
            val_t->data.thread_val = hThread;
            value_free(func_name_val);
            return val_t;
          }
#else
          // POSIX TODO
#endif
        }
        value_free(func_name_val);
        // arg_val sahipliği devredilmediyse free et (hata durumu)
      }
      return value_create_void();
    }

    // mutex_create()
    if (strcmp(node->name, "mutex_create") == 0) {
#ifdef _WIN32
      CRITICAL_SECTION *cs =
          (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
      InitializeCriticalSection(cs);

      Value *val_m = (Value *)malloc(sizeof(Value));
      val_m->type = VAL_MUTEX;
      val_m->data.mutex_val = cs;
      return val_m;
#else
      // POSIX PTHREAD_MUTEX_INITIALIZER? Dynamic -> pthread_mutex_init
#endif
      return value_create_void();
    }

    // mutex_lock(mutex)
    if (strcmp(node->name, "mutex_lock") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        if (arg->type == VAL_MUTEX) {
#ifdef _WIN32
          EnterCriticalSection(arg->data.mutex_val);
#endif
        }
        value_free(arg); // Handle kopyasını free et, mutex'in kendisini değil
      }
      return value_create_void();
    }

    // mutex_unlock(mutex)
    if (strcmp(node->name, "mutex_unlock") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        if (arg->type == VAL_MUTEX) {
#ifdef _WIN32
          LeaveCriticalSection(arg->data.mutex_val);
#endif
        }
        value_free(arg);
      }
      return value_create_void();
    }

    // toInt() - type conversion
    if (strcmp(node->name, "toInt") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        Value *result = NULL;

        if (arg->type == VAL_INT) {
          result = value_create_int(arg->data.int_val);
        } else if (arg->type == VAL_FLOAT) {
          result = value_create_int((long long)arg->data.float_val);
        } else if (arg->type == VAL_BOOL) {
          result = value_create_int(arg->data.bool_val ? 1 : 0);
        } else if (arg->type == VAL_STRING) {
          char *endptr = NULL;
          long long v = strtoll(arg->data.string_val, &endptr, 10);
          result = value_create_int(v);
        } else {
          printf("Error: Unsupported type for toInt() (line %d)\n", node->line);
        }

        value_free(arg);
        return result ? result : value_create_int(0);
      }
      return value_create_int(0);
    }

    // toFloat() - type conversion
    if (strcmp(node->name, "toFloat") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        Value *result = NULL;

        if (arg->type == VAL_INT) {
          result = value_create_float((float)arg->data.int_val);
        } else if (arg->type == VAL_FLOAT) {
          result = value_create_float(arg->data.float_val);
        } else if (arg->type == VAL_BOOL) {
          result = value_create_float(arg->data.bool_val ? 1.0f : 0.0f);
        } else if (arg->type == VAL_STRING) {
          result = value_create_float(atof(arg->data.string_val));
        } else {
          printf("Error: Unsupported type for toFloat() (line %d)\n",
                 node->line);
        }

        value_free(arg);
        return result ? result : value_create_float(0.0f);
      }
      return value_create_float(0.0f);
    }

    // toString() - type conversion
    if (strcmp(node->name, "toString") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        char buffer[256];

        if (arg->type == VAL_INT) {
          snprintf(buffer, sizeof(buffer), "%lld", arg->data.int_val);
        } else if (arg->type == VAL_FLOAT) {
          snprintf(buffer, sizeof(buffer), "%g", arg->data.float_val);
        } else if (arg->type == VAL_BOOL) {
          snprintf(buffer, sizeof(buffer), "%s",
                   arg->data.bool_val ? "true" : "false");
        } else if (arg->type == VAL_STRING) {
          value_free(arg);
          return value_create_string(arg->data.string_val);
        } else if (arg->type == VAL_OBJECT || arg->type == VAL_ARRAY) {
          char *js = value_to_json_string(arg);
          Value *s = value_create_string(js);
          free(js);
          value_free(arg);
          return s;
        } else {
          printf("Error: Unsupported type for toString() (line %d)\n",
                 node->line);
          buffer[0] = '\0';
        }

        value_free(arg);
        return value_create_string(buffer);
      }
      return value_create_string("");
    }

    // toJson(value, pretty=false) - JSON string üret
    if (strcmp(node->name, "toJson") == 0) {
      if (node->argument_count >= 1) {
        int pretty = 0;
        if (node->argument_count >= 2) {
          Value *pv = interpreter_eval_expression(interp, node->arguments[1]);
          pretty = (pv->type == VAL_BOOL && pv->data.bool_val);
          value_free(pv);
        }
        Value *v = interpreter_eval_expression(interp, node->arguments[0]);
        char *js;
        if (!pretty) {
          js = value_to_json_string(v);
        } else {
          // Pretty-print: basit girintileme (2 boşluk)
          // Not: hızlı çözüm olarak minify çıktısını kullanıyoruz; genişletme
          // ileri sürümde
          js = value_to_json_string(v);
        }
        value_free(v);
        Value *s = value_create_string(js);
        free(js);
        return s;
      }
      return value_create_string("null");
    }

    // fromJson(jsonStr) veya fromJson("TypeName", jsonStr, strict=true)
    if (strcmp(node->name, "fromJson") == 0) {
      if (node->argument_count >= 1) {
        int arg_idx = 0;
        TypeDef *target = NULL;
        int strict = 1;
        if (node->argument_count == 2) {
          Value *tname =
              interpreter_eval_expression(interp, node->arguments[0]);
          if (tname->type == VAL_STRING) {
            target = interpreter_get_type(interp, tname->data.string_val);
          }
          value_free(tname);
          arg_idx = 1;
        } else if (node->argument_count >= 3) {
          Value *tname =
              interpreter_eval_expression(interp, node->arguments[0]);
          if (tname->type == VAL_STRING) {
            target = interpreter_get_type(interp, tname->data.string_val);
          }
          value_free(tname);
          Value *sv = interpreter_eval_expression(interp, node->arguments[2]);
          strict = (sv->type != VAL_BOOL) ? 1 : sv->data.bool_val;
          value_free(sv);
          arg_idx = 1;
        }
        Value *s =
            interpreter_eval_expression(interp, node->arguments[arg_idx]);
        if (s->type != VAL_STRING) {
          value_free(s);
          return value_create_void();
        }
        JsonCur cur = {s->data.string_val, 0};
        Value *v = json_parse_value(&cur);
        value_free(s);
        if (!target)
          return v;
        // target tipe döndür (object beklenir)
        if (v->type != VAL_OBJECT) {
          value_free(v);
          return value_create_void();
        }
        // İşaretle ve tip kontrolü
        hash_table_set(v->data.object_val, "__type",
                       value_create_string(target->name));
        for (int i = 0; i < target->field_count; i++) {
          Value *fld =
              hash_table_get(v->data.object_val, target->field_names[i]);
          if (!fld) {
            // default varsa uygula
            if (target->field_defaults && target->field_defaults[i]) {
              Value *dv = interpreter_eval_expression(
                  interp, target->field_defaults[i]);
              hash_table_set(v->data.object_val, target->field_names[i], dv);
            } else if (strict) {
              printf("Error: Missing field in JSON: %s (fromJson)\n",
                     target->field_names[i]);
              value_free(v);
              exit(1);
            } else {
              // lenient: leave missing field as void
              hash_table_set(v->data.object_val, target->field_names[i],
                             value_create_void());
            }
          }
        }
        return v;
      }
      return value_create_void();
    }

    // toBool() - type conversion
    if (strcmp(node->name, "toBool") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);

        // Detailed type warning (optional): inform about unexpected types
        if (!(arg->type == VAL_INT || arg->type == VAL_FLOAT ||
              arg->type == VAL_BOOL || arg->type == VAL_STRING)) {
          const char *t = "unknown";
          switch (arg->type) {
          case VAL_ARRAY:
            t = "array";
            break;
          case VAL_OBJECT:
            t = "object";
            break;
          case VAL_BIGINT:
            t = "bigint";
            break;
          case VAL_VOID:
            t = "void";
            break;
          default:
            break;
          }
          printf("Warning: Unexpected type for toBool(): %s (applying "
                 "truthiness) (line %d)\n",
                 t, node->line);
        }

        int result = value_is_truthy(arg);
        value_free(arg);
        return value_create_bool(result);
      }
      return value_create_bool(0);
    }

    // length() - dizi veya string uzunluğu
    if (strcmp(node->name, "length") == 0) {
      if (node->argument_count > 0) {
        Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
        int len = 0;

        if (arg->type == VAL_ARRAY) {
          len = arg->data.array_val->length;
        } else if (arg->type == VAL_STRING) {
          len = strlen(arg->data.string_val);
        } else if (arg->type == VAL_OBJECT) {
          // object uzunluğu: entry sayısı
          len = arg->data.object_val->size;
        } else {
          printf("Error: length() only for array/object/string (line %d)\n",
                 node->line);
        }

        value_free(arg);
        return value_create_int(len);
      }
      return value_create_int(0);
    }

    // push() - diziye eleman ekle
    if (strcmp(node->name, "push") == 0) {
      if (node->argument_count >= 2) {
        // İlk argüman: dizi değişkeni (identifier olarak gelir)
        if (node->arguments[0]->type == AST_IDENTIFIER) {
          char *arr_name = node->arguments[0]->name;
          Value *arr_val = symbol_table_get(interp->current_scope, arr_name);

          if (arr_val && arr_val->type == VAL_ARRAY) {
            Value *elem =
                interpreter_eval_expression(interp, node->arguments[1]);
            array_push(arr_val->data.array_val, elem);
            value_free(elem);
            return value_create_void();
          } else {
            printf("Error: push() first argument must be array (line %d)\n",
                   node->line);
          }
        }
      }
      return value_create_void();
    }

    // pop() - diziden eleman çıkar
    if (strcmp(node->name, "pop") == 0) {
      if (node->argument_count >= 1) {
        // İlk argüman: dizi değişkeni (identifier olarak gelir)
        if (node->arguments[0]->type == AST_IDENTIFIER) {
          char *arr_name = node->arguments[0]->name;
          Value *arr_val = symbol_table_get(interp->current_scope, arr_name);

          if (arr_val && arr_val->type == VAL_ARRAY) {
            return array_pop(arr_val->data.array_val);
          } else {
            printf("Error: pop() first argument must be array (line %d)\n",
                   node->line);
          }
        }
      }
      return value_create_void();
    }

// ========================================================================
// MATEMATİK FONKSİYONLARI
// ========================================================================

// Helper: Get numeric value as double
#define GET_NUM_ARG(idx)                                                       \
  ({                                                                           \
    Value *_arg = interpreter_eval_expression(interp, node->arguments[idx]);   \
    double _val = 0.0;                                                         \
    if (_arg->type == VAL_INT)                                                 \
      _val = (double)_arg->data.int_val;                                       \
    else if (_arg->type == VAL_FLOAT)                                          \
      _val = (double)_arg->data.float_val;                                     \
    value_free(_arg);                                                          \
    _val;                                                                      \
  })

    // abs(x) - mutlak değer
    if (strcmp(node->name, "abs") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)fabs(x));
    }

    // sqrt(x) - karekök
    if (strcmp(node->name, "sqrt") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)sqrt(x));
    }

    // pow(x, y) - üs alma
    if (strcmp(node->name, "pow") == 0 && node->argument_count >= 2) {
      // Eğer iki argüman da integer ise BigInt ile hesapla
      Value *a0 = interpreter_eval_expression(interp, node->arguments[0]);
      Value *a1 = interpreter_eval_expression(interp, node->arguments[1]);
      Value *out = NULL;
      if ((a0->type == VAL_INT || a0->type == VAL_BIGINT) &&
          (a1->type == VAL_INT)) {
        const char *base = (a0->type == VAL_BIGINT)
                               ? a0->data.bigint_val
                               : bigint_from_ll_str(a0->data.int_val);
        long long e = a1->data.int_val;
        if (e < 0) {
          // Negatif üs desteklenmiyor -> float
          double xd =
              (a0->type == VAL_BIGINT) ? atof(base) : (double)a0->data.int_val;
          out = value_create_float((float)pow(xd, (double)e));
        } else {
          char *p = bigint_pow_str(base, e);
          out = value_create_bigint(p);
          free(p);
        }
        if (a0->type != VAL_BIGINT)
          free((char *)base);
      } else {
        if (!((a0->type == VAL_INT || a0->type == VAL_FLOAT) &&
              (a1->type == VAL_INT || a1->type == VAL_FLOAT))) {
          printf("Error: pow() expects numeric arguments (line %d)\n",
                 node->line);
          value_free(a0);
          value_free(a1);
          exit(1);
        }
        double x = (a0->type == VAL_FLOAT) ? a0->data.float_val
                                           : (double)a0->data.int_val;
        double y = (a1->type == VAL_FLOAT) ? a1->data.float_val
                                           : (double)a1->data.int_val;
        out = value_create_float((float)pow(x, y));
      }
      value_free(a0);
      value_free(a1);
      return out;
    }

    // mod(a, b) - tamsayı mod (BigInt destekli)
    if (strcmp(node->name, "mod") == 0 && node->argument_count >= 2) {
      Value *a0 = interpreter_eval_expression(interp, node->arguments[0]);
      Value *a1 = interpreter_eval_expression(interp, node->arguments[1]);
      Value *out = NULL;
      if ((a0->type == VAL_BIGINT) || (a1->type == VAL_BIGINT)) {
        const char *la = (a0->type == VAL_BIGINT)
                             ? a0->data.bigint_val
                             : bigint_from_ll_str(a0->data.int_val);
        const char *rb = (a1->type == VAL_BIGINT)
                             ? a1->data.bigint_val
                             : bigint_from_ll_str(a1->data.int_val);
        // Modulo by zero check
        if (strcmp(rb, "0") == 0 || (strlen(rb) == 1 && rb[0] == '0')) {
          printf("Error: Modulo by zero! (line %d)\n", node->line);
          if (a0->type != VAL_BIGINT)
            free((char *)la);
          if (a1->type != VAL_BIGINT)
            free((char *)rb);
          value_free(a0);
          value_free(a1);
          exit(1);
        }
        char *q;
        char *r;
        bigint_divmod(la, rb, &q, &r);
        out = value_create_bigint(r);
        free(q);
        free(r);
        if (a0->type != VAL_BIGINT)
          free((char *)la);
        if (a1->type != VAL_BIGINT)
          free((char *)rb);
      } else {
        if (!(a0->type == VAL_INT && a1->type == VAL_INT)) {
          printf("Error: mod() expects integer arguments (line %d)\n",
                 node->line);
          value_free(a0);
          value_free(a1);
          exit(1);
        }
        if (a1->data.int_val == 0) {
          printf("Error: Modulo by zero! (line %d)\n", node->line);
          value_free(a0);
          value_free(a1);
          exit(1);
        }
        out = value_create_int(a0->data.int_val % a1->data.int_val);
      }
      value_free(a0);
      value_free(a1);
      return out;
    }

    // floor(x) - aşağı yuvarlama
    if (strcmp(node->name, "floor") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_int((int)floor(x));
    }

    // ceil(x) - yukarı yuvarlama
    if (strcmp(node->name, "ceil") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_int((int)ceil(x));
    }

    // round(x) - yuvarlama
    if (strcmp(node->name, "round") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_int((int)round(x));
    }

    // sin(x) - sinüs
    if (strcmp(node->name, "sin") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)sin(x));
    }

    // cos(x) - kosinüs
    if (strcmp(node->name, "cos") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)cos(x));
    }

    // tan(x) - tanjant
    if (strcmp(node->name, "tan") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)tan(x));
    }

    // asin(x) - arcsinüs
    if (strcmp(node->name, "asin") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)asin(x));
    }

    // acos(x) - arckosinüs
    if (strcmp(node->name, "acos") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)acos(x));
    }

    // atan(x) - arctanjant
    if (strcmp(node->name, "atan") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)atan(x));
    }

    // atan2(y, x) - iki argümanlı arctanjant
    if (strcmp(node->name, "atan2") == 0 && node->argument_count >= 2) {
      double y = GET_NUM_ARG(0);
      double x = GET_NUM_ARG(1);
      return value_create_float((float)atan2(y, x));
    }

    // exp(x) - e üzeri x
    if (strcmp(node->name, "exp") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)exp(x));
    }

    // log(x) - doğal logaritma (ln)
    if (strcmp(node->name, "log") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)log(x));
    }

    // log10(x) - 10 tabanlı logaritma
    if (strcmp(node->name, "log10") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)log10(x));
    }

    // log2(x) - 2 tabanlı logaritma
    if (strcmp(node->name, "log2") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)log2(x));
    }

    // sinh(x) - hiperbolik sinüs
    if (strcmp(node->name, "sinh") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)sinh(x));
    }

    // cosh(x) - hiperbolik kosinüs
    if (strcmp(node->name, "cosh") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)cosh(x));
    }

    // tanh(x) - hiperbolik tanjant
    if (strcmp(node->name, "tanh") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)tanh(x));
    }

    // min(a, b, ...) - minimum değer
    if (strcmp(node->name, "min") == 0 && node->argument_count >= 1) {
      double min_val = GET_NUM_ARG(0);
      for (int i = 1; i < node->argument_count; i++) {
        double val = GET_NUM_ARG(i);
        if (val < min_val)
          min_val = val;
      }
      return value_create_float((float)min_val);
    }

    // max(a, b, ...) - maximum değer
    if (strcmp(node->name, "max") == 0 && node->argument_count >= 1) {
      double max_val = GET_NUM_ARG(0);
      for (int i = 1; i < node->argument_count; i++) {
        double val = GET_NUM_ARG(i);
        if (val > max_val)
          max_val = val;
      }
      return value_create_float((float)max_val);
    }

    // random() - 0 ile 1 arası rastgele sayı
    if (strcmp(node->name, "random") == 0) {
      static int seeded = 0;
      if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
      }
      return value_create_float((float)rand() / (float)RAND_MAX);
    }

    // randint(a, b) - a ile b arası rastgele tam sayı
    if (strcmp(node->name, "randint") == 0 && node->argument_count >= 2) {
      static int seeded = 0;
      if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
      }
      int a = (int)GET_NUM_ARG(0);
      int b = (int)GET_NUM_ARG(1);
      int result = a + rand() % (b - a + 1);
      return value_create_int(result);
    }

    // cbrt(x) - küp kök
    if (strcmp(node->name, "cbrt") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_float((float)cbrt(x));
    }

    // hypot(x, y) - hipotenüs (sqrt(x^2 + y^2))
    if (strcmp(node->name, "hypot") == 0 && node->argument_count >= 2) {
      double x = GET_NUM_ARG(0);
      double y = GET_NUM_ARG(1);
      return value_create_float((float)hypot(x, y));
    }

    // fmod(x, y) - kayan nokta mod
    if (strcmp(node->name, "fmod") == 0 && node->argument_count >= 2) {
      double x = GET_NUM_ARG(0);
      double y = GET_NUM_ARG(1);
      return value_create_float((float)fmod(x, y));
    }

// ========================================================================
// STRING FONKSİYONLARI
// ========================================================================

// Helper: Get string argument
#define GET_STR_ARG(idx)                                                       \
  ({                                                                           \
    Value *_arg = interpreter_eval_expression(interp, node->arguments[idx]);   \
    char *_str = NULL;                                                         \
    if (_arg->type == VAL_STRING)                                              \
      _str = _arg->data.string_val;                                            \
    _str;                                                                      \
  })

    // upper(s) - convert to uppercase
    if (strcmp(node->name, "upper") == 0 && node->argument_count >= 1) {
      Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
      if (arg->type == VAL_STRING) {
        // Optimize: allocate once, value_create_string will copy
        int len = (int)strlen(arg->data.string_val);
        char *str = (char *)malloc((size_t)len + 1);
        strcpy(str, arg->data.string_val);
        for (int i = 0; str[i]; i++) {
          str[i] = toupper((unsigned char)str[i]);
        }
        value_free(arg);
        Value *result = value_create_string(str);
        free(str);
        return result;
      }
      value_free(arg);
      return value_create_string("");
    }

    // lower(s) - convert to lowercase
    if (strcmp(node->name, "lower") == 0 && node->argument_count >= 1) {
      Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
      if (arg->type == VAL_STRING) {
        // Optimize: allocate once, value_create_string will copy
        int len = (int)strlen(arg->data.string_val);
        char *str = (char *)malloc((size_t)len + 1);
        strcpy(str, arg->data.string_val);
        for (int i = 0; str[i]; i++) {
          str[i] = tolower((unsigned char)str[i]);
        }
        value_free(arg);
        Value *result = value_create_string(str);
        free(str);
        return result;
      }
      value_free(arg);
      return value_create_string("");
    }

    // trim(s) - baş ve sondaki boşlukları sil
    if (strcmp(node->name, "trim") == 0 && node->argument_count >= 1) {
      Value *arg = interpreter_eval_expression(interp, node->arguments[0]);
      if (arg->type == VAL_STRING) {
        char *str = arg->data.string_val;
        char *start = str;
        char *end = str + strlen(str) - 1;

        // Baştan boşlukları atla
        while (*start && isspace((unsigned char)*start))
          start++;

        // Sondan boşlukları atla
        while (end > start && isspace((unsigned char)*end))
          end--;

        // Yeni string oluştur
        int len = end - start + 1;
        char *result_str = (char *)malloc(len + 1);
        strncpy(result_str, start, len);
        result_str[len] = '\0';

        value_free(arg);
        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }
      value_free(arg);
      return value_create_string("");
    }

    // replace(s, old, new) - değiştir
    if (strcmp(node->name, "replace") == 0 && node->argument_count >= 3) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *old_val = interpreter_eval_expression(interp, node->arguments[1]);
      Value *new_val = interpreter_eval_expression(interp, node->arguments[2]);

      if (str_val->type == VAL_STRING && old_val->type == VAL_STRING &&
          new_val->type == VAL_STRING) {
        char *str = str_val->data.string_val;
        char *old = old_val->data.string_val;
        char *new = new_val->data.string_val;

        int old_len = strlen(old);
        int new_len = strlen(new);

        // Kaç kez geçiyor say
        int count = 0;
        char *p = str;
        while ((p = strstr(p, old)) != NULL) {
          count++;
          p += old_len;
        }

        // Yeni string için yer ayır
        int result_len = strlen(str) + count * (new_len - old_len);
        char *result_str = (char *)malloc(result_len + 1);
        char *dst = result_str;

        // Replace işlemi
        p = str;
        char *found;
        while ((found = strstr(p, old)) != NULL) {
          int prefix_len = found - p;
          strncpy(dst, p, prefix_len);
          dst += prefix_len;
          strcpy(dst, new);
          dst += new_len;
          p = found + old_len;
        }
        strcpy(dst, p);

        value_free(str_val);
        value_free(old_val);
        value_free(new_val);

        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }

      value_free(str_val);
      value_free(old_val);
      value_free(new_val);
      return value_create_string("");
    }

    // contains(s, sub) - alt string var mı
    if (strcmp(node->name, "contains") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *sub_val = interpreter_eval_expression(interp, node->arguments[1]);

      int result = 0;
      if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
        result = (strstr(str_val->data.string_val, sub_val->data.string_val) !=
                  NULL);
      }

      value_free(str_val);
      value_free(sub_val);
      return value_create_bool(result);
    }

    // startsWith(s, prefix) - ile başlıyor mu
    if (strcmp(node->name, "startsWith") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *prefix_val =
          interpreter_eval_expression(interp, node->arguments[1]);

      int result = 0;
      if (str_val->type == VAL_STRING && prefix_val->type == VAL_STRING) {
        result = (strncmp(str_val->data.string_val, prefix_val->data.string_val,
                          strlen(prefix_val->data.string_val)) == 0);
      }

      value_free(str_val);
      value_free(prefix_val);
      return value_create_bool(result);
    }

    // endsWith(s, suffix) - ile bitiyor mu
    if (strcmp(node->name, "endsWith") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *suffix_val =
          interpreter_eval_expression(interp, node->arguments[1]);

      int result = 0;
      if (str_val->type == VAL_STRING && suffix_val->type == VAL_STRING) {
        int str_len = strlen(str_val->data.string_val);
        int suffix_len = strlen(suffix_val->data.string_val);

        if (suffix_len <= str_len) {
          result = (strcmp(str_val->data.string_val + str_len - suffix_len,
                           suffix_val->data.string_val) == 0);
        }
      }

      value_free(str_val);
      value_free(suffix_val);
      return value_create_bool(result);
    }

    // indexOf(s, sub) - ilk konum (-1 = bulunamadı)
    if (strcmp(node->name, "indexOf") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *sub_val = interpreter_eval_expression(interp, node->arguments[1]);

      int result = -1;
      if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
        char *found =
            strstr(str_val->data.string_val, sub_val->data.string_val);
        if (found) {
          result = found - str_val->data.string_val;
        }
      }

      value_free(str_val);
      value_free(sub_val);
      return value_create_int(result);
    }

    // substring(s, start, end) - alt string
    if (strcmp(node->name, "substring") == 0 && node->argument_count >= 3) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *start_val =
          interpreter_eval_expression(interp, node->arguments[1]);
      Value *end_val = interpreter_eval_expression(interp, node->arguments[2]);

      if (str_val->type == VAL_STRING && start_val->type == VAL_INT &&
          end_val->type == VAL_INT) {
        char *str = str_val->data.string_val;
        int start = start_val->data.int_val;
        int end = end_val->data.int_val;
        int len = strlen(str);

        // Sınır kontrolleri
        if (start < 0)
          start = 0;
        if (end > len)
          end = len;
        if (start > end)
          start = end;

        int sub_len = end - start;
        char *result_str = (char *)malloc(sub_len + 1);
        strncpy(result_str, str + start, sub_len);
        result_str[sub_len] = '\0';

        value_free(str_val);
        value_free(start_val);
        value_free(end_val);

        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }

      value_free(str_val);
      value_free(start_val);
      value_free(end_val);
      return value_create_string("");
    }

    // repeat(s, n) - tekrarla
    if (strcmp(node->name, "repeat") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *n_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (str_val->type == VAL_STRING && n_val->type == VAL_INT) {
        char *str = str_val->data.string_val;
        int n = n_val->data.int_val;
        int str_len = strlen(str);

        if (n <= 0) {
          value_free(str_val);
          value_free(n_val);
          return value_create_string("");
        }

        char *result_str = (char *)malloc(str_len * n + 1);
        result_str[0] = '\0';

        for (int i = 0; i < n; i++) {
          strcat(result_str, str);
        }

        value_free(str_val);
        value_free(n_val);

        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }

      value_free(str_val);
      value_free(n_val);
      return value_create_string("");
    }

    // reverse(s) - ters çevir
    if (strcmp(node->name, "reverse") == 0 && node->argument_count >= 1) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);

      if (str_val->type == VAL_STRING) {
        char *str = str_val->data.string_val;
        int len = strlen(str);
        char *result_str = (char *)malloc(len + 1);

        for (int i = 0; i < len; i++) {
          result_str[i] = str[len - 1 - i];
        }
        result_str[len] = '\0';

        value_free(str_val);
        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }

      value_free(str_val);
      return value_create_string("");
    }

    // isEmpty(s) - boş mu
    if (strcmp(node->name, "isEmpty") == 0 && node->argument_count >= 1) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);

      int result = 1;
      if (str_val->type == VAL_STRING) {
        result = (strlen(str_val->data.string_val) == 0);
      }

      value_free(str_val);
      return value_create_bool(result);
    }

    // count(s, sub) - kaç kez geçiyor
    if (strcmp(node->name, "count") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *sub_val = interpreter_eval_expression(interp, node->arguments[1]);

      int count = 0;
      if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
        char *str = str_val->data.string_val;
        char *sub = sub_val->data.string_val;
        int sub_len = strlen(sub);

        char *p = str;
        while ((p = strstr(p, sub)) != NULL) {
          count++;
          p += sub_len;
        }
      }

      value_free(str_val);
      value_free(sub_val);
      return value_create_int(count);
    }

    // capitalize(s) - ilk harf büyük
    if (strcmp(node->name, "capitalize") == 0 && node->argument_count >= 1) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);

      if (str_val->type == VAL_STRING) {
        char *str = strdup(str_val->data.string_val);

        if (str[0]) {
          str[0] = toupper((unsigned char)str[0]);
          for (int i = 1; str[i]; i++) {
            str[i] = tolower((unsigned char)str[i]);
          }
        }

        value_free(str_val);
        Value *result = value_create_string(str);
        free(str);
        return result;
      }

      value_free(str_val);
      return value_create_string("");
    }

    // isDigit(s) - sadece rakam mı
    if (strcmp(node->name, "isDigit") == 0 && node->argument_count >= 1) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);

      int result = 0;
      if (str_val->type == VAL_STRING && strlen(str_val->data.string_val) > 0) {
        result = 1;
        for (char *p = str_val->data.string_val; *p; p++) {
          if (!isdigit((unsigned char)*p)) {
            result = 0;
            break;
          }
        }
      }

      value_free(str_val);
      return value_create_bool(result);
    }

    // isAlpha(s) - sadece harf mi
    if (strcmp(node->name, "isAlpha") == 0 && node->argument_count >= 1) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);

      int result = 0;
      if (str_val->type == VAL_STRING && strlen(str_val->data.string_val) > 0) {
        result = 1;
        for (char *p = str_val->data.string_val; *p; p++) {
          if (!isalpha((unsigned char)*p)) {
            result = 0;
            break;
          }
        }
      }

      value_free(str_val);
      return value_create_bool(result);
    }

    // split(s, delimiter) - string'i böl ve dizi döndür
    if (strcmp(node->name, "split") == 0 && node->argument_count >= 2) {
      Value *str_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *delim_val =
          interpreter_eval_expression(interp, node->arguments[1]);

      if (str_val->type == VAL_STRING && delim_val->type == VAL_STRING) {
        char *str = strdup(str_val->data.string_val);
        char *delim = delim_val->data.string_val;

        // Önce kaç parça olacağını say
        int count = 1;
        char *temp = str;
        while ((temp = strstr(temp, delim)) != NULL) {
          count++;
          temp += strlen(delim);
        }

        // Dizi oluştur
        Value *arr = value_create_typed_array(count, VAL_STRING);

        // String'i böl
        char *token = str;
        char *next = NULL;
        for (int i = 0; i < count; i++) {
          next = strstr(token, delim);

          if (next) {
            *next = '\0';
            array_push(arr->data.array_val, value_create_string(token));
            token = next + strlen(delim);
          } else {
            array_push(arr->data.array_val, value_create_string(token));
          }
        }

        free(str);
        value_free(str_val);
        value_free(delim_val);
        return arr;
      }

      value_free(str_val);
      value_free(delim_val);
      return value_create_array(0);
    }

    // join(separator, array) - diziyi birleştir
    if (strcmp(node->name, "join") == 0 && node->argument_count >= 2) {
      Value *sep_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *arr_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (sep_val->type == VAL_STRING && arr_val->type == VAL_ARRAY) {
        char *sep = sep_val->data.string_val;
        Array *arr = arr_val->data.array_val;

        // Toplam uzunluk hesapla
        int total_len = 0;
        for (int i = 0; i < arr->length; i++) {
          if (arr->elements[i]->type == VAL_STRING) {
            total_len += strlen(arr->elements[i]->data.string_val);
          }
          if (i > 0) {
            total_len += strlen(sep);
          }
        }

        // Sonuç string'i oluştur
        char *result_str = (char *)malloc(total_len + 1);
        result_str[0] = '\0';

        for (int i = 0; i < arr->length; i++) {
          if (i > 0) {
            strcat(result_str, sep);
          }
          if (arr->elements[i]->type == VAL_STRING) {
            strcat(result_str, arr->elements[i]->data.string_val);
          }
        }

        value_free(sep_val);
        value_free(arr_val);

        Value *result = value_create_string(result_str);
        free(result_str);
        return result;
      }

      value_free(sep_val);
      value_free(arr_val);
      return value_create_string("");
    }

    // trunc(x) - ondalık kısmı atar
    if (strcmp(node->name, "trunc") == 0 && node->argument_count >= 1) {
      double x = GET_NUM_ARG(0);
      return value_create_int((int)trunc(x));
    }

    // http_parse_request(raw_request)
    if (strcmp(node->name, "http_parse_request") == 0 &&
        node->argument_count >= 1) {
      Value *raw_val = interpreter_eval_expression(interp, node->arguments[0]);
      if (raw_val->type != VAL_STRING) {
        value_free(raw_val);
        return value_create_object(); // Return empty object on error
      }

      char *raw = raw_val->data.string_val;
      Value *req_obj = value_create_object();

      // Parse Request Line: GET /path HTTP/1.1
      char *line_end = strstr(raw, "\r\n");
      if (line_end) {
        char *line = (char *)malloc(line_end - raw + 1);
        strncpy(line, raw, line_end - raw);
        line[line_end - raw] = '\0';

        char *method = strtok(line, " ");
        char *path = strtok(NULL, " ");
        // char *proto = strtok(NULL, " "); // Unused for now

        if (method)
          hash_table_set(req_obj->data.object_val, "method",
                         value_create_string(method));
        if (path)
          hash_table_set(req_obj->data.object_val, "path",
                         value_create_string(path));

        free(line);

        // Parse Headers
        Value *headers_obj = value_create_object();
        char *header_start = line_end + 2;
        while (1) {
          char *header_end = strstr(header_start, "\r\n");
          if (!header_end || header_end == header_start) {
            // End of headers
            if (header_end)
              header_start = header_end + 2; // Skip last CRLF
            break;
          }

          char *header_line = (char *)malloc(header_end - header_start + 1);
          strncpy(header_line, header_start, header_end - header_start);
          header_line[header_end - header_start] = '\0';

          char *colon = strchr(header_line, ':');
          if (colon) {
            *colon = '\0';
            char *key = header_line;
            char *val = colon + 1;
            while (*val == ' ')
              val++; // trim leading space
            hash_table_set(headers_obj->data.object_val, key,
                           value_create_string(val));
          }

          free(header_line);
          header_start = header_end + 2;
        }
        hash_table_set(req_obj->data.object_val, "headers", headers_obj);

        // Body
        if (*header_start) {
          hash_table_set(req_obj->data.object_val, "body",
                         value_create_string(header_start));
        } else {
          hash_table_set(req_obj->data.object_val, "body",
                         value_create_string(""));
        }
      } else {
        // Fallback if no CRLF found (maybe just one line or empty)
        hash_table_set(req_obj->data.object_val, "body",
                       value_create_string(""));
      }

      value_free(raw_val);
      return req_obj;
    }

    // http_create_response(status, content_type, body)
    if (strcmp(node->name, "http_create_response") == 0 &&
        node->argument_count >= 3) {
      Value *status_val =
          interpreter_eval_expression(interp, node->arguments[0]);
      Value *type_val = interpreter_eval_expression(interp, node->arguments[1]);
      Value *body_val = interpreter_eval_expression(interp, node->arguments[2]);

      int status =
          (status_val->type == VAL_INT) ? (int)status_val->data.int_val : 200;
      char *ctype = (type_val->type == VAL_STRING) ? type_val->data.string_val
                                                   : "text/plain";
      char *body =
          (body_val->type == VAL_STRING) ? body_val->data.string_val : "";

      char *status_text = "OK";
      if (status == 404)
        status_text = "Not Found";
      if (status == 500)
        status_text = "Internal Server Error";
      if (status == 400)
        status_text = "Bad Request";
      if (status == 201)
        status_text = "Created";

      // Calculate length
      int len = snprintf(NULL, 0,
                         "HTTP/1.1 %d %s\r\nContent-Type: "
                         "%s\r\nContent-Length: %zu\r\nConnection: "
                         "close\r\n\r\n%s",
                         status, status_text, ctype, strlen(body), body);

      char *response = (char *)malloc(len + 1);
      sprintf(response,
              "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: "
              "%zu\r\nConnection: close\r\n\r\n%s",
              status, status_text, ctype, strlen(body), body);

      value_free(status_val);
      value_free(type_val);
      Value *res = value_create_string(response);
      free(response);
      return res;
    }

    // DATABASE FUNCTIONS
    // ========================================================================

    // db_open(path)
    if (strcmp(node->name, "db_open") == 0 && node->argument_count >= 1) {
      // printf("DEBUG: db_open called\n");
      Value *path_val = interpreter_eval_expression(interp, node->arguments[0]);
      if (path_val->type != VAL_STRING) {
        value_free(path_val);
        return value_create_int(0); // NULL pointer basically
      }

      // printf("DEBUG: Opening database: %s\n", path_val->data.string_val);
      sqlite3 *db;
      int rc = sqlite3_open(path_val->data.string_val, &db);

      value_free(path_val);

      if (rc) {
        // printf("DEBUG: Failed to open database: %d\n", rc);
        sqlite3_close(db);
        return value_create_int(0);
      }

      // printf("DEBUG: Database opened successfully. DB ptr: %p\n", (void*)db);
      return value_create_int((long long)db);
    }

    // db_close(db)
    if (strcmp(node->name, "db_close") == 0 && node->argument_count >= 1) {
      Value *db_val = interpreter_eval_expression(interp, node->arguments[0]);
      if (db_val->type == VAL_INT) {
        sqlite3 *db = (sqlite3 *)db_val->data.int_val;
        if (db) {
          sqlite3_close(db);
        }
      }
      value_free(db_val);
      return value_create_void();
    }

    // db_query(db, sql)
    if (strcmp(node->name, "db_query") == 0 && node->argument_count >= 2) {
      // printf("DEBUG: db_query called\n");
      // fflush(stdout);
      Value *db_val = interpreter_eval_expression(interp, node->arguments[0]);
      Value *sql_val = interpreter_eval_expression(interp, node->arguments[1]);

      if (db_val->type == VAL_INT && sql_val->type == VAL_STRING) {
        sqlite3 *db = (sqlite3 *)db_val->data.int_val;
        char *sql = sql_val->data.string_val;
        char *err_msg = 0;

        // printf("DEBUG: Executing SQL: %s\n", sql);
        // fflush(stdout);

        // We will store results in a list of objects
        Value *result_list = value_create_array(0);

        int rc = sqlite3_exec(db, sql, tulpar_sqlite_callback,
                              (void *)result_list, &err_msg);

        value_free(db_val);
        value_free(sql_val);

        if (rc != SQLITE_OK) {
          printf("SQL Error: %s\n", err_msg);
          sqlite3_free(err_msg);
          return result_list;
        }

        // printf("DEBUG: SQL executed successfully\n");
        return result_list;
      }

      value_free(db_val);
      value_free(sql_val);
      return value_create_array(0);
    }

    // Kullanıcı tanımlı fonksiyonlar
    // Metot çağrısı mı? (receiver varsa)
    if (node->receiver) {
      Value *recv = interpreter_eval_expression(interp, node->receiver);
      if (recv->type != VAL_OBJECT) {
        printf("Error: Object expected for method call (line %d)\n",
               node->line);
        exit(1);
      }
      Value *mark = hash_table_get(recv->data.object_val, "__type");
      if (!mark || mark->type != VAL_STRING) {
        printf("Error: Type marker not found for method (line %d)\n",
               node->line);
        exit(1);
      }
      // Fonksiyon adı: TypeName.method
      char fullname[256];
      snprintf(fullname, sizeof(fullname), "%s.%s", mark->data.string_val,
               node->name);
      Function *m = interpreter_get_function(interp, fullname);
      if (!m) {
        printf("Error: Undefined method '%s' (line %d)\n", fullname,
               node->line);
        exit(1);
      }
      // Argümanları hazırla
      Value **arg_values =
          (Value **)malloc(sizeof(Value *) * (node->argument_count));
      for (int i = 0; i < node->argument_count; i++)
        arg_values[i] = interpreter_eval_expression(interp, node->arguments[i]);
      // Yeni scope ve self
      SymbolTable *old_scope = interp->current_scope;
      interp->current_scope = symbol_table_create(interp->global_scope);
      symbol_table_set(interp->current_scope, "self", recv);
      for (int i = 0; i < node->argument_count; i++) {
        symbol_table_set(interp->current_scope, m->node->parameters[i]->name,
                         arg_values[i]);
        value_free(arg_values[i]);
      }
      free(arg_values);
      value_free(recv);
      // Çalıştır
      interp->should_return = 0;
      interpreter_execute_statement(interp, m->node->body);
      Value *result = interp->return_value ? value_copy(interp->return_value)
                                           : value_create_void();
      symbol_table_free(interp->current_scope);
      interp->current_scope = old_scope;
      if (interp->return_value) {
        value_free(interp->return_value);
        interp->return_value = NULL;
      }
      interp->should_return = 0;
      return result;
    }

    Function *func = interpreter_get_function(interp, node->name);
    if (!func) {
      // Type constructor?
      TypeDef *t = interpreter_get_type(interp, node->name);
      if (t) {
        // Argümanları değerlendir ve object oluştur (named args destekli)
        Value *obj = value_create_object();
        // Type işareti
        hash_table_set(obj->data.object_val, "__type",
                       value_create_string(t->name));
        int used_fields = 0;
        int *filled = (int *)calloc(t->field_count, sizeof(int));
        // Named arg varsa eşle
        int has_named = 0;
        for (int i = 0; i < node->argument_count; i++) {
          if (node->argument_names && node->argument_names[i]) {
            has_named = 1;
            break;
          }
        }
        if (has_named) {
          for (int i = 0; i < node->argument_count; i++) {
            if (!node->argument_names[i]) {
              printf("Error: For type '%s', all arguments must be named or "
                     "none\n",
                     node->name);
              exit(1);
            }
            const char *fname = node->argument_names[i];
            int idx = -1;
            for (int k = 0; k < t->field_count; k++) {
              if (strcmp(t->field_names[k], fname) == 0) {
                idx = k;
                break;
              }
            }
            if (idx < 0) {
              printf("Error: Field '%s' not found in type '%s'\n", fname,
                     node->name);
              exit(1);
            }
            if (filled[idx]) {
              printf("Error: Field '%s' assigned twice in type '%s'\n", fname,
                     node->name);
              exit(1);
            }
            Value *arg =
                interpreter_eval_expression(interp, node->arguments[i]);
            // Type validation
            if (t->field_types[idx] == TYPE_INT && arg->type != VAL_INT) {
              printf("Error: Field '%s' must be int\n", t->field_names[idx]);
              exit(1);
            }
            if (t->field_types[idx] == TYPE_FLOAT &&
                !(arg->type == VAL_FLOAT || arg->type == VAL_INT)) {
              printf("Error: Field '%s' must be float\n", t->field_names[idx]);
              exit(1);
            }
            if (t->field_types[idx] == TYPE_STRING && arg->type != VAL_STRING) {
              printf("Error: Field '%s' must be str\n", t->field_names[idx]);
              exit(1);
            }
            if (t->field_types[idx] == TYPE_BOOL && arg->type != VAL_BOOL) {
              printf("Error: Field '%s' must be bool\n", t->field_names[idx]);
              exit(1);
            }
            if (t->field_types[idx] == TYPE_CUSTOM) {
              if (arg->type != VAL_OBJECT) {
                printf("Error: Field '%s' must be type '%s'\n",
                       t->field_names[idx], t->field_custom_types[idx]);
                exit(1);
              }
              Value *mark = hash_table_get(arg->data.object_val, "__type");
              if (!mark || mark->type != VAL_STRING ||
                  strcmp(mark->data.string_val, t->field_custom_types[idx]) !=
                      0) {
                printf("Error: Field '%s' expects type '%s'\n",
                       t->field_names[idx], t->field_custom_types[idx]);
                exit(1);
              }
            }
            hash_table_set(obj->data.object_val, t->field_names[idx], arg);
            filled[idx] = 1;
            used_fields++;
          }
          // Fill missing fields with defaults
          for (int k = 0; k < t->field_count; k++) {
            if (!filled[k]) {
              if (t->field_defaults && t->field_defaults[k]) {
                Value *dv =
                    interpreter_eval_expression(interp, t->field_defaults[k]);
                if (t->field_types[k] == TYPE_INT && dv->type != VAL_INT) {
                  printf("Error: Default '%s' must be int\n",
                         t->field_names[k]);
                  exit(1);
                }
                if (t->field_types[k] == TYPE_FLOAT &&
                    !(dv->type == VAL_FLOAT || dv->type == VAL_INT)) {
                  printf("Error: Default '%s' must be float\n",
                         t->field_names[k]);
                  exit(1);
                }
                if (t->field_types[k] == TYPE_STRING &&
                    dv->type != VAL_STRING) {
                  printf("Error: Default '%s' must be str\n",
                         t->field_names[k]);
                  exit(1);
                }
                if (t->field_types[k] == TYPE_BOOL && dv->type != VAL_BOOL) {
                  printf("Error: Default '%s' must be bool\n",
                         t->field_names[k]);
                  exit(1);
                }
                if (t->field_types[k] == TYPE_CUSTOM) {
                  if (dv->type != VAL_OBJECT) {
                    printf("Error: Default '%s' must be type '%s'\n",
                           t->field_names[k], t->field_custom_types[k]);
                    exit(1);
                  }
                  Value *mark = hash_table_get(dv->data.object_val, "__type");
                  if (!mark || mark->type != VAL_STRING ||
                      strcmp(mark->data.string_val, t->field_custom_types[k]) !=
                          0) {
                    printf("Error: Default '%s' expects type '%s'\n",
                           t->field_names[k], t->field_custom_types[k]);
                    exit(1);
                  }
                }
                hash_table_set(obj->data.object_val, t->field_names[k], dv);
                filled[k] = 1;
                used_fields++;
              } else {
                printf("Error: Missing field '%s' for type '%s'\n",
                       t->field_names[k], node->name);
                exit(1);
              }
            }
          }
        } else {
          if (node->argument_count != t->field_count) {
            printf("Error: Expected %d arguments for type '%s', got %d\n",
                   t->field_count, node->name, node->argument_count);
            exit(1);
          }
          for (int i = 0; i < t->field_count; i++) {
            Value *arg =
                interpreter_eval_expression(interp, node->arguments[i]);
            if (t->field_types[i] == TYPE_INT && arg->type != VAL_INT) {
              printf("Error: Field '%s' must be int\n", t->field_names[i]);
              exit(1);
            }
            if (t->field_types[i] == TYPE_FLOAT &&
                !(arg->type == VAL_FLOAT || arg->type == VAL_INT)) {
              printf("Error: Field '%s' must be float\n", t->field_names[i]);
              exit(1);
            }
            if (t->field_types[i] == TYPE_STRING && arg->type != VAL_STRING) {
              printf("Error: Field '%s' must be str\n", t->field_names[i]);
              exit(1);
            }
            if (t->field_types[i] == TYPE_BOOL && arg->type != VAL_BOOL) {
              printf("Error: Field '%s' must be bool\n", t->field_names[i]);
              exit(1);
            }
            if (t->field_types[i] == TYPE_CUSTOM) {
              if (arg->type != VAL_OBJECT) {
                printf("Error: Field '%s' must be type '%s'\n",
                       t->field_names[i], t->field_custom_types[i]);
                exit(1);
              }
              Value *mark = hash_table_get(arg->data.object_val, "__type");
              if (!mark || mark->type != VAL_STRING ||
                  strcmp(mark->data.string_val, t->field_custom_types[i]) !=
                      0) {
                printf("Error: Field '%s' expects type '%s'\n",
                       t->field_names[i], t->field_custom_types[i]);
                exit(1);
              }
            }
            hash_table_set(obj->data.object_val, t->field_names[i], arg);
          }
        }
        free(filled);
        return obj;
      }
      printf("Error: Undefined function '%s'\n", node->name);
      exit(1);
    }

    // Parametreleri önce değerlendir (mevcut scope'ta)
    Value **arg_values =
        (Value **)malloc(sizeof(Value *) * node->argument_count);
    for (int i = 0; i < node->argument_count; i++) {
      arg_values[i] = interpreter_eval_expression(interp, node->arguments[i]);
    }

    // Yeni scope oluştur
    SymbolTable *old_scope = interp->current_scope;
    interp->current_scope = symbol_table_create(interp->global_scope);

    // Parametreleri yeni scope'a ekle
    for (int i = 0; i < node->argument_count; i++) {
      symbol_table_set(interp->current_scope, func->node->parameters[i]->name,
                       arg_values[i]);
      value_free(arg_values[i]);
    }
    free(arg_values);

    // Fonksiyon gövdesini çalıştır
    interp->should_return = 0;
    interpreter_execute_statement(interp, func->node->body);

    // Return değerini al
    Value *result = interp->return_value ? value_copy(interp->return_value)
                                         : value_create_void();

    // Scope'u geri al
    symbol_table_free(interp->current_scope);
    interp->current_scope = old_scope;

    if (interp->return_value) {
      value_free(interp->return_value);
      interp->return_value = NULL;
    }
    interp->should_return = 0;

    return result;
  }

  default:
    return value_create_void();
  }
}

// ============================================================================
// STATEMENT ÇALIŞTIRMA (Statement Execution)
// ============================================================================

void interpreter_execute_statement(Interpreter *interp, ASTNode *node) {
  if (!node || interp->should_return)
    return;

  switch (node->type) {
  case AST_TYPE_DECL: {
    TypeDef *t = (TypeDef *)malloc(sizeof(TypeDef));
    t->name = strdup(node->name);
    t->field_count = node->field_count;
    t->field_names = (char **)malloc(sizeof(char *) * t->field_count);
    t->field_types = (DataType *)malloc(sizeof(DataType) * t->field_count);
    t->field_defaults = (ASTNode **)malloc(sizeof(ASTNode *) * t->field_count);
    t->field_custom_types = (char **)malloc(sizeof(char *) * t->field_count);
    for (int i = 0; i < t->field_count; i++) {
      t->field_names[i] = strdup(node->field_names[i]);
      t->field_types[i] = node->field_types[i];
      t->field_defaults[i] =
          node->field_defaults ? node->field_defaults[i] : NULL;
      t->field_custom_types[i] =
          node->field_custom_types ? (node->field_custom_types[i]
                                          ? strdup(node->field_custom_types[i])
                                          : NULL)
                                   : NULL;
    }
    interpreter_register_type(interp, t);
    break;
  }
  case AST_PROGRAM:
    for (int i = 0; i < node->statement_count && !interp->should_return; i++) {
      interpreter_execute_statement(interp, node->statements[i]);
    }
    break;

  case AST_BLOCK: {
    // Block creates a new scope
    SymbolTable *old_scope = interp->current_scope;
    interp->current_scope = symbol_table_create(old_scope);

    for (int i = 0; i < node->statement_count && !interp->should_return; i++) {
      interpreter_execute_statement(interp, node->statements[i]);
    }

    // Restore old scope
    symbol_table_free(interp->current_scope);
    interp->current_scope = old_scope;
    break;
  }

  case AST_VARIABLE_DECL: {
    Value *val = NULL;
    if (node->right) {
      val = interpreter_eval_expression(interp, node->right);

      // Eğer tipli array tanımı ise ve değer de array ise, tip kontrolü yap
      if (val->type == VAL_ARRAY) {
        ValueType required_type = VAL_VOID;

        switch (node->data_type) {
        case TYPE_ARRAY_INT:
          required_type = VAL_INT;
          break;
        case TYPE_ARRAY_FLOAT:
          required_type = VAL_FLOAT;
          break;
        case TYPE_ARRAY_STR:
          required_type = VAL_STRING;
          break;
        case TYPE_ARRAY_BOOL:
          required_type = VAL_BOOL;
          break;
        default:
          break;
        }

        // Array'in tipini güncelle
        if (required_type != VAL_VOID) {
          val->data.array_val->elem_type = required_type;

          // Mevcut elemanları kontrol et
          for (int i = 0; i < val->data.array_val->length; i++) {
            if (val->data.array_val->elements[i]->type != required_type) {
              printf("Error: All elements in array literal must be of type ");
              switch (required_type) {
              case VAL_INT:
                printf("int");
                break;
              case VAL_FLOAT:
                printf("float");
                break;
              case VAL_STRING:
                printf("str");
                break;
              case VAL_BOOL:
                printf("bool");
                break;
              default:
                break;
              }
              printf("!\n");
              value_free(val);
              exit(1);
            }
          }
        }
      }
    } else {
      // Varsayılan değer
      switch (node->data_type) {
      case TYPE_INT:
        val = value_create_int(0);
        break;
      case TYPE_FLOAT:
        val = value_create_float(0.0f);
        break;
      case TYPE_STRING:
        val = value_create_string("");
        break;
      case TYPE_BOOL:
        val = value_create_bool(0);
        break;
      case TYPE_ARRAY:
        val = value_create_array(4); // Mixed type array
        break;
      case TYPE_ARRAY_INT:
        val = value_create_typed_array(4, VAL_INT);
        break;
      case TYPE_ARRAY_FLOAT:
        val = value_create_typed_array(4, VAL_FLOAT);
        break;
      case TYPE_ARRAY_STR:
        val = value_create_typed_array(4, VAL_STRING);
        break;
      case TYPE_ARRAY_BOOL:
        val = value_create_typed_array(4, VAL_BOOL);
        break;
      case TYPE_ARRAY_JSON:
        val = value_create_array(4); // JSON-like mixed array
        break;
      default:
        val = value_create_void();
        break;
      }
    }

    symbol_table_set(interp->current_scope, node->name, val);
    value_free(val);
    break;
  }

  case AST_ASSIGNMENT: {
    Value *val = interpreter_eval_expression(interp, node->right);

    // Eğer sol taraf array/object erişimi zinciri ise
    if (node->left && node->left->type == AST_ARRAY_ACCESS) {
      // Zinciri çöz: base name ve segment listesi
      ASTNode *seg = node->left;
      // En sola kadar git
      while (seg->left)
        seg = seg->left;
      if (!seg->name) {
        printf("Error: Invalid assignment left-hand side\n");
        value_free(val);
        exit(1);
      }
      // Base container
      Value *container = symbol_table_get(interp->current_scope, seg->name);
      if (!container) {
        printf("Error: '%s' is not defined\n", seg->name);
        value_free(val);
        exit(1);
      }
      // Zinciri baştan tekrar yürü ve parent+key bul
      // seg şu anda ilk düğüm; parent için son düğümden bir önceki noktayı
      // bulmalıyız İlk düğüm tekrar
      ASTNode *walker = node->left;
      Value *current = container;
      Value *parent = NULL;
      while (walker->left) {
        // İleriye gitmek için önce sol'u işle
        walker = walker->left;
      }
      // Şimdi en sol seg'e tekrar başlayarak node->left'e kadar ilerleyelim
      // Yeniden başlat
      // Zinciri iteratif gezmek için liste üretelim
      int depth = 0;
      ASTNode *tmp = node->left;
      while (tmp) {
        depth++;
        tmp = tmp->left;
      }
      ASTNode **nodes = (ASTNode **)malloc(sizeof(ASTNode *) * depth);
      tmp = node->left;
      for (int i = depth - 1; i >= 0; i--) {
        nodes[i] = tmp;
        tmp = tmp->left;
      }
      // nodes[0] en sol (base), nodes[depth-1] en sağ (target)
      for (int i = 0; i < depth - 1; i++) {
        ASTNode *n = nodes[i];
        // index'i değerlendir
        Value *idx = interpreter_eval_expression(interp, n->index);
        parent = current;
        // İleri container'a ilerle
        if (parent->type == VAL_OBJECT) {
          if (idx->type != VAL_STRING) {
            printf("Error: Object key must be string\n");
            value_free(idx);
            value_free(val);
            exit(1);
          }
          Value *child =
              hash_table_get(parent->data.object_val, idx->data.string_val);
          if (!child) {
            // Auto-create eksik object
            Value *created = value_create_object();
            hash_table_set(parent->data.object_val, idx->data.string_val,
                           created);
            child =
                hash_table_get(parent->data.object_val, idx->data.string_val);
          }
          current = child;
        } else if (parent->type == VAL_ARRAY) {
          if (idx->type != VAL_INT) {
            printf("Error: Array index must be integer\n");
            value_free(idx);
            value_free(val);
            exit(1);
          }
          int index = idx->data.int_val;
          // Doğrudan pointer erişimi
          if (index < 0 || index >= parent->data.array_val->length) {
            printf("Error: Array index out of bounds (line %d)\n", node->line);
            value_free(idx);
            value_free(val);
            exit(1);
          }
          current = parent->data.array_val->elements[index];
        } else {
          printf("Error: Intermediate segment must be array or object (line "
                 "%d)\n",
                 node->line);
          value_free(idx);
          value_free(val);
          exit(1);
        }
        value_free(idx);
      }
      // Şimdi parent=current_parent, target key son segmentin index'i
      Value *target_parent = current;
      ASTNode *last = nodes[depth - 1];
      Value *last_idx = interpreter_eval_expression(interp, last->index);
      if (target_parent->type == VAL_OBJECT) {
        if (last_idx->type != VAL_STRING) {
          printf("Error: Object key must be string (line %d)\n", node->line);
          value_free(last_idx);
          value_free(val);
          exit(1);
        }
        hash_table_set(target_parent->data.object_val,
                       last_idx->data.string_val, value_copy(val));
        value_free(last_idx);
      } else if (target_parent->type == VAL_ARRAY) {
        if (last_idx->type != VAL_INT) {
          printf("Error: Array index must be integer (line %d)\n", node->line);
          value_free(last_idx);
          value_free(val);
          exit(1);
        }
        array_set(target_parent->data.array_val, last_idx->data.int_val, val);
        value_free(last_idx);
      } else {
        printf("Error: Target container must be array or object (line %d)\n",
               node->line);
        value_free(last_idx);
        value_free(val);
        exit(1);
      }
      free(nodes);
    } else {
      // Normal assignment
      symbol_table_set(interp->current_scope, node->name, val);
    }

    value_free(val);
    break;
  }

  case AST_COMPOUND_ASSIGN: {
    // x += 5 gibi
    Value *current = symbol_table_get(interp->current_scope, node->name);
    if (!current) {
      printf("Error: Undefined variable '%s'\n", node->name);
      exit(1);
    }

    Value *right_val = interpreter_eval_expression(interp, node->right);
    Value *result = NULL;

    if (node->op == TOKEN_PLUS_EQUAL) {
      if (current->type == VAL_BIGINT || right_val->type == VAL_BIGINT) {
        const char *la = (current->type == VAL_BIGINT)
                             ? current->data.bigint_val
                             : bigint_from_ll_str(current->data.int_val);
        const char *rb = (right_val->type == VAL_BIGINT)
                             ? right_val->data.bigint_val
                             : bigint_from_ll_str(right_val->data.int_val);
        char *sum = bigint_add_str(la, rb);
        if (current->type != VAL_BIGINT)
          free((char *)la);
        if (right_val->type != VAL_BIGINT)
          free((char *)rb);
        result = value_create_bigint(sum);
        free(sum);
      } else if (current->type == VAL_INT && right_val->type == VAL_INT) {
        long long a = current->data.int_val;
        long long b = right_val->data.int_val;
        if ((b > 0 && a > (LLONG_MAX - b)) || (b < 0 && a < (LLONG_MIN - b))) {
          char *sa = bigint_from_ll_str(a);
          char *sb = bigint_from_ll_str(b);
          char *sum = bigint_add_str(sa, sb);
          free(sa);
          free(sb);
          result = value_create_bigint(sum);
          free(sum);
        } else {
          result = value_create_int(a + b);
        }
      } else {
        float l = (current->type == VAL_FLOAT) ? current->data.float_val
                                               : (float)current->data.int_val;
        float r = (right_val->type == VAL_FLOAT)
                      ? right_val->data.float_val
                      : (float)right_val->data.int_val;
        result = value_create_float(l + r);
      }
    } else if (node->op == TOKEN_MINUS_EQUAL) {
      if (current->type == VAL_INT && right_val->type == VAL_INT) {
        long long a = current->data.int_val;
        long long b = right_val->data.int_val;
        if ((-b > 0 && a > (LLONG_MAX + b)) ||
            (-b < 0 && a < (LLONG_MIN + b))) {
          float l = (float)a;
          float r = (float)b;
          result = value_create_float(l - r);
        } else {
          result = value_create_int(a - b);
        }
      } else {
        float l = (current->type == VAL_FLOAT) ? current->data.float_val
                                               : (float)current->data.int_val;
        float r = (right_val->type == VAL_FLOAT)
                      ? right_val->data.float_val
                      : (float)right_val->data.int_val;
        result = value_create_float(l - r);
      }
    } else if (node->op == TOKEN_MULTIPLY_EQUAL) {
      if (current->type == VAL_BIGINT || right_val->type == VAL_BIGINT) {
        if (current->type == VAL_BIGINT && right_val->type == VAL_BIGINT) {
          char *prod = bigint_mul_str(current->data.bigint_val,
                                      right_val->data.bigint_val);
          result = value_create_bigint(prod);
          free(prod);
        } else {
          const char *big = (current->type == VAL_BIGINT)
                                ? current->data.bigint_val
                                : right_val->data.bigint_val;
          long long small = (current->type == VAL_INT)
                                ? current->data.int_val
                                : right_val->data.int_val;
          char *prod = bigint_mul_small(big, small);
          result = value_create_bigint(prod);
          free(prod);
        }
      } else if (current->type == VAL_INT && right_val->type == VAL_INT) {
        long long a = current->data.int_val;
        long long b = right_val->data.int_val;
        if (a != 0 && ((b > 0 && (a > LLONG_MAX / b || a < LLONG_MIN / b)) ||
                       (b < 0 && (a == LLONG_MIN || -a > LLONG_MAX / -b)))) {
          char *sa = bigint_from_ll_str(a);
          char *sb = bigint_from_ll_str(b);
          char *prod = bigint_mul_str(sa, sb);
          free(sa);
          free(sb);
          result = value_create_bigint(prod);
          free(prod);
        } else {
          result = value_create_int(a * b);
        }
      } else {
        float l = (current->type == VAL_FLOAT) ? current->data.float_val
                                               : (float)current->data.int_val;
        float r = (right_val->type == VAL_FLOAT)
                      ? right_val->data.float_val
                      : (float)right_val->data.int_val;
        result = value_create_float(l * r);
      }
    } else if (node->op == TOKEN_DIVIDE_EQUAL) {
      if (current->type == VAL_INT && right_val->type == VAL_INT) {
        result =
            value_create_int(current->data.int_val / right_val->data.int_val);
      } else {
        float l = (current->type == VAL_FLOAT) ? current->data.float_val
                                               : current->data.int_val;
        float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val
                                                 : right_val->data.int_val;
        result = value_create_float(l / r);
      }
    }

    if (result) {
      symbol_table_set(interp->current_scope, node->name, result);
      value_free(result);
    }
    value_free(right_val);
    break;
  }

  case AST_INCREMENT: {
    // x++
    Value *current = symbol_table_get(interp->current_scope, node->name);
    if (!current) {
      printf("Error: Undefined variable '%s'\n", node->name);
      exit(1);
    }

    Value *result = NULL;
    if (current->type == VAL_INT) {
      result = value_create_int(current->data.int_val + 1);
    } else if (current->type == VAL_FLOAT) {
      result = value_create_float(current->data.float_val + 1.0f);
    }

    if (result) {
      symbol_table_set(interp->current_scope, node->name, result);
      value_free(result);
    }
    break;
  }

  case AST_DECREMENT: {
    // x--
    Value *current = symbol_table_get(interp->current_scope, node->name);
    if (!current) {
      printf("Error: Undefined variable '%s'\n", node->name);
      exit(1);
    }

    Value *result = NULL;
    if (current->type == VAL_INT) {
      result = value_create_int(current->data.int_val - 1);
    } else if (current->type == VAL_FLOAT) {
      result = value_create_float(current->data.float_val - 1.0f);
    }

    if (result) {
      symbol_table_set(interp->current_scope, node->name, result);
      value_free(result);
    }
    break;
  }

  case AST_FUNCTION_DECL:
    interpreter_register_function(interp, node->name, node);
    break;

  case AST_RETURN:
    if (interp->return_value) {
      value_free(interp->return_value);
    }
    interp->return_value =
        interpreter_eval_expression(interp, node->return_value);
    interp->should_return = 1;
    break;

  case AST_BREAK:
    interp->should_break = 1;
    break;

  case AST_CONTINUE:
    interp->should_continue = 1;
    break;

  case AST_IMPORT: {
    // Import statement: import "file.tpr";
    const char *filename = node->value.string_value;

    // Read file
    FILE *file = fopen(filename, "rb");
    if (!file) {
      printf("Error: Could not open import file '%s' (line %d)\n", filename,
             node->line);
      exit(1);
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read content
    char *source = (char *)malloc((size_t)size + 1);
    fread(source, 1, size, file);
    source[size] = '\0';
    fclose(file);

    // Parse and execute imported file
    Lexer *lexer = lexer_create(source);
    int token_capacity = 100;
    int token_count = 0;
    Token **tokens = (Token **)malloc(sizeof(Token *) * token_capacity);

    Token *token;
    while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
      if (token_count >= token_capacity) {
        token_capacity *= 2;
        tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
      }
      tokens[token_count++] = token;
    }
    tokens[token_count++] = token;

    lexer_free(lexer);

    Parser *parser = parser_create(tokens, token_count);
    ASTNode *imported_ast = parser_parse(parser);

    // Execute imported code in current interpreter context
    interpreter_execute(interp, imported_ast);

    if (interp->retained_count >= interp->retained_capacity) {
      int new_capacity =
          interp->retained_capacity > 0 ? interp->retained_capacity * 2 : 4;
      interp->retained_modules = (ASTNode **)realloc(
          interp->retained_modules, sizeof(ASTNode *) * new_capacity);
      interp->retained_capacity = new_capacity;
    }
    interp->retained_modules[interp->retained_count++] = imported_ast;

    // Cleanup
    parser_free(parser);
    for (int i = 0; i < token_count; i++) {
      token_free(tokens[i]);
    }
    free(tokens);
    free(source);
    break;
  }

  case AST_IF: {
    Value *cond = interpreter_eval_expression(interp, node->condition);

    if (value_is_truthy(cond)) {
      interpreter_execute_statement(interp, node->then_branch);
    } else if (node->else_branch) {
      interpreter_execute_statement(interp, node->else_branch);
    }

    value_free(cond);
    break;
  }

  case AST_WHILE: {
    while (1) {
      Value *cond = interpreter_eval_expression(interp, node->condition);
      int should_continue = value_is_truthy(cond);
      value_free(cond);

      if (!should_continue || interp->should_return || interp->should_break) {
        break;
      }

      interpreter_execute_statement(interp, node->body);

      if (interp->should_continue) {
        interp->should_continue = 0;
        continue;
      }

      if (interp->should_break) {
        break;
      }
    }
    interp->should_break = 0;
    break;
  }

  case AST_FOR: {
    // For döngüsü için yeni scope oluştur (loop variable için)
    SymbolTable *old_scope = interp->current_scope;
    interp->current_scope = symbol_table_create(old_scope);

    // Init statement'ı çalıştır (int i = 0)
    if (node->init) {
      interpreter_execute_statement(interp, node->init);
    }

    // For döngüsü
    while (1) {
      // Koşulu kontrol et
      if (node->condition) {
        Value *cond = interpreter_eval_expression(interp, node->condition);
        int should_continue = value_is_truthy(cond);
        value_free(cond);

        if (!should_continue || interp->should_return || interp->should_break) {
          break;
        }
      }

      // Döngü gövdesini çalıştır
      interpreter_execute_statement(interp, node->body);

      if (interp->should_return || interp->should_break) {
        break;
      }

      if (interp->should_continue) {
        interp->should_continue = 0;
        // Continue: increment'i çalıştır ve devam et
        if (node->increment) {
          interpreter_execute_statement(interp, node->increment);
        }
        continue;
      }

      // Increment statement'ı çalıştır (i = i + 1)
      if (node->increment) {
        interpreter_execute_statement(interp, node->increment);
      }
    }

    // Restore old scope
    symbol_table_free(interp->current_scope);
    interp->current_scope = old_scope;
    interp->should_break = 0;
    break;
  }

  case AST_FOR_IN: {
    // Iterable'ı değerlendir (range(10) gibi)
    Value *iterable_val = interpreter_eval_expression(interp, node->iterable);

    // Eğer range() fonksiyon çağrısıysa, int döner
    if (iterable_val->type == VAL_INT) {
      int count = iterable_val->data.int_val;

      // 0'dan count'a kadar döngü
      for (int i = 0; i < count; i++) {
        if (interp->should_return || interp->should_break)
          break;

        // Iterator değişkenini güncelle
        Value *iter_val = value_create_int(i);
        symbol_table_set(interp->current_scope, node->name, iter_val);
        value_free(iter_val);

        // Döngü gövdesini çalıştır
        interpreter_execute_statement(interp, node->body);

        if (interp->should_continue) {
          interp->should_continue = 0;
          continue;
        }

        if (interp->should_break) {
          break;
        }
      }
    }

    interp->should_break = 0;
    value_free(iterable_val);
    break;
  }

  case AST_FUNCTION_CALL: {
    Value *result = interpreter_eval_expression(interp, node);
    value_free(result);
    break;
  }

  case AST_TRY_CATCH: {
    // Try-Catch exception handling using setjmp/longjmp
    ExceptionHandler handler;
    handler.exception = NULL;
    handler.active = 1;
    handler.parent = interp->exception_handler;
    interp->exception_handler = &handler;

    // setjmp returns 0 on first call, non-zero when longjmp is called
    int jmp_result = setjmp(handler.jump_buffer);

    if (jmp_result == 0) {
      // Normal execution - try block
      interpreter_execute_statement(interp, node->try_block);

      // If no exception, deactivate handler
      handler.active = 0;
      interp->exception_handler = handler.parent;

      // Execute finally block if present
      if (node->finally_block) {
        interpreter_execute_statement(interp, node->finally_block);
      }
    } else {
      // Exception was thrown - catch block
      handler.active = 0;
      interp->exception_handler = handler.parent;

      // Get the exception value
      Value *exception_val = interp->current_exception;
      interp->has_exception = 0;
      interp->current_exception = NULL;

      // Create new scope for catch block with exception variable
      SymbolTable *old_scope = interp->current_scope;
      interp->current_scope = symbol_table_create(old_scope);

      // Set exception variable in catch scope
      if (node->catch_var && exception_val) {
        symbol_table_set(interp->current_scope, node->catch_var, exception_val);
      }

      // Execute catch block
      interpreter_execute_statement(interp, node->catch_block);

      // Restore scope
      symbol_table_free(interp->current_scope);
      interp->current_scope = old_scope;

      // Free exception value
      if (exception_val) {
        value_free(exception_val);
      }

      // Execute finally block if present
      if (node->finally_block) {
        interpreter_execute_statement(interp, node->finally_block);
      }
    }
    break;
  }

  case AST_THROW: {
    // Evaluate the throw expression
    Value *throw_val = interpreter_eval_expression(interp, node->throw_expr);

    // Check if there's an active exception handler
    if (interp->exception_handler && interp->exception_handler->active) {
      // Store exception and jump to handler
      interp->has_exception = 1;
      interp->current_exception = throw_val;
      longjmp(interp->exception_handler->jump_buffer, 1);
    } else {
      // No handler - unhandled exception, print error and exit
      printf("Unhandled exception at line %d: ", node->line);
      value_print(throw_val);
      printf("\n");
      value_free(throw_val);
      exit(1);
    }
    break;
  }

  default:
    break;
  }
}

// ============================================================================
// ANA İNTERPRETER FONKSİYONU
// ============================================================================

void interpreter_execute(Interpreter *interp, ASTNode *node) {
  interpreter_execute_statement(interp, node);
}

Value *interpreter_eval(Interpreter *interp, ASTNode *node) {
  return interpreter_eval_expression(interp, node);
}

// ============================================
// Thread Helper Functions
// ============================================

// Helper: Interpreter clone (Thread için)
// Sadece fonksiyonları ve o anki global değişkenlerin snapshot'ını alır
static Interpreter *interpreter_clone(Interpreter *src) {
  Interpreter *dest = interpreter_create();

  // Fonksiyonları kopyala (kod değişmez, referans)
  for (int i = 0; i < src->function_count; i++) {
    Function *f = src->functions[i];
    interpreter_register_function(dest, f->name, f->node);
  }

  // Tipleri kopyala
  for (int i = 0; i < src->type_count; i++) {
    TypeDef *t = src->types[i];
    interpreter_register_type(dest, t);
  }

  // Global değişkenleri kopyala (Deep Copy gerekli!)
  SymbolTable *src_global = src->global_scope;
  for (int i = 0; i < src_global->var_count; i++) {
    Variable *var = src_global->variables[i];
    if (var && var->name && var->value) {
      Value *val_copy = value_copy(var->value);
      symbol_table_set(dest->global_scope, var->name, val_copy);
    }
  }

  return dest;
}

// Thread Entry Point (Windows)
#ifdef _WIN32
DWORD WINAPI thread_entry_point(LPVOID lpParam) {
  ThreadArgs *args = (ThreadArgs *)lpParam;

  // Yeni interpreter oluştur ve init et
  Interpreter *thread_interp = interpreter_clone(args->parent_interp);

  // Fonksiyonu bul
  Function *func = interpreter_get_function(thread_interp, args->func_name);
  if (func) {
    ASTNode *func_node = func->node;
    if (func_node && func_node->body) {
      // Argüman varsa ilk parametreye ata
      // args->arg zaten kopyalanmış bir değer olmalı (main thread'den bağımsız)
      if (func_node->param_count > 0 && args->arg) {
        symbol_table_set(thread_interp->global_scope,
                         func_node->parameters[0]->name, args->arg);
      }

      // Fonksiyon gövdesini çalıştır
      // interpreter_execute exception yakalamayı da içerir
      interpreter_execute(thread_interp, func_node->body);
    }
  }

  // Temizlik
  interpreter_free(thread_interp);
  // args->arg thread'e aitti (clone sırasında veya creation sırasında
  // kopyalandı) Ancak symbol_table_set (yukarıda) kopyalamadı, direkt pointer'ı
  // aldı. interpreter_free sembol tablosunu temizleyince argüman da silinir. Bu
  // yüzden burada free etmeye gerek yok (eğer symbol table sahipliği aldıysa).
  // symbol_table_set value ownership almaz, ama SymbolTable destroy ederken
  // value_free çağırır mı? Evet, symbol_table_free -> value_free çağırır. Bu
  // yüzden burada args->arg'ı free etmemeliyiz.

  free(args->func_name);
  free(args);

  return 0;
}
#else
// POSIX Thread entry point
void *thread_entry_point(void *arg) {
  // POSIX Implementation (TODO)
  return NULL;
}
#endif

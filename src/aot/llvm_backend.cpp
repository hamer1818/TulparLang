// Tulpar LLVM Backend - Implementation
// Phase 3 Fix: Printf types

#include "llvm_backend.hpp"
#include "../embedded_libs.h" // Embedded libraries
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../parser/import_alias.hpp"
#include "../common/localization.hpp"
#include "../common/diagnostics.hpp"
#include "llvm_types.hpp"
#include "llvm_values.hpp"
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Globals that the runtime needs a per-thread view of. Wings' `_request`
// is set + read once per HTTP request; with shared storage, parallel
// worker threads race on it and the lib-level workaround was a mutex
// covering the whole "set _request → dispatch handler" critical section.
// Marking it TLS lets each thread mutate its own copy and the mutex
// goes away. Keep this list narrow — TLS reads are slightly more
// expensive than plain globals, so only flag identifiers that actually
// need per-thread storage.
static bool global_needs_tls(const char *name) {
  if (!name)
    return false;
  return strcmp(name, "_request") == 0;
}

// Counter globals that wings increments from parallel worker threads.
// The pattern `name = name + delta` against any of these is compiled
// to a single `atomicrmw add` instead of the usual load + add + store
// triple — the unlocked triple races on read-modify-write under load
// and drops increments. Keep this list narrow; atomic RMW costs a
// memory fence on most ISAs and isn't worth paying for non-shared
// counters.
static bool global_needs_atomic_rmw(const char *name) {
  if (!name)
    return false;
  return strcmp(name, "_wings_requests_total") == 0 ||
         strcmp(name, "_wings_requests_2xx") == 0 ||
         strcmp(name, "_wings_requests_4xx") == 0 ||
         strcmp(name, "_wings_requests_5xx") == 0;
}

char *my_strdup(const char *s) {
  if (!s)
    return nullptr;
  size_t len = strlen(s);
  char *copy = static_cast<char*>(malloc(len + 1));
  if (copy)
    strcpy(copy, s);
  return copy;
}

// Helper to build alloca in entry block
LLVMValueRef llvm_build_alloca_at_entry(LLVMBackend *backend, LLVMTypeRef type,
                                        const char *name) {
  LLVMValueRef func = backend->current_function;
  LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(func);
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(backend->context);
  LLVMValueRef first = LLVMGetFirstInstruction(entry);
  if (first)
    LLVMPositionBuilderBefore(builder, first);
  else
    LLVMPositionBuilderAtEnd(builder, entry);
  LLVMValueRef alloca = LLVMBuildAlloca(builder, type, name);
  LLVMDisposeBuilder(builder);
  return alloca;
}

// Forward declarations
void codegen_func_def(LLVMBackend *backend, ASTNode_C *node);
static void predeclare_func_signature(LLVMBackend *backend, ASTNode_C *node);
static void report_codegen_error(LLVMBackend *backend, int line,
                                 const char *kind, const char *message,
                                 const char *caret_token, const char *hint);

// Lowers `for (name in iterable) body` to a desugared C-style for over an
// index, in place. The original AST_FOR_IN node is mutated into an AST_BLOCK
// whose statements are equivalent to:
//
//   { json __forin_it_<line> = <iterable>;
//     for (int __forin_idx_<line> = 0;
//          __forin_idx_<line> < length(__forin_it_<line>);
//          __forin_idx_<line> = __forin_idx_<line> + 1) {
//       <name> = __forin_it_<line>[__forin_idx_<line>];
//       <body>
//     } }
//
// The original `iterable` and `body` subtrees are moved into the desugared
// form (no deep copy), so the existing ast_node_free recursion frees them
// exactly once on shutdown. Necessary because AST_FOR_IN had no codegen case
// at all in this backend — `for (x in arr) { ... }` silently produced an
// exe that did nothing.
static void lower_for_in_in_place(ASTNode_C *node) {
  ASTNode_C *iterable = node->iterable;
  ASTNode_C *body = node->body;
  char *loop_var = node->name;
  int line = node->line;

  char it_name[48], idx_name[48];
  std::snprintf(it_name, sizeof(it_name), "__forin_it_%d_%p", line,
                static_cast<void *>(node));
  std::snprintf(idx_name, sizeof(idx_name), "__forin_idx_%d_%p", line,
                static_cast<void *>(node));

  auto mk_id = [&](const char *n) {
    ASTNode_C *id = ast_node_create(AST_IDENTIFIER);
    id->name = strdup(n);
    id->line = line;
    return id;
  };
  auto mk_int = [&](long long v) {
    ASTNode_C *lit = ast_node_create(AST_INT_LITERAL);
    lit->value.int_value = v;
    lit->line = line;
    return lit;
  };

  // var <name>; — declare the loop variable up front so the per-iteration
  // assignment below resolves it as a local. Without this, AOT's strict
  // "assign to undefined" check fires before we ever reach the loop body.
  ASTNode_C *loop_var_decl = ast_node_create(AST_VARIABLE_DECL);
  loop_var_decl->name = strdup(loop_var);
  loop_var_decl->data_type = TYPE_VOID;
  loop_var_decl->line = line;

  // json __forin_it_<line> = <iterable>;
  ASTNode_C *it_decl = ast_node_create(AST_VARIABLE_DECL);
  it_decl->name = strdup(it_name);
  it_decl->data_type = TYPE_ARRAY_JSON;
  it_decl->right = iterable;
  it_decl->line = line;

  // int __forin_idx_<line> = 0;
  ASTNode_C *idx_decl = ast_node_create(AST_VARIABLE_DECL);
  idx_decl->name = strdup(idx_name);
  idx_decl->data_type = TYPE_INT;
  idx_decl->right = mk_int(0);
  idx_decl->line = line;

  // length(__forin_it_<line>)
  ASTNode_C *len_call = ast_node_create(AST_FUNCTION_CALL);
  len_call->name = strdup("length");
  len_call->argument_count = 1;
  len_call->arguments =
      static_cast<ASTNode_C **>(std::calloc(1, sizeof(ASTNode_C *)));
  len_call->arguments[0] = mk_id(it_name);
  len_call->line = line;

  // __forin_idx_<line> < length(...)
  ASTNode_C *cond = ast_node_create(AST_BINARY_OP);
  cond->op = TOKEN_LESS;
  cond->left = mk_id(idx_name);
  cond->right = len_call;
  cond->line = line;

  // __forin_idx_<line> = __forin_idx_<line> + 1
  ASTNode_C *plus = ast_node_create(AST_BINARY_OP);
  plus->op = TOKEN_PLUS;
  plus->left = mk_id(idx_name);
  plus->right = mk_int(1);
  plus->line = line;
  ASTNode_C *idx_inc = ast_node_create(AST_ASSIGNMENT);
  idx_inc->name = strdup(idx_name);
  idx_inc->right = plus;
  idx_inc->line = line;

  // <name> = __forin_it_<line>[__forin_idx_<line>]
  ASTNode_C *access = ast_node_create(AST_ARRAY_ACCESS);
  access->left = mk_id(it_name);
  access->index = mk_id(idx_name);
  access->line = line;
  ASTNode_C *name_assign = ast_node_create(AST_ASSIGNMENT);
  name_assign->name = strdup(loop_var);
  name_assign->right = access;
  name_assign->line = line;

  // Wrap body so the assignment runs first each iteration.
  ASTNode_C *new_body = ast_node_create(AST_BLOCK);
  new_body->statement_count = 2;
  new_body->statements =
      static_cast<ASTNode_C **>(std::calloc(2, sizeof(ASTNode_C *)));
  new_body->statements[0] = name_assign;
  new_body->statements[1] = body;
  new_body->line = line;

  ASTNode_C *for_node = ast_node_create(AST_FOR);
  for_node->init = idx_decl;
  for_node->condition = cond;
  for_node->increment = idx_inc;
  for_node->body = new_body;
  for_node->line = line;

  // Mutate the original node into AST_BLOCK { it_decl; for_node; }. We
  // null out the ownership pointers we transferred so the original node's
  // destructor doesn't double-free.
  std::free(node->name);
  node->type = AST_BLOCK;
  node->name = nullptr;
  node->iterable = nullptr;
  node->body = nullptr;
  node->statement_count = 3;
  node->statements =
      static_cast<ASTNode_C **>(std::calloc(3, sizeof(ASTNode_C *)));
  node->statements[0] = loop_var_decl;
  node->statements[1] = it_decl;
  node->statements[2] = for_node;
}

// ---------------------------------------------------------------------------
// "Did you mean..?" Levenshtein-based suggestion helper for diagnostics.
// ---------------------------------------------------------------------------
//
// Bounded edit distance between two strings; returns >cap as soon as we know
// the answer can't be ≤ cap. Cheap enough to call against every known
// function/variable on every diagnostic — typical projects have <1000 names.
static int levenshtein_bounded(const char *a, const char *b, int cap) {
  int la = (int)strlen(a);
  int lb = (int)strlen(b);
  if (abs(la - lb) > cap) return cap + 1;
  if (la > 64 || lb > 64) return cap + 1; // keep memory tiny

  int row[65];
  for (int j = 0; j <= lb; j++) row[j] = j;
  for (int i = 1; i <= la; i++) {
    int prev = row[0];
    row[0] = i;
    int row_min = i;
    for (int j = 1; j <= lb; j++) {
      int cur = row[j];
      int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      // case-insensitive bonus: 'A' vs 'a' costs 0 (catch typos like uppercase)
      if (cost == 1) {
        char ca = a[i - 1], cb = b[j - 1];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca == cb) cost = 0;
      }
      int v = prev + cost;
      if (row[j - 1] + 1 < v) v = row[j - 1] + 1;
      if (cur + 1 < v) v = cur + 1;
      prev = cur;
      row[j] = v;
      if (v < row_min) row_min = v;
    }
    if (row_min > cap) return cap + 1; // early exit
  }
  return row[lb];
}

// Search a list of candidate names for one whose edit distance to `target`
// is small. Returns the best match or null. We tolerate up to 2 edits for
// names ≥4 chars, and 1 edit for shorter names — short names like "i" / "x"
// are common and would otherwise produce noisy false positives.
static const char *find_closest_name(const char *target, const char **names,
                                     int n) {
  int tlen = (int)strlen(target);
  int max_dist = (tlen >= 4) ? 2 : 1;
  const char *best = nullptr;
  int best_dist = max_dist + 1;
  for (int i = 0; i < n; i++) {
    if (!names[i]) continue;
    int d = levenshtein_bounded(target, names[i], max_dist);
    if (d <= max_dist && d < best_dist) {
      best = names[i];
      best_dist = d;
      if (d == 0) break;
    }
  }
  return best;
}

// Collect every name visible at the current codegen point: every var in the
// scope chain, every registered user function, plus every LLVM module-level
// global (top-level non-int vars live there but aren't in the scope chain).
// Caller-allocated buffer; truncates silently on overflow.
static int collect_visible_names(LLVMBackend *backend, const char **out,
                                 int max_names) {
  int n = 0;
  Scope *s = backend->current_scope;
  while (s && n < max_names) {
    for (int i = 0; i < s->count && n < max_names; i++) {
      out[n++] = s->vars[i].name;
    }
    s = s->parent;
  }
  for (int i = 0; i < backend->function_count && n < max_names; i++) {
    out[n++] = backend->functions[i].name;
  }
  // Top-level non-int globals (e.g. `str user_name = ...;`) live as LLVM
  // module globals without being mirrored into the scope chain. Walk them
  // explicitly so suggestions cover every visible identifier.
  for (LLVMValueRef g = LLVMGetFirstGlobal(backend->module);
       g && n < max_names; g = LLVMGetNextGlobal(g)) {
    const char *gname = LLVMGetValueName(g);
    if (gname && *gname && *gname != '_') {
      out[n++] = gname;
    }
  }
  return n;
}

// Suggestion-aware error reporter. Wraps report_codegen_error and appends
// a second `= ipucu:` line when a near-match is found in scope.
static void report_codegen_error_with_suggestion(LLVMBackend *backend,
                                                 int line, const char *kind,
                                                 const char *message,
                                                 const char *caret_token,
                                                 const char *base_hint) {
  const char *suggestion = nullptr;
  if (caret_token && *caret_token) {
    const char *names[256];
    int n = collect_visible_names(backend, names, 256);
    suggestion = find_closest_name(caret_token, names, n);
    // Don't suggest the literal token itself (zero-distance hit on already-
    // resolved decl with later-shadowing scope is rare but possible).
    if (suggestion && strcmp(suggestion, caret_token) == 0) suggestion = nullptr;
  }
  if (suggestion) {
    char combined[512];
    if (base_hint && *base_hint) {
      snprintf(combined, sizeof(combined), "%s — bunu mu demek istediniz: '%s'?",
               base_hint, suggestion);
    } else {
      snprintf(combined, sizeof(combined), "bunu mu demek istediniz: '%s'?",
               suggestion);
    }
    report_codegen_error(backend, line, kind, message, caret_token, combined);
  } else {
    report_codegen_error(backend, line, kind, message, caret_token, base_hint);
  }
}

// Rust-style codegen error reporter. Prints a header like
//
//     hata: 'json_response' adında bir fonksiyon bulunamadı
//       --> lib/http_utils.tpr:245
//        |
//   245  |    return json_response(403, {"error": "Forbidden"});
//        |           ^^^^^^^^^^^^^
//        = ipucu: import edildiğinden ve adının doğru yazıldığından emin olun
//
// `caret_token` is the symbol the error is about (used to compute caret
// length); pass null/empty to skip the caret line. `hint` is an optional
// final line; pass null to skip. Both `kind` and `message` are required.
//
// On builds without source_text (older code paths), falls back to the
// classic single-line "HATA (Satır N): ..." form so nothing regresses.
static void report_codegen_error(LLVMBackend *backend, int line,
                                 const char *kind, const char *message,
                                 const char *caret_token, const char *hint) {
  backend->had_error = 1;

  // Compute caret column + length once; both stderr rendering and the
  // structured sink need them.
  int caret_col_1based = 1;
  int caret_len = 0;
  if (caret_token && *caret_token && backend->source_text) {
    const char *src = backend->source_text;
    int cur_line = 1;
    const char *line_start = src;
    while (*src && cur_line < line) {
      if (*src == '\n') {
        cur_line++;
        line_start = src + 1;
      }
      src++;
    }
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
    int line_len = (int)(line_end - line_start);
    int tok_len = (int)strlen(caret_token);
    for (int i = 0; i + tok_len <= line_len; i++) {
      if (memcmp(line_start + i, caret_token, tok_len) == 0) {
        caret_col_1based = i + 1;
        caret_len = tok_len;
        break;
      }
    }
  }

  // LSP / structured-collection mode: push and skip stderr.
  if (tulpar::diag_sink_active()) {
    tulpar::diag_sink_push(line, caret_col_1based, caret_len,
                           kind ? kind : "error", message, hint);
    return;
  }

  if (!backend->source_text) {
    // Legacy path: keep the existing one-line "HATA" format.
    fprintf(stderr, "%s (%s %d): %s\n",
            tulpar::i18n::tr_en("HATA", "ERROR"),
            tulpar::i18n::tr_en("Satır", "Line"),
            line, message);
    if (hint && *hint) {
      fprintf(stderr, "  %s: %s\n",
              tulpar::i18n::tr_en("İpucu", "Hint"), hint);
    }
    return;
  }

  // Callers pass literal "hata" / "uyarı" as the kind label. Map them to
  // the user's locale here so the prefix is English on non-Turkish systems.
  const char *kind_localized;
  if (!kind) {
    kind_localized = tulpar::i18n::tr_en("hata", "error");
  } else if (strcmp(kind, "hata") == 0) {
    kind_localized = tulpar::i18n::tr_en("hata", "error");
  } else if (strcmp(kind, "uyarı") == 0 || strcmp(kind, "uyari") == 0) {
    kind_localized = tulpar::i18n::tr_en("uyarı", "warning");
  } else {
    kind_localized = kind;
  }
  fprintf(stderr, "%s: %s\n", kind_localized, message);
  if (backend->source_filename && *backend->source_filename) {
    fprintf(stderr, "  --> %s:%d\n", backend->source_filename, line);
  } else {
    fprintf(stderr, "  --> (stdin):%d\n", line);
  }

  // Walk to the requested line. We re-scan from the start every call —
  // diagnostics are rare and source files are small, so the O(line*len)
  // cost is irrelevant.
  const char *src = backend->source_text;
  int cur_line = 1;
  const char *line_start = src;
  while (*src && cur_line < line) {
    if (*src == '\n') {
      cur_line++;
      line_start = src + 1;
    }
    src++;
  }
  const char *line_end = line_start;
  while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;

  fprintf(stderr, "    |\n");
  fprintf(stderr, "%3d | %.*s\n", line, (int)(line_end - line_start), line_start);

  if (caret_len > 0) {
    fprintf(stderr, "    | ");
    int caret_col_0based = caret_col_1based - 1;
    for (int i = 0; i < caret_col_0based; i++) fputc(' ', stderr);
    for (int i = 0; i < caret_len; i++) fputc('^', stderr);
    fputc('\n', stderr);
  }
  if (hint && *hint) {
    fprintf(stderr, "    = %s: %s\n",
            tulpar::i18n::tr_en("ipucu", "hint"), hint);
  }
}

// Declare external runtime functions
void declare_runtime_functions(LLVMBackend *backend) {
  // printf: i32 printf(i8*, ...)
  LLVMTypeRef printf_params[] = {backend->string_type};
  LLVMTypeRef printf_type =
      LLVMFunctionType(backend->int32_type, printf_params, 1, 1);
  backend->func_printf =
      LLVMAddFunction(backend->module, "printf", printf_type);

  // vm_alloc_string: ObjString* vm_alloc_string(VM*, i8*, i32)
  // We'll treat VM* as void* (i8*) for now since we don't need its fields here
  // yet
  LLVMTypeRef alloc_str_params[] = {
      backend->ptr_type,    // VM* vm
      backend->string_type, // char* chars
      backend->int32_type   // int length
  };
  LLVMTypeRef alloc_str_type = LLVMFunctionType(
      LLVMPointerType(backend->obj_string_type, 0), alloc_str_params, 3, 0);
  backend->func_vm_alloc_string =
      LLVMAddFunction(backend->module, "vm_alloc_string_aot", alloc_str_type);

  // print_value: void print_value(VMValue)
  // VMValue is passed by value (struct)
  // print_value: void print_value(VMValue*) - takes pointer for ABI
  // compatibility
  LLVMTypeRef print_val_params[] = {backend->ptr_type};
  LLVMTypeRef print_val_type =
      LLVMFunctionType(backend->void_type, print_val_params, 1, 0);
  backend->func_print_value =
      LLVMAddFunction(backend->module, "aot_print_value", print_val_type);

  // print_value_inline: void print_value_inline(VMValue*) - no newline
  backend->func_print_value_inline = backend->func_print_value;

  // print_newline: void print_newline()
  LLVMTypeRef print_nl_type = LLVMFunctionType(backend->void_type, nullptr, 0, 0);
  backend->func_print_newline =
      LLVMAddFunction(backend->module, "print_newline", print_nl_type);

  // vm_binary_op: void vm_binary_op(VM* vm, VMValue* a, VMValue* b, int op,
  // VMValue* result)
  LLVMTypeRef bin_op_params[] = {backend->ptr_type, backend->ptr_type,
                                 backend->ptr_type, backend->int32_type,
                                 backend->ptr_type};
  LLVMTypeRef bin_op_type =
      LLVMFunctionType(backend->void_type, bin_op_params, 5, 0);
  backend->vm_binary_op_type = bin_op_type; // Store type
  backend->func_vm_binary_op =
      LLVMAddFunction(backend->module, "vm_binary_op", bin_op_type);

  // Array Functions
  // vm_allocate_array(VM*) -> ObjArray*
  LLVMTypeRef alloc_arr_params[] = {backend->ptr_type};
  LLVMTypeRef alloc_arr_type = LLVMFunctionType(
      backend->ptr_type, alloc_arr_params, 1, 0); // ObjArray* is ptr
  backend->func_vm_allocate_array = LLVMAddFunction(
      backend->module, "vm_allocate_array_aot_wrapper", alloc_arr_type);

  // vm_array_push_ptr(VM*, ObjArray*, VMValue*)
  LLVMTypeRef push_params[] = {backend->ptr_type, backend->ptr_type,
                               backend->ptr_type};
  LLVMTypeRef push_type =
      LLVMFunctionType(backend->void_type, push_params, 3, 0);
  backend->func_vm_array_push = LLVMAddFunction(
      backend->module, "vm_array_push_aot_ptr_wrapper", push_type);

  // vm_array_get(ObjArray*, int) -> VMValue
  LLVMTypeRef get_params[] = {backend->ptr_type, backend->int32_type};
  LLVMTypeRef get_type =
      llvm_make_vmvalue_func_type(backend, get_params, 2, 0);
  backend->func_vm_array_get =
      LLVMAddFunction(backend->module, "vm_array_get", get_type);

  // vm_array_set(ObjArray*, int, VMValue)
  LLVMTypeRef set_params[] = {backend->ptr_type, backend->int32_type,
                              backend->vm_value_type};
  LLVMTypeRef set_type = LLVMFunctionType(backend->void_type, set_params, 3, 0);
  backend->func_vm_array_set =
      LLVMAddFunction(backend->module, "vm_array_set", set_type);

  // Object/Generic Functions

  // vm_allocate_object(VM*) -> ObjObject*
  LLVMTypeRef alloc_obj_params[] = {backend->ptr_type};
  LLVMTypeRef alloc_obj_type =
      LLVMFunctionType(backend->ptr_type, alloc_obj_params, 1, 0);
  backend->func_vm_allocate_object = LLVMAddFunction(
      backend->module, "vm_allocate_object_aot_wrapper", alloc_obj_type);

  // vm_object_set_ptr(VM*, ObjObject*, char*, VMValue*)
  LLVMTypeRef obj_set_params[] = {backend->ptr_type, backend->ptr_type,
                                  backend->ptr_type, backend->ptr_type};
  LLVMTypeRef obj_set_type =
      LLVMFunctionType(backend->void_type, obj_set_params, 4, 0);
  backend->func_vm_object_set = LLVMAddFunction(
      backend->module, "vm_object_set_aot_ptr_wrapper", obj_set_type);

  // vm_get_element_ptr(VMValue*, VMValue*) -> VMValue (pointer-based for ABI
  // compatibility)
  LLVMTypeRef get_el_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef get_el_type =
      llvm_make_vmvalue_func_type(backend, get_el_params, 2, 0);
  backend->func_vm_get_element =
      LLVMAddFunction(backend->module, "vm_get_element_ptr", get_el_type);

  // vm_set_element_ptr(VM*, VMValue*, VMValue*, VMValue*) (pointer-based for
  // ABI compatibility)
  LLVMTypeRef set_el_params[] = {backend->ptr_type, backend->ptr_type,
                                 backend->ptr_type, backend->ptr_type};
  LLVMTypeRef set_el_type =
      LLVMFunctionType(backend->void_type, set_el_params, 4, 0);
  backend->func_vm_set_element =
      LLVMAddFunction(backend->module, "vm_set_element_ptr", set_el_type);

  // ====== AOT Builtin Functions ======

  // aot_to_string_ptr(VMValue*) -> VMValue
  LLVMTypeRef to_str_params[] = {backend->ptr_type};
  LLVMTypeRef to_str_type =
      llvm_make_vmvalue_func_type(backend, to_str_params, 1, 0);
  backend->func_aot_to_string =
      LLVMAddFunction(backend->module, "aot_to_string_ptr", to_str_type);

  // aot_runtime_init() -> void
  LLVMTypeRef rt_init_type = LLVMFunctionType(backend->void_type, nullptr, 0, 0);
  backend->func_aot_runtime_init =
      LLVMAddFunction(backend->module, "aot_runtime_init", rt_init_type);

  // aot_to_int_ptr(VMValue*) -> int64
  LLVMTypeRef to_int_params[] = {backend->ptr_type};
  LLVMTypeRef to_int_type =
      LLVMFunctionType(backend->int_type, to_int_params, 1, 0);
  backend->func_aot_to_int =
      LLVMAddFunction(backend->module, "aot_to_int_ptr", to_int_type);

  // aot_to_json_ptr(VMValue*) -> VMValue
  LLVMTypeRef to_json_params[] = {backend->ptr_type};
  LLVMTypeRef to_json_type =
      llvm_make_vmvalue_func_type(backend, to_json_params, 1, 0);
  backend->func_aot_to_json =
      LLVMAddFunction(backend->module, "aot_to_json_ptr", to_json_type);

  // aot_to_float_ptr(VMValue*) -> double
  LLVMTypeRef to_float_params[] = {backend->ptr_type};
  LLVMTypeRef to_float_type =
      LLVMFunctionType(backend->float_type, to_float_params, 1, 0);
  backend->func_aot_to_float =
      LLVMAddFunction(backend->module, "aot_to_float_ptr", to_float_type);

  // aot_len_ptr(VMValue*) -> int64
  LLVMTypeRef len_params[] = {backend->ptr_type};
  LLVMTypeRef len_type = LLVMFunctionType(backend->int_type, len_params, 1, 0);
  backend->func_aot_len =
      LLVMAddFunction(backend->module, "aot_len_ptr", len_type);

  // aot_array_push(VMValue*, VMValue*) -> void (pointer ABI)
  LLVMTypeRef push_aot_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef push_aot_type =
      LLVMFunctionType(backend->void_type, push_aot_params, 2, 0);
  backend->func_aot_array_push =
      LLVMAddFunction(backend->module, "aot_array_push", push_aot_type);

  // aot_array_pop(VMValue) -> VMValue
  LLVMTypeRef pop_params[] = {backend->vm_value_type};
  LLVMTypeRef pop_type =
      llvm_make_vmvalue_func_type(backend, pop_params, 1, 0);
  backend->func_aot_array_pop =
      LLVMAddFunction(backend->module, "aot_array_pop", pop_type);

  // ObjStruct heap-promotion runtime (Plan 04 v2).
  //
  // aot_struct_alloc(const char *type_name, int field_count) -> VMValue
  LLVMTypeRef struct_alloc_params[] = {backend->ptr_type, backend->int32_type};
  LLVMTypeRef struct_alloc_type =
      llvm_make_vmvalue_func_type(backend, struct_alloc_params, 2, 0);
  backend->func_aot_struct_alloc =
      LLVMAddFunction(backend->module, "aot_struct_alloc", struct_alloc_type);

  // aot_struct_alloc_from_fields(const char *type_name, int field_count,
  //                              const int64_t *src) -> VMValue
  LLVMTypeRef struct_clone_params[] = {backend->ptr_type, backend->int32_type,
                                       backend->ptr_type};
  LLVMTypeRef struct_clone_type =
      llvm_make_vmvalue_func_type(backend, struct_clone_params, 3, 0);
  backend->func_aot_struct_alloc_from_fields = LLVMAddFunction(
      backend->module, "aot_struct_alloc_from_fields", struct_clone_type);

  // aot_struct_get_field_ptr(VMValue *v, int idx) -> i64
  // Pointer ABI: matches the caller convention used by aot_array_push +
  // aot_print_value etc. Avoids the {i32,[4xi8],i64} by-value struct
  // mismatch with the C ABI on Windows x64.
  LLVMTypeRef struct_get_params[] = {backend->ptr_type, backend->int32_type};
  LLVMTypeRef struct_get_type =
      LLVMFunctionType(backend->int_type, struct_get_params, 2, 0);
  backend->func_aot_struct_get_field = LLVMAddFunction(
      backend->module, "aot_struct_get_field_ptr", struct_get_type);

  // aot_struct_set_field_ptr(VMValue *v, int idx, i64 val) -> void
  LLVMTypeRef struct_set_params[] = {backend->ptr_type, backend->int32_type,
                                     backend->int_type};
  LLVMTypeRef struct_set_type =
      LLVMFunctionType(backend->void_type, struct_set_params, 3, 0);
  backend->func_aot_struct_set_field = LLVMAddFunction(
      backend->module, "aot_struct_set_field_ptr", struct_set_type);

  // aot_struct_unpack_to(VMValue *v, int field_count, i64 *dst) -> void
  LLVMTypeRef struct_unpack_params[] = {
      backend->ptr_type, backend->int32_type, backend->ptr_type};
  LLVMTypeRef struct_unpack_type =
      LLVMFunctionType(backend->void_type, struct_unpack_params, 3, 0);
  backend->func_aot_struct_unpack_to = LLVMAddFunction(
      backend->module, "aot_struct_unpack_to", struct_unpack_type);

  // ====== Fast Array Access (value-based, no alloca) ======
  // aot_array_get_fast(VMValue arr, i64 index) -> VMValue
  LLVMTypeRef get_fast_params[] = {backend->vm_value_type, backend->int_type};
  LLVMTypeRef get_fast_type =
      llvm_make_vmvalue_func_type(backend, get_fast_params, 2, 0);
  backend->func_aot_array_get_fast =
      LLVMAddFunction(backend->module, "aot_array_get_fast", get_fast_type);

  // aot_array_set_fast(VMValue arr, i64 index, VMValue value) -> void
  LLVMTypeRef set_fast_params[] = {backend->vm_value_type, backend->int_type,
                                   backend->vm_value_type};
  LLVMTypeRef set_fast_type =
      LLVMFunctionType(backend->void_type, set_fast_params, 3, 0);
  backend->func_aot_array_set_fast =
      LLVMAddFunction(backend->module, "aot_array_set_fast", set_fast_type);

  // ====== RAW Pointer Array Access (Maximum Performance) ======
  // aot_array_get_raw(VMValue arr) -> ObjArray* (ptr)
  LLVMTypeRef get_raw_params[] = {backend->vm_value_type};
  LLVMTypeRef get_raw_type =
      LLVMFunctionType(backend->ptr_type, get_raw_params, 1, 0);
  backend->func_aot_array_get_raw =
      LLVMAddFunction(backend->module, "aot_array_get_raw", get_raw_type);

  // aot_array_get_raw_fast(ObjArray* arr, i64 index) -> VMValue
  LLVMTypeRef get_raw_fast_params[] = {backend->ptr_type, backend->int_type};
  LLVMTypeRef get_raw_fast_type =
      llvm_make_vmvalue_func_type(backend, get_raw_fast_params, 2, 0);
  backend->func_aot_array_get_raw_fast = LLVMAddFunction(
      backend->module, "aot_array_get_raw_fast", get_raw_fast_type);

  // aot_array_set_raw_fast(ObjArray* arr, i64 index, VMValue value) -> void
  LLVMTypeRef set_raw_fast_params[] = {backend->ptr_type, backend->int_type,
                                       backend->vm_value_type};
  LLVMTypeRef set_raw_fast_type =
      LLVMFunctionType(backend->void_type, set_raw_fast_params, 3, 0);
  backend->func_aot_array_set_raw_fast = LLVMAddFunction(
      backend->module, "aot_array_set_raw_fast", set_raw_fast_type);

  // aot_input() -> VMValue
  LLVMTypeRef input_type = llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_input =
      LLVMAddFunction(backend->module, "aot_input", input_type);

  // aot_env(name) -> VMValue (string)
  LLVMTypeRef env_params[] = {backend->vm_value_type};
  LLVMTypeRef env_type =
      llvm_make_vmvalue_func_type(backend, env_params, 1, 0);
  backend->func_aot_env =
      LLVMAddFunction(backend->module, "aot_env", env_type);

  // aot_arena_save() -> int (checkpoint handle)
  LLVMTypeRef arena_save_type =
      llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_arena_save =
      LLVMAddFunction(backend->module, "aot_arena_save", arena_save_type);

  // aot_now_iso8601() -> str
  backend->func_aot_now_iso8601 =
      LLVMAddFunction(backend->module, "aot_now_iso8601", arena_save_type);

  // aot_format_iso8601(secs) -> str
  LLVMTypeRef fmt_iso_params[] = {backend->vm_value_type};
  LLVMTypeRef fmt_iso_type =
      llvm_make_vmvalue_func_type(backend, fmt_iso_params, 1, 0);
  backend->func_aot_format_iso8601 =
      LLVMAddFunction(backend->module, "aot_format_iso8601", fmt_iso_type);

  // aot_parse_iso8601(str) -> int (unix seconds, -1 on parse failure)
  backend->func_aot_parse_iso8601 =
      LLVMAddFunction(backend->module, "aot_parse_iso8601", fmt_iso_type);

  // aot_weekday(secs) -> int  (0=Sunday … 6=Saturday)
  backend->func_aot_weekday =
      LLVMAddFunction(backend->module, "aot_weekday", fmt_iso_type);

  // aot_date_add_seconds(base, delta) -> int
  LLVMTypeRef date_add_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef date_add_type =
      llvm_make_vmvalue_func_type(backend, date_add_params, 2, 0);
  backend->func_aot_date_add_seconds =
      LLVMAddFunction(backend->module, "aot_date_add_seconds", date_add_type);

  // aot_file_glob(pattern) -> array<str>
  backend->func_aot_file_glob =
      LLVMAddFunction(backend->module, "aot_file_glob", fmt_iso_type);

  // aot_csv_parse(str) -> array<array<str>>
  backend->func_aot_csv_parse =
      LLVMAddFunction(backend->module, "aot_csv_parse", fmt_iso_type);

  // aot_csv_emit(rows) -> str
  backend->func_aot_csv_emit =
      LLVMAddFunction(backend->module, "aot_csv_emit", fmt_iso_type);

  // aot_keys(obj) -> array<str>
  backend->func_aot_keys =
      LLVMAddFunction(backend->module, "aot_keys", fmt_iso_type);

  // Regex (std::regex) builtins. Two-arg: match/search/capture; three-arg: replace.
  LLVMTypeRef regex2_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef regex2_type =
      llvm_make_vmvalue_func_type(backend, regex2_params, 2, 0);
  backend->func_aot_regex_match =
      LLVMAddFunction(backend->module, "aot_regex_match", regex2_type);
  backend->func_aot_regex_search =
      LLVMAddFunction(backend->module, "aot_regex_search", regex2_type);
  backend->func_aot_regex_capture =
      LLVMAddFunction(backend->module, "aot_regex_capture", regex2_type);

  LLVMTypeRef regex3_params[] = {backend->vm_value_type, backend->vm_value_type,
                                 backend->vm_value_type};
  LLVMTypeRef regex3_type =
      llvm_make_vmvalue_func_type(backend, regex3_params, 3, 0);
  backend->func_aot_regex_replace =
      LLVMAddFunction(backend->module, "aot_regex_replace", regex3_type);

  // aot_http_request(method, url, body) -> json (response object)
  LLVMTypeRef http_req_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef http_req_type =
      llvm_make_vmvalue_func_type(backend, http_req_params, 3, 0);
  backend->func_aot_http_request =
      LLVMAddFunction(backend->module, "aot_http_request", http_req_type);

  // aot_arena_restore(handle) -> int (0)
  LLVMTypeRef arena_restore_params[] = {backend->vm_value_type};
  LLVMTypeRef arena_restore_type =
      llvm_make_vmvalue_func_type(backend, arena_restore_params, 1, 0);
  backend->func_aot_arena_restore =
      LLVMAddFunction(backend->module, "aot_arena_restore", arena_restore_type);

  // aot_trim_ptr(VMValue*) -> VMValue
  LLVMTypeRef trim_params[] = {backend->ptr_type};
  LLVMTypeRef trim_type =
      llvm_make_vmvalue_func_type(backend, trim_params, 1, 0);
  backend->func_aot_trim =
      LLVMAddFunction(backend->module, "aot_trim_ptr", trim_type);

  // aot_replace_ptr(VMValue*, VMValue*, VMValue*) -> VMValue
  LLVMTypeRef replace_params[] = {backend->ptr_type, backend->ptr_type,
                                  backend->ptr_type};
  LLVMTypeRef replace_type =
      llvm_make_vmvalue_func_type(backend, replace_params, 3, 0);
  backend->func_aot_replace =
      LLVMAddFunction(backend->module, "aot_replace_ptr", replace_type);

  // aot_split_ptr(VMValue*, VMValue*) -> VMValue
  LLVMTypeRef split_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef split_type =
      llvm_make_vmvalue_func_type(backend, split_params, 2, 0);
  backend->func_aot_split =
      LLVMAddFunction(backend->module, "aot_split_ptr", split_type);

  // File I/O Functions
  // aot_read_file(path) -> VMValue
  LLVMTypeRef read_params[] = {backend->ptr_type};
  LLVMTypeRef read_type =
      llvm_make_vmvalue_func_type(backend, read_params, 1, 0);
  backend->func_aot_read_file =
      LLVMAddFunction(backend->module, "aot_read_file_ptr", read_type);

  // aot_write_file(path, content) -> VMValue
  LLVMTypeRef write_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef write_type =
      llvm_make_vmvalue_func_type(backend, write_params, 2, 0);
  backend->func_aot_write_file =
      LLVMAddFunction(backend->module, "aot_write_file_ptr", write_type);

  // aot_append_file(path, content) -> VMValue
  backend->func_aot_append_file =
      LLVMAddFunction(backend->module, "aot_append_file_ptr", write_type);

  backend->func_aot_file_exists =
      LLVMAddFunction(backend->module, "aot_file_exists_ptr", read_type);

  // aot_sha256(str) -> VMValue (lowercase 64-char hex digest as str).
  // Same single-VMValue-pointer ABI as read_file/file_exists.
  backend->func_aot_sha256 =
      LLVMAddFunction(backend->module, "aot_sha256_ptr", read_type);

  // Exception Handling Functions
  // aot_try_push() -> jmp_buf* (ptr)
  LLVMTypeRef try_push_type = LLVMFunctionType(backend->ptr_type, nullptr, 0, 0);
  backend->func_aot_try_push =
      LLVMAddFunction(backend->module, "aot_try_push", try_push_type);

  // aot_try_pop() -> void
  LLVMTypeRef try_pop_type = LLVMFunctionType(backend->void_type, nullptr, 0, 0);
  backend->func_aot_try_pop =
      LLVMAddFunction(backend->module, "aot_try_pop", try_pop_type);

  // aot_throw_ptr(VMValue*) -> void (noreturn)
  LLVMTypeRef throw_params[] = {backend->ptr_type};
  LLVMTypeRef throw_type =
      LLVMFunctionType(backend->void_type, throw_params, 1, 0);
  backend->func_aot_throw =
      LLVMAddFunction(backend->module, "aot_throw_ptr", throw_type);

  // aot_get_exception() -> VMValue
  LLVMTypeRef get_exc_type =
      llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_get_exception =
      LLVMAddFunction(backend->module, "aot_get_exception", get_exc_type);

  // setjmp signature is platform-dependent.
  //   Linux/macOS: int setjmp(jmp_buf)               -- 1 ptr arg
  //   Windows x64: int _setjmpex(jmp_buf, void*)     -- 2 ptr args; the second
  //                arg is the SEH frame address. Calling the 1-arg form leaves
  //                the SEH frame unset and the matching longjmp later corrupts
  //                stack unwind => process aborts at first throw.
  // The C-side macro setjmp(buf) expands to either form; we explicitly target
  // the right ABI here.
#ifdef _WIN32
  {
    LLVMTypeRef setjmp_params[] = {backend->ptr_type, backend->ptr_type};
    LLVMTypeRef setjmp_type =
        LLVMFunctionType(backend->int32_type, setjmp_params, 2, 0);
    backend->func_setjmp =
        LLVMAddFunction(backend->module, "_setjmpex", setjmp_type);
  }
#else
  {
    LLVMTypeRef setjmp_params[] = {backend->ptr_type};
    LLVMTypeRef setjmp_type =
        LLVMFunctionType(backend->int32_type, setjmp_params, 1, 0);
    backend->func_setjmp =
        LLVMAddFunction(backend->module, "setjmp", setjmp_type);
  }
#endif
  LLVMAddAttributeAtIndex(
      backend->func_setjmp, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context,
          LLVMGetEnumAttributeKindForName("returns_twice", 13), 0));

  // llvm.frameaddress.p0(i32) -> ptr  -- needed to pass the current frame
  // address as setjmp's second arg on Windows x64.
#ifdef _WIN32
  {
    LLVMTypeRef fa_params[] = {backend->int32_type};
    LLVMTypeRef fa_type =
        LLVMFunctionType(backend->ptr_type, fa_params, 1, 0);
    backend->func_frameaddress = LLVMAddFunction(
        backend->module, "llvm.frameaddress.p0", fa_type);
  }
#else
  backend->func_frameaddress = nullptr;
#endif

  // aot_clock_ms() -> VMValue (float ms)
  LLVMTypeRef clock_type = llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_clock_ms =
      LLVMAddFunction(backend->module, "aot_clock_ms", clock_type);

  // Socket Functions
  // aot_socket_server(host, port) -> int (fd)
  LLVMTypeRef sock_server_params[] = {backend->vm_value_type,
                                      backend->vm_value_type};
  LLVMTypeRef sock_server_type =
      llvm_make_vmvalue_func_type(backend, sock_server_params, 2, 0);
  backend->func_aot_socket_server =
      LLVMAddFunction(backend->module, "aot_socket_server", sock_server_type);

  // aot_socket_client(host, port) -> int (fd)
  backend->func_aot_socket_client =
      LLVMAddFunction(backend->module, "aot_socket_client", sock_server_type);

  // aot_socket_accept(server_fd) -> int (client_fd)
  LLVMTypeRef sock_accept_params[] = {backend->vm_value_type};
  LLVMTypeRef sock_accept_type =
      llvm_make_vmvalue_func_type(backend, sock_accept_params, 1, 0);
  backend->func_aot_socket_accept =
      LLVMAddFunction(backend->module, "aot_socket_accept", sock_accept_type);

  // aot_socket_send(fd, data) -> int (bytes)
  LLVMTypeRef sock_send_params[] = {backend->vm_value_type,
                                    backend->vm_value_type};
  LLVMTypeRef sock_send_type =
      llvm_make_vmvalue_func_type(backend, sock_send_params, 2, 0);
  backend->func_aot_socket_send =
      LLVMAddFunction(backend->module, "aot_socket_send", sock_send_type);

  // aot_socket_receive(fd, size) -> string
  // Uses same signature as send (2 params)
  backend->func_aot_socket_receive =
      LLVMAddFunction(backend->module, "aot_socket_receive", sock_send_type);

  // aot_socket_close(fd) -> VMValue (sentinel 0)
  // Originally void-returning, but that triggered a Windows MinGW64 ABI
  // crash for struct-by-value args on void functions; the runtime now
  // returns a VMValue so we keep the codegen calling convention uniform
  // with every other builtin.
  LLVMTypeRef sock_close_params[] = {backend->vm_value_type};
  LLVMTypeRef sock_close_type =
      llvm_make_vmvalue_func_type(backend, sock_close_params, 1, 0);
  backend->func_aot_socket_close =
      LLVMAddFunction(backend->module, "aot_socket_close", sock_close_type);

  // aot_socket_set_nonblocking(fd) -> int (1 ok, 0 fail). Same signature
  // as socket_close so we reuse `sock_close_type`.
  backend->func_aot_socket_set_nonblocking = LLVMAddFunction(
      backend->module, "aot_socket_set_nonblocking", sock_close_type);

  // aot_socket_poll(fds_array, timeout_ms) -> json array of indices.
  // 2 VMValue params, returns VMValue.
  LLVMTypeRef sock_poll_params[] = {backend->vm_value_type,
                                    backend->vm_value_type};
  LLVMTypeRef sock_poll_type =
      llvm_make_vmvalue_func_type(backend, sock_poll_params, 2, 0);
  backend->func_aot_socket_poll =
      LLVMAddFunction(backend->module, "aot_socket_poll", sock_poll_type);

  // TLS server primitives. tls_init / tls_accept take 2 args; tls_recv
  // and tls_send take 2 args; tls_close and tls_ctx_free take 1 arg.
  // All return a VMValue (int, string, or sentinel).
  backend->func_aot_tls_init =
      LLVMAddFunction(backend->module, "aot_tls_init", sock_poll_type);
  backend->func_aot_tls_accept =
      LLVMAddFunction(backend->module, "aot_tls_accept", sock_poll_type);
  backend->func_aot_tls_recv =
      LLVMAddFunction(backend->module, "aot_tls_recv", sock_poll_type);
  backend->func_aot_tls_send =
      LLVMAddFunction(backend->module, "aot_tls_send", sock_poll_type);
  backend->func_aot_tls_close =
      LLVMAddFunction(backend->module, "aot_tls_close", sock_close_type);
  backend->func_aot_tls_ctx_free =
      LLVMAddFunction(backend->module, "aot_tls_ctx_free", sock_close_type);

  // aot_call_dynamic(handler_name) -> VMValue
  // Param is VMValue (string)
  LLVMTypeRef call_dyn_params[] = {backend->vm_value_type};
  LLVMTypeRef call_dyn_type =
      llvm_make_vmvalue_func_type(backend, call_dyn_params, 1, 0);
  backend->func_aot_call_dynamic =
      LLVMAddFunction(backend->module, "aot_call_dynamic", call_dyn_type);

  // ====== Fast String Operations ======
  // aot_string_concat_fast_ptr(VMValue*, VMValue*) -> VMValue
  LLVMTypeRef str_concat_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef str_concat_type =
      llvm_make_vmvalue_func_type(backend, str_concat_params, 2, 0);
  backend->func_aot_string_concat_fast = LLVMAddFunction(
      backend->module, "aot_string_concat_fast_ptr", str_concat_type);

  // ====== StringBuilder Functions ======
  // aot_stringbuilder_new(int capacity) -> ptr
  LLVMTypeRef sb_new_params[] = {backend->int32_type};
  LLVMTypeRef sb_new_type =
      LLVMFunctionType(backend->ptr_type, sb_new_params, 1, 0);
  backend->func_aot_stringbuilder_new =
      LLVMAddFunction(backend->module, "aot_stringbuilder_new", sb_new_type);

  // aot_stringbuilder_append_vmvalue(ptr, VMValue) -> void
  LLVMTypeRef sb_append_params[] = {backend->ptr_type, backend->vm_value_type};
  LLVMTypeRef sb_append_type =
      LLVMFunctionType(backend->void_type, sb_append_params, 2, 0);
  backend->func_aot_stringbuilder_append_vmvalue = LLVMAddFunction(
      backend->module, "aot_stringbuilder_append_vmvalue", sb_append_type);

  // aot_stringbuilder_to_string(ptr) -> VMValue
  LLVMTypeRef sb_tostring_params[] = {backend->ptr_type};
  LLVMTypeRef sb_tostring_type =
      llvm_make_vmvalue_func_type(backend, sb_tostring_params, 1, 0);
  backend->func_aot_stringbuilder_to_string = LLVMAddFunction(
      backend->module, "aot_stringbuilder_to_string", sb_tostring_type);

  // aot_stringbuilder_free(ptr) -> void
  LLVMTypeRef sb_free_params[] = {backend->ptr_type};
  LLVMTypeRef sb_free_type =
      LLVMFunctionType(backend->void_type, sb_free_params, 1, 0);
  backend->func_aot_stringbuilder_free =
      LLVMAddFunction(backend->module, "aot_stringbuilder_free", sb_free_type);

  // ====== Threading Functions ======
  // aot_thread_create(func_ptr, arg) -> VMValue (thread_id)
  LLVMTypeRef thread_create_params[] = {backend->ptr_type,
                                        backend->vm_value_type};
  LLVMTypeRef thread_create_type =
      llvm_make_vmvalue_func_type(backend, thread_create_params, 2, 0);
  backend->func_aot_thread_create =
      LLVMAddFunction(backend->module, "aot_thread_create", thread_create_type);

  // aot_thread_join / aot_thread_detach were previously `(VMValue) -> void`,
  // which collides with the same MinGW64 ABI quirk that crashed
  // `aot_socket_close`. Both runtime functions now return VMValue
  // (sentinel 0) so codegen calls them via the standard VMValue
  // calling convention.
  LLVMTypeRef thread_join_params[] = {backend->vm_value_type};
  LLVMTypeRef thread_join_type =
      llvm_make_vmvalue_func_type(backend, thread_join_params, 1, 0);
  backend->func_aot_thread_join =
      LLVMAddFunction(backend->module, "aot_thread_join", thread_join_type);

  backend->func_aot_thread_detach =
      LLVMAddFunction(backend->module, "aot_thread_detach", thread_join_type);

  // aot_mutex_create() -> VMValue (mutex_ptr)
  LLVMTypeRef mutex_create_type =
      llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_mutex_create =
      LLVMAddFunction(backend->module, "aot_mutex_create", mutex_create_type);

  // mutex ops were `(VMValue) -> void` and tripped the same MinGW64
  // ABI quirk fixed elsewhere in this file. Now they return VMValue
  // (sentinel 0) so the codegen calling convention is uniform.
  LLVMTypeRef mutex_op_params[] = {backend->vm_value_type};
  LLVMTypeRef mutex_op_type =
      llvm_make_vmvalue_func_type(backend, mutex_op_params, 1, 0);
  backend->func_aot_mutex_lock =
      LLVMAddFunction(backend->module, "aot_mutex_lock", mutex_op_type);
  backend->func_aot_mutex_unlock =
      LLVMAddFunction(backend->module, "aot_mutex_unlock", mutex_op_type);
  backend->func_aot_mutex_destroy =
      LLVMAddFunction(backend->module, "aot_mutex_destroy", mutex_op_type);

  // ====== HTTP Functions ======
  // aot_http_parse_request(raw) -> VMValue (object)
  LLVMTypeRef http_parse_params[] = {backend->vm_value_type};
  LLVMTypeRef http_parse_type =
      llvm_make_vmvalue_func_type(backend, http_parse_params, 1, 0);
  backend->func_aot_http_parse_request = LLVMAddFunction(
      backend->module, "aot_http_parse_request", http_parse_type);

  // aot_http_create_response(status, content_type, body) -> VMValue (string)
  LLVMTypeRef http_response_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef http_response_type =
      llvm_make_vmvalue_func_type(backend, http_response_params, 3, 0);
  backend->func_aot_http_create_response = LLVMAddFunction(
      backend->module, "aot_http_create_response", http_response_type);

  // aot_http_status_text(status) -> VMValue (string)
  // Maps a numeric status to its IANA reason phrase (404 -> "Not Found").
  backend->func_aot_http_status_text = LLVMAddFunction(
      backend->module, "aot_http_status_text", http_parse_type);

  // aot_http_create_response_full(status, ct, body, headers) -> string
  // 4-arg variant: extra headers are taken from a JSON object.
  LLVMTypeRef http_response4_params[] = {
      backend->vm_value_type, backend->vm_value_type,
      backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef http_response4_type =
      llvm_make_vmvalue_func_type(backend, http_response4_params, 4, 0);
  backend->func_aot_http_create_response_full = LLVMAddFunction(
      backend->module, "aot_http_create_response_full", http_response4_type);

  // aot_http_create_response_keepalive(status, ct, body, headers, keep) -> string
  // 5-arg variant. The keep flag drives the emitted Connection header.
  LLVMTypeRef http_response5_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type,
      backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef http_response5_type =
      llvm_make_vmvalue_func_type(backend, http_response5_params, 5, 0);
  backend->func_aot_http_create_response_keepalive = LLVMAddFunction(
      backend->module, "aot_http_create_response_keepalive",
      http_response5_type);

  // aot_http_should_keepalive(parsed) -> int (0/1, packed as VMValue int)
  backend->func_aot_http_should_keepalive = LLVMAddFunction(
      backend->module, "aot_http_should_keepalive", http_parse_type);

  // aot_http_recv_request(client_fd, max_bytes) -> str (VMValue)
  LLVMTypeRef http_recv_params[] = {backend->vm_value_type,
                                    backend->vm_value_type};
  LLVMTypeRef http_recv_type =
      llvm_make_vmvalue_func_type(backend, http_recv_params, 2, 0);
  backend->func_aot_http_recv_request = LLVMAddFunction(
      backend->module, "aot_http_recv_request", http_recv_type);

  // aot_path_match(pattern, path) -> VMValue (object)
  // Matches Express-style routes (/users/:id, /static/*) and returns
  // {"matched": bool, "params": {...}}.
  LLVMTypeRef path_match_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef path_match_type =
      llvm_make_vmvalue_func_type(backend, path_match_params, 2, 0);
  backend->func_aot_path_match = LLVMAddFunction(
      backend->module, "aot_path_match", path_match_type);

  // aot_parse_query(str) -> VMValue (object)
  backend->func_aot_parse_query = LLVMAddFunction(
      backend->module, "aot_parse_query", http_parse_type);

  // aot_wings_build_response(result, default_headers, keep) -> wire-string
  // Native replacement for lib/wings.tpr's `_wings_build_response`.
  // Same 3 VMValue args as the keepalive response builder minus the
  // explicit status/ct (those are inferred from the result envelope).
  LLVMTypeRef wings_build_resp_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef wings_build_resp_type =
      llvm_make_vmvalue_func_type(backend, wings_build_resp_params, 3, 0);
  backend->func_aot_wings_build_response = LLVMAddFunction(
      backend->module, "aot_wings_build_response", wings_build_resp_type);

  // aot_wings_find_route(routes, method, path) -> {index, params}
  // Two-pass route lookup (exact, then pattern) in one native call,
  // replacing the per-request Tulpar dispatch through
  // `_find_route_with_params`. Shape matches `aot_path_match` — three
  // VMValue inputs, one VMValue object output.
  LLVMTypeRef wings_find_route_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef wings_find_route_type =
      llvm_make_vmvalue_func_type(backend, wings_find_route_params, 3, 0);
  backend->func_aot_wings_find_route = LLVMAddFunction(
      backend->module, "aot_wings_find_route", wings_find_route_type);

  // aot_string_pin(str) -> str (permanent copy)
  // VMValue->VMValue. Used by the wings response cache to pin entries
  // past the per-request `arena_restore`.
  LLVMTypeRef string_pin_params[] = {backend->vm_value_type};
  LLVMTypeRef string_pin_type =
      llvm_make_vmvalue_func_type(backend, string_pin_params, 1, 0);
  backend->func_aot_string_pin = LLVMAddFunction(
      backend->module, "aot_string_pin", string_pin_type);

  // aot_parse_cookies(str) -> VMValue (object)
  // Same VMValue (str) -> VMValue (object) shape as parse_query, so we
  // reuse http_parse_type rather than declaring a fresh signature.
  backend->func_aot_parse_cookies = LLVMAddFunction(
      backend->module, "aot_parse_cookies", http_parse_type);

  // aot_exit_i32(int code) -> noreturn  (process termination)
  // Takes a raw i32 to avoid VMValue ABI complications for a one-shot call.
  LLVMTypeRef exit_params[] = {backend->int32_type};
  LLVMTypeRef exit_type = LLVMFunctionType(backend->void_type, exit_params, 1, 0);
  backend->func_aot_exit =
      LLVMAddFunction(backend->module, "aot_exit_i32", exit_type);

  // ====== Math Functions ======
  // Single param math: abs, sqrt, floor, ceil, round, sin, cos, tan, asin,
  // acos, atan, exp, log, log10, log2, sinh, cosh, tanh, cbrt, trunc
  // Special case: abs takes pointer to avoid ABI issues (void return, result
  // ptr, arg ptr)
  LLVMTypeRef math1_ptr_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef math1_ptr_type =
      LLVMFunctionType(backend->void_type, math1_ptr_params, 2, 0);
  backend->func_aot_math_abs =
      LLVMAddFunction(backend->module, "aot_math_abs", math1_ptr_type);

  // Single param math (Standard): sqrt, floor, ceil, ...
  LLVMTypeRef math1_params[] = {backend->ptr_type};
  LLVMTypeRef math1_type =
      llvm_make_vmvalue_func_type(backend, math1_params, 1, 0);

  // backend->func_aot_math_abs = ... (Removed from here)
  backend->func_aot_math_sqrt =
      LLVMAddFunction(backend->module, "aot_math_sqrt_ptr", math1_type);
  backend->func_aot_math_floor =
      LLVMAddFunction(backend->module, "aot_math_floor_ptr", math1_type);
  backend->func_aot_math_ceil =
      LLVMAddFunction(backend->module, "aot_math_ceil_ptr", math1_type);
  backend->func_aot_math_round =
      LLVMAddFunction(backend->module, "aot_math_round_ptr", math1_type);
  backend->func_aot_math_sin =
      LLVMAddFunction(backend->module, "aot_math_sin_ptr", math1_type);
  backend->func_aot_math_cos =
      LLVMAddFunction(backend->module, "aot_math_cos_ptr", math1_type);
  backend->func_aot_math_tan =
      LLVMAddFunction(backend->module, "aot_math_tan_ptr", math1_type);
  backend->func_aot_math_asin =
      LLVMAddFunction(backend->module, "aot_math_asin_ptr", math1_type);
  backend->func_aot_math_acos =
      LLVMAddFunction(backend->module, "aot_math_acos_ptr", math1_type);
  backend->func_aot_math_atan =
      LLVMAddFunction(backend->module, "aot_math_atan_ptr", math1_type);
  backend->func_aot_math_exp =
      LLVMAddFunction(backend->module, "aot_math_exp_ptr", math1_type);
  backend->func_aot_math_log =
      LLVMAddFunction(backend->module, "aot_math_log_ptr", math1_type);
  backend->func_aot_math_log10 =
      LLVMAddFunction(backend->module, "aot_math_log10_ptr", math1_type);
  backend->func_aot_math_log2 =
      LLVMAddFunction(backend->module, "aot_math_log2_ptr", math1_type);
  backend->func_aot_math_sinh =
      LLVMAddFunction(backend->module, "aot_math_sinh_ptr", math1_type);
  backend->func_aot_math_cosh =
      LLVMAddFunction(backend->module, "aot_math_cosh_ptr", math1_type);
  backend->func_aot_math_tanh =
      LLVMAddFunction(backend->module, "aot_math_tanh_ptr", math1_type);
  backend->func_aot_math_cbrt =
      LLVMAddFunction(backend->module, "aot_math_cbrt_ptr", math1_type);
  backend->func_aot_math_trunc =
      LLVMAddFunction(backend->module, "aot_math_trunc_ptr", math1_type);

  // Two param math: pow, atan2, hypot, fmod, min, max, randint
  LLVMTypeRef math2_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef math2_type =
      llvm_make_vmvalue_func_type(backend, math2_params, 2, 0);

  backend->func_aot_math_pow =
      LLVMAddFunction(backend->module, "aot_math_pow_ptr", math2_type);
  backend->func_aot_math_atan2 =
      LLVMAddFunction(backend->module, "aot_math_atan2_ptr", math2_type);
  backend->func_aot_math_hypot =
      LLVMAddFunction(backend->module, "aot_math_hypot_ptr", math2_type);
  backend->func_aot_math_fmod =
      LLVMAddFunction(backend->module, "aot_math_fmod_ptr", math2_type);
  backend->func_aot_math_mod =
      LLVMAddFunction(backend->module, "aot_math_mod_ptr", math2_type);
  backend->func_aot_math_min =
      LLVMAddFunction(backend->module, "aot_math_min_ptr", math2_type);
  backend->func_aot_math_max =
      LLVMAddFunction(backend->module, "aot_math_max_ptr", math2_type);
  backend->func_aot_math_randint =
      LLVMAddFunction(backend->module, "aot_math_randint_ptr", math2_type);

  // No param math: random
  LLVMTypeRef math0_type = llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_math_random =
      LLVMAddFunction(backend->module, "aot_math_random", math0_type);

  // ====== String Functions ======
  // Single param: upper, lower, reverse, isEmpty, capitalize, isDigit, isAlpha
  LLVMTypeRef str1_params[] = {backend->ptr_type};
  LLVMTypeRef str1_type =
      llvm_make_vmvalue_func_type(backend, str1_params, 1, 0);

  backend->func_aot_string_upper =
      LLVMAddFunction(backend->module, "aot_string_upper_ptr", str1_type);
  backend->func_aot_string_lower =
      LLVMAddFunction(backend->module, "aot_string_lower_ptr", str1_type);
  backend->func_aot_string_reverse =
      LLVMAddFunction(backend->module, "aot_string_reverse_ptr", str1_type);
  backend->func_aot_string_is_empty =
      LLVMAddFunction(backend->module, "aot_string_is_empty_ptr", str1_type);
  backend->func_aot_string_capitalize =
      LLVMAddFunction(backend->module, "aot_string_capitalize_ptr", str1_type);
  backend->func_aot_string_is_digit =
      LLVMAddFunction(backend->module, "aot_string_is_digit_ptr", str1_type);
  backend->func_aot_string_is_alpha =
      LLVMAddFunction(backend->module, "aot_string_is_alpha_ptr", str1_type);

  // Two param: contains, startsWith, endsWith, indexOf, repeat, count, join
  LLVMTypeRef str2_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef str2_type =
      llvm_make_vmvalue_func_type(backend, str2_params, 2, 0);

  backend->func_aot_string_contains =
      LLVMAddFunction(backend->module, "aot_string_contains_ptr", str2_type);
  backend->func_aot_string_starts_with =
      LLVMAddFunction(backend->module, "aot_string_starts_with_ptr", str2_type);
  backend->func_aot_string_ends_with =
      LLVMAddFunction(backend->module, "aot_string_ends_with_ptr", str2_type);
  backend->func_aot_string_index_of =
      LLVMAddFunction(backend->module, "aot_string_index_of_ptr", str2_type);
  backend->func_aot_string_repeat =
      LLVMAddFunction(backend->module, "aot_string_repeat_ptr", str2_type);
  backend->func_aot_string_count =
      LLVMAddFunction(backend->module, "aot_string_count_ptr", str2_type);
  backend->func_aot_string_join =
      LLVMAddFunction(backend->module, "aot_string_join_ptr", str2_type);

  // Three param: substring(str, start, end)
  LLVMTypeRef str3_params[] = {backend->ptr_type, backend->ptr_type,
                               backend->ptr_type};
  LLVMTypeRef str3_type =
      llvm_make_vmvalue_func_type(backend, str3_params, 3, 0);
  backend->func_aot_string_substring =
      LLVMAddFunction(backend->module, "aot_string_substring_ptr", str3_type);

  // ====== Time Functions ======
  LLVMTypeRef time0_type = llvm_make_vmvalue_func_type(backend, nullptr, 0, 0);
  backend->func_aot_timestamp =
      LLVMAddFunction(backend->module, "aot_timestamp", time0_type);
  backend->func_aot_time_ms =
      LLVMAddFunction(backend->module, "aot_time_ms", time0_type);

  LLVMTypeRef sleep_params[] = {backend->ptr_type};
  LLVMTypeRef sleep_type =
      LLVMFunctionType(backend->void_type, sleep_params, 1, 0);
  backend->func_aot_sleep =
      LLVMAddFunction(backend->module, "aot_sleep_ptr", sleep_type);

  // ====== JSON Functions ======
  // aot_from_json(str) -> VMValue
  LLVMTypeRef from_json_params[] = {backend->vm_value_type};
  LLVMTypeRef from_json_type =
      llvm_make_vmvalue_func_type(backend, from_json_params, 1, 0);
  backend->func_aot_from_json =
      LLVMAddFunction(backend->module, "aot_from_json", from_json_type);

  // ====== Input Functions ======
  // aot_input_int(prompt) -> VMValue
  // aot_input_float(prompt) -> VMValue
  LLVMTypeRef input_prompt_params[] = {backend->vm_value_type};
  LLVMTypeRef input_prompt_type =
      llvm_make_vmvalue_func_type(backend, input_prompt_params, 1, 0);
  backend->func_aot_input_int =
      LLVMAddFunction(backend->module, "aot_input_int", input_prompt_type);
  backend->func_aot_input_float =
      LLVMAddFunction(backend->module, "aot_input_float", input_prompt_type);

  // ====== Range Function ======
  // aot_range(end) -> VMValue (array)
  LLVMTypeRef range_params[] = {backend->vm_value_type};
  LLVMTypeRef range_type =
      llvm_make_vmvalue_func_type(backend, range_params, 1, 0);
  backend->func_aot_range =
      LLVMAddFunction(backend->module, "aot_range", range_type);

  // ====== SQLite Database Functions ======
  // aot_db_open_ptr(path) -> int64 (db handle)
  LLVMTypeRef db_open_params[] = {backend->ptr_type};
  LLVMTypeRef db_open_type =
      llvm_make_vmvalue_func_type(backend, db_open_params, 1, 0);
  backend->func_aot_db_open =
      LLVMAddFunction(backend->module, "aot_db_open_ptr", db_open_type);

  // aot_db_close_ptr(db) -> void
  LLVMTypeRef db_close_params[] = {backend->ptr_type};
  LLVMTypeRef db_close_type =
      LLVMFunctionType(backend->void_type, db_close_params, 1, 0);
  backend->func_aot_db_close =
      LLVMAddFunction(backend->module, "aot_db_close_ptr", db_close_type);

  // aot_db_execute_ptr(db, sql) -> bool
  LLVMTypeRef db_exec_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef db_exec_type =
      llvm_make_vmvalue_func_type(backend, db_exec_params, 2, 0);
  backend->func_aot_db_execute =
      LLVMAddFunction(backend->module, "aot_db_execute_ptr", db_exec_type);

  // aot_db_query_ptr(db, sql) -> array
  backend->func_aot_db_query =
      LLVMAddFunction(backend->module, "aot_db_query_ptr", db_exec_type);

  // aot_db_last_insert_id(db) -> int64
  backend->func_aot_db_last_insert_id =
      LLVMAddFunction(backend->module, "aot_db_last_insert_id", db_open_type);

  // aot_db_error(db) -> string
  backend->func_aot_db_error =
      LLVMAddFunction(backend->module, "aot_db_error", db_open_type);

  // ====== Type Checking Functions ======
  LLVMTypeRef type_check_params[] = {backend->vm_value_type};
  LLVMTypeRef type_check_type =
      llvm_make_vmvalue_func_type(backend, type_check_params, 1, 0);

  backend->func_aot_typeof =
      LLVMAddFunction(backend->module, "aot_typeof", type_check_type);
  backend->func_aot_is_int =
      LLVMAddFunction(backend->module, "aot_is_int", type_check_type);
  backend->func_aot_is_float =
      LLVMAddFunction(backend->module, "aot_is_float", type_check_type);
  backend->func_aot_is_string =
      LLVMAddFunction(backend->module, "aot_is_string", type_check_type);
  backend->func_aot_is_array =
      LLVMAddFunction(backend->module, "aot_is_array", type_check_type);
  backend->func_aot_is_object =
      LLVMAddFunction(backend->module, "aot_is_object", type_check_type);
  backend->func_aot_is_bool =
      LLVMAddFunction(backend->module, "aot_is_bool", type_check_type);
}

LLVMBackend *llvm_backend_create(const char *module_name) {
  // calloc instead of malloc: every counter / pointer field defaults to 0 /
  // NULL. Previously this struct grew via malloc and each new counter had
  // to be initialised by hand below — `struct_type_count` (PR48) slipped
  // through that and produced a silent-path heisenbug where
  // register_struct_type() wrote far outside `struct_types[64]` and
  // crashed with ACCESS_VIOLATION before any IR was emitted. Zero-init
  // fixes the class of bug at the source.
  LLVMBackend *backend = static_cast<LLVMBackend*>(calloc(1, sizeof(LLVMBackend)));
  LLVMInitializeNativeTarget();

  backend->context = LLVMContextCreate();
  backend->module =
      LLVMModuleCreateWithNameInContext(module_name, backend->context);
  backend->builder = LLVMCreateBuilderInContext(backend->context);

  backend->int_type = LLVMInt64TypeInContext(backend->context);
  backend->int32_type = LLVMInt32TypeInContext(backend->context); // i32
  backend->float_type = LLVMDoubleTypeInContext(backend->context);
  backend->bool_type = LLVMInt1TypeInContext(backend->context);
  backend->void_type = LLVMVoidTypeInContext(backend->context);
  backend->ptr_type =
      LLVMPointerType(LLVMInt8TypeInContext(backend->context), 0);
  backend->string_type = backend->ptr_type;

  // Initialize VM Types
  llvm_init_types(backend);

  backend->current_function = nullptr;
  backend->current_scope = nullptr;
  backend->current_function = nullptr;
  backend->current_scope = nullptr;
  backend->loop_depth = 0;
  backend->function_count = 0;
  backend->imported_count = 0;

  // Enable static typing by default for performance
  backend->use_static_typing = 1;

  backend->quiet = 0;
  backend->had_error = 0;
  backend->source_text = nullptr;
  backend->source_filename = nullptr;

  // Declare Runtime
  declare_runtime_functions(backend);

  return backend;
}

void llvm_backend_destroy(LLVMBackend *backend) {
  if (!backend)
    return;
  LLVMDisposeBuilder(backend->builder);
  LLVMDisposeModule(backend->module);
  LLVMContextDispose(backend->context);
  for (int i = 0; i < backend->function_count; i++) {
    free(backend->functions[i].name);
    if (backend->functions[i].param_struct_names) {
      for (int p = 0; p < backend->functions[i].param_count; p++) {
        free(backend->functions[i].param_struct_names[p]);
      }
      free(backend->functions[i].param_struct_names);
    }
    free(backend->functions[i].return_struct_name);
  }
  free(backend);
}

void register_function(LLVMBackend *backend, const char *name,
                       LLVMTypeRef type) {
  if (backend->function_count < 128) {
    backend->functions[backend->function_count].name = my_strdup(name);
    backend->functions[backend->function_count].type = type;
    // Struct info defaults: NULL slots mean "not a struct param/return".
    // Populated by set_function_struct_info() right after register_function
    // when the caller (predeclare / codegen_func_def) has the AST in hand.
    backend->functions[backend->function_count].param_struct_names = nullptr;
    backend->functions[backend->function_count].param_count = 0;
    backend->functions[backend->function_count].return_struct_name = nullptr;
    backend->function_count++;
  }
}

// Populate per-function struct info from the AST. Walks parameters and the
// declared return type; if a slot is TYPE_CUSTOM and resolves to a registered
// struct entry, that slot's struct name is recorded so call-site codegen can
// switch to pointer-passing instead of VMValue boxing. Idempotent — safe to
// call from both predeclare and codegen_func_def (codegen_func_def reuses the
// pre-declared signature so the entry already exists).
static void set_function_struct_info(LLVMBackend *backend,
                                     const char *name, ASTNode_C *node) {
  if (!backend || !name || !node) return;
  if (node->type != AST_FUNCTION_DECL) return;
  for (int i = 0; i < backend->function_count; i++) {
    if (strcmp(backend->functions[i].name, name) != 0) continue;
    FunctionEntry *fe = &backend->functions[i];
    // Free any previously populated info so repeat calls don't leak.
    if (fe->param_struct_names) {
      for (int p = 0; p < fe->param_count; p++) free(fe->param_struct_names[p]);
      free(fe->param_struct_names);
      fe->param_struct_names = nullptr;
    }
    free(fe->return_struct_name);
    fe->return_struct_name = nullptr;
    fe->param_count = node->param_count;
    if (node->param_count > 0) {
      fe->param_struct_names = static_cast<char **>(
          calloc(node->param_count, sizeof(char *)));
      for (int p = 0; p < node->param_count; p++) {
        ASTNode_C *par = node->parameters[p];
        if (par && par->data_type == TYPE_CUSTOM &&
            par->return_custom_type) {
          if (find_struct_type(backend, par->return_custom_type)) {
            fe->param_struct_names[p] = my_strdup(par->return_custom_type);
          }
        }
      }
    }
    if (node->return_type == TYPE_CUSTOM && node->return_custom_type) {
      if (find_struct_type(backend, node->return_custom_type)) {
        fe->return_struct_name = my_strdup(node->return_custom_type);
      }
    }
    return;
  }
}

LLVMTypeRef get_function_type(LLVMBackend *backend, const char *name) {
  if (strcmp(name, "print") == 0 || strcmp(name, "printf") == 0) {
    LLVMValueRef f = LLVMGetNamedFunction(backend->module, "printf");
    if (f)
      return LLVMGlobalGetValueType(f);
  }
  for (int i = 0; i < backend->function_count; i++) {
    if (strcmp(backend->functions[i].name, name) == 0)
      return backend->functions[i].type;
  }
  return nullptr;
}

void enter_scope(LLVMBackend *backend) {
  Scope *scope = static_cast<Scope*>(malloc(sizeof(Scope)));
  scope->count = 0;
  scope->parent = backend->current_scope;
  backend->current_scope = scope;
}

void exit_scope(LLVMBackend *backend) {
  if (backend->current_scope) {
    Scope *old = backend->current_scope;
    backend->current_scope = old->parent;
    for (int i = 0; i < old->count; i++) {
      free(old->vars[i].name);
      if (old->vars[i].struct_type_name) free(old->vars[i].struct_type_name);
    }
    free(old);
  }
}

void add_local(LLVMBackend *backend, const char *name, LLVMValueRef val) {
  if (!backend->current_scope)
    return;
  Scope *s = backend->current_scope;
  if (s->count < 256) {
    s->vars[s->count].name = my_strdup(name);
    s->vars[s->count].value = val;
    s->vars[s->count].known_type = INFERRED_UNKNOWN;
    s->vars[s->count].native_value = nullptr;
    s->vars[s->count].struct_type_name = nullptr;
    s->count++;
  }
}

// Add local with known type for unboxed operations
void add_local_typed(LLVMBackend *backend, const char *name, LLVMValueRef val,
                     InferredType type, LLVMValueRef native_val) {
  if (!backend->current_scope)
    return;
  Scope *s = backend->current_scope;
  if (s->count < 256) {
    s->vars[s->count].name = my_strdup(name);
    s->vars[s->count].value = val;
    s->vars[s->count].known_type = type;
    s->vars[s->count].native_value = native_val;
    s->vars[s->count].struct_type_name = nullptr;
    s->count++;
  }
}

// Add a typed-struct local. `alloca_ptr` is an alloca to the LLVM struct
// type registered under `struct_name`. Field accesses against this name
// take the GEP+load/store path instead of vm_get_element / vm_set_element.
void add_local_struct(LLVMBackend *backend, const char *name,
                      LLVMValueRef alloca_ptr, const char *struct_name) {
  if (!backend->current_scope) return;
  Scope *s = backend->current_scope;
  if (s->count < 256) {
    s->vars[s->count].name = my_strdup(name);
    s->vars[s->count].value = alloca_ptr;
    s->vars[s->count].known_type = INFERRED_UNKNOWN;
    s->vars[s->count].native_value = nullptr;
    s->vars[s->count].struct_type_name = my_strdup(struct_name);
    s->count++;
  }
}

const char *get_local_struct_type(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0) {
        return s->vars[i].struct_type_name;
      }
    }
    s = s->parent;
  }
  return nullptr;
}

StructTypeEntry *find_struct_type(LLVMBackend *backend, const char *name) {
  if (!backend || !name) return nullptr;
  for (int i = 0; i < backend->struct_type_count; i++) {
    if (strcmp(backend->struct_types[i].name, name) == 0) {
      return &backend->struct_types[i];
    }
  }
  return nullptr;
}

int struct_type_field_index(StructTypeEntry *st, const char *field_name) {
  if (!st || !field_name) return -1;
  for (int i = 0; i < st->field_count; i++) {
    if (strcmp(st->field_names[i], field_name) == 0) return i;
  }
  return -1;
}

int struct_is_trivially_unboxable(StructTypeEntry *st) {
  if (!st) return 0;
  for (int i = 0; i < st->field_count; i++) {
    DataType ft = st->field_types[i];
    if (ft != TYPE_INT && ft != TYPE_BOOL) return 0;
  }
  return 1;
}

StructTypeEntry *register_struct_type(LLVMBackend *backend, ASTNode_C *type_decl) {
  if (!backend || !type_decl || type_decl->type != AST_TYPE_DECL) return nullptr;
  if (!type_decl->name || !type_decl->field_names || type_decl->field_count <= 0)
    return nullptr;
  if (backend->struct_type_count >= 64) {
    fprintf(stderr,
            "[AOT] Warning: struct table full; '%s' falls back to boxed path.\n",
            type_decl->name);
    return nullptr;
  }
  // Allow re-declaration to be a no-op when the layout matches; otherwise the
  // earlier registered version wins (parser/typeinfer have already flagged
  // duplicates upstream).
  StructTypeEntry *existing = find_struct_type(backend, type_decl->name);
  if (existing) return existing;

  StructTypeEntry *st = &backend->struct_types[backend->struct_type_count];
  st->name = my_strdup(type_decl->name);
  st->field_count = type_decl->field_count;
  st->field_names =
      static_cast<char **>(malloc(sizeof(char *) * st->field_count));
  st->field_types =
      static_cast<DataType *>(malloc(sizeof(DataType) * st->field_count));
  for (int i = 0; i < st->field_count; i++) {
    st->field_names[i] = my_strdup(type_decl->field_names[i]);
    st->field_types[i] = type_decl->field_types[i];
  }

  // Build the LLVM struct layout. Trivially unboxable fields (int/bool) map
  // to i64 — bool gets promoted to i64 so the struct stays naturally aligned
  // and field load/store sites don't have to special-case smaller integers.
  // Non-trivial fields fall back to vm_value_type so the struct still has a
  // reserved slot, but field access against that slot will need to take the
  // boxed path (left as a follow-up: this PR's VAR_DECL branch only takes
  // the typed alloca for trivially-unboxable structs).
  LLVMTypeRef *field_llvm =
      static_cast<LLVMTypeRef *>(malloc(sizeof(LLVMTypeRef) * st->field_count));
  for (int i = 0; i < st->field_count; i++) {
    DataType ft = st->field_types[i];
    if (ft == TYPE_INT || ft == TYPE_BOOL) {
      field_llvm[i] = backend->int_type;
    } else {
      field_llvm[i] = backend->vm_value_type;
    }
  }
  // Named struct improves IR readability without affecting layout.
  char ty_name[128];
  snprintf(ty_name, sizeof(ty_name), "tulpar_struct.%s", st->name);
  st->llvm_type = LLVMStructCreateNamed(backend->context, ty_name);
  LLVMStructSetBody(st->llvm_type, field_llvm, (unsigned)st->field_count, 0);
  free(field_llvm);

  backend->struct_type_count++;
  return st;
}

LLVMValueRef get_local(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].value;
    }
    s = s->parent;
  }

  // Fallback: Check for Global Variable (for imported modules)
  LLVMValueRef global_var = LLVMGetNamedGlobal(backend->module, name);
  if (global_var) {
    return global_var;
  }

  return nullptr;
}

// Get the inferred type of a local variable
InferredType get_local_type(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].known_type;
    }
    s = s->parent;
  }
  return INFERRED_UNKNOWN;
}

// Get the native (unboxed) value for a local if available
LLVMValueRef get_local_native(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].native_value;
    }
    s = s->parent;
  }
  return nullptr;
}

LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode_C *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode_C *node);

// Typed expression result for unboxed operations
typedef struct {
  LLVMValueRef value; // Native value (i64, double, or VMValue)
  InferredType type;  // Known type at compile time
  LLVMValueRef boxed; // Boxed VMValue (lazy, may be nullptr)
} TypedValue;

// Forward declarations for typed codegen
TypedValue codegen_typed_expr(LLVMBackend *backend, ASTNode_C *node);
LLVMValueRef box_typed_value(LLVMBackend *backend, TypedValue tv);

// Box a typed value to VMValue when needed
LLVMValueRef box_typed_value(LLVMBackend *backend, TypedValue tv) {
  if (tv.boxed)
    return tv.boxed;

  switch (tv.type) {
  case INFERRED_INT:
    return llvm_vm_val_int_val(backend, tv.value);
  case INFERRED_FLOAT:
    return llvm_build_vm_val_float(backend, tv.value);
  case INFERRED_BOOL: {
    // Convert i64 to bool VMValue
    LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
    s = LLVMBuildInsertValue(backend->builder, s,
                             LLVMConstInt(backend->int32_type, 2, 0), 0, "");  // VM_VAL_BOOL = 2
    s = LLVMBuildInsertValue(backend->builder, s, tv.value, 2, "");
    return s;
  }
  default:
    return tv.value; // Already boxed
  }
}

// Typed expression codegen - returns native values when possible
TypedValue codegen_typed_expr(LLVMBackend *backend, ASTNode_C *node) {
  TypedValue result = {nullptr, INFERRED_UNKNOWN, nullptr};

  if (!node)
    return result;

  switch (node->type) {
  case AST_INT_LITERAL:
    result.value = LLVMConstInt(backend->int_type, node->value.int_value, 1);
    result.type = INFERRED_INT;
    return result;

  case AST_FLOAT_LITERAL:
    result.value = LLVMConstReal(backend->float_type, node->value.float_value);
    result.type = INFERRED_FLOAT;
    return result;

  case AST_BOOL_LITERAL:
    result.value = LLVMConstInt(backend->int_type, node->value.bool_value, 0);
    result.type = INFERRED_BOOL;
    return result;

  case AST_IDENTIFIER: {
    InferredType var_type = get_local_type(backend, node->name);
    LLVMValueRef native = get_local_native(backend, node->name);

    if (var_type != INFERRED_UNKNOWN && native) {
      result.value = LLVMBuildLoad2(
          backend->builder,
          var_type == INFERRED_FLOAT ? backend->float_type : backend->int_type,
          native, node->name);
      result.type = var_type;
    } else {
      // Fall back to boxed
      LLVMValueRef val_ptr = get_local(backend, node->name);
      if (val_ptr) {
        result.boxed = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                      val_ptr, node->name);
        result.value = result.boxed;
      }
    }
    return result;
  }

  case AST_FUNCTION_CALL: {
    // Look up the function
    LLVMValueRef func = LLVMGetNamedFunction(backend->module, node->name);
    if (func) {
      LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
      LLVMTypeRef ret_type = LLVMGetReturnType(func_type);

      // Check if it's a native function (returns i64 directly)
      if (ret_type == backend->int_type) {
        // Build arguments - convert to i64
        int arg_count = node->argument_count;
        LLVMValueRef *args = nullptr;
        if (arg_count > 0) {
          args = static_cast<LLVMValueRef*>(malloc(sizeof(LLVMValueRef) * arg_count));
          for (int i = 0; i < arg_count; i++) {
            TypedValue arg = codegen_typed_expr(backend, node->arguments[i]);
            if (arg.type == INFERRED_INT || arg.type == INFERRED_BOOL) {
              args[i] = arg.value;
            } else if (arg.boxed) {
              // Extract int from boxed value
              args[i] = LLVMBuildExtractValue(backend->builder, arg.boxed, 2,
                                              "arg_int");
            } else {
              args[i] = arg.value;
            }
          }
        }

        result.value = LLVMBuildCall2(backend->builder, func_type, func, args,
                                      arg_count, "call_native");
        result.type = INFERRED_INT;
        if (args)
          free(args);
        return result;
      }
    }

    // Fall back to boxed codegen
    result.boxed = codegen_expression(backend, node);
    result.value = result.boxed;
    return result;
  }

  case AST_BINARY_OP: {
    TypedValue L = codegen_typed_expr(backend, node->left);
    TypedValue R = codegen_typed_expr(backend, node->right);

    // Fast path: both are known integers
    if (L.type == INFERRED_INT && R.type == INFERRED_INT) {
      switch (node->op) {
      case TOKEN_PLUS:
        result.value = LLVMBuildAdd(backend->builder, L.value, R.value, "add");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_MINUS:
        result.value = LLVMBuildSub(backend->builder, L.value, R.value, "sub");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_MULTIPLY:
        result.value = LLVMBuildMul(backend->builder, L.value, R.value, "mul");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_DIVIDE:
        result.value = LLVMBuildSDiv(backend->builder, L.value, R.value, "div");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_LESS:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSLT, L.value, R.value, "lt"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_GREATER:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSGT, L.value, R.value, "gt"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_LESS_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSLE, L.value, R.value, "le"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_GREATER_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSGE, L.value, R.value, "ge"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntEQ, L.value, R.value, "eq"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_NOT_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntNE, L.value, R.value, "ne"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      default:
        break;
      }
    }

    // Fall back to boxed path
    LLVMValueRef L_boxed = box_typed_value(backend, L);
    LLVMValueRef R_boxed = box_typed_value(backend, R);
    result.boxed = codegen_expression(backend, node); // Use existing path
    result.value = result.boxed;
    return result;
  }

  default:
    // Fall back to boxed codegen
    result.boxed = codegen_expression(backend, node);
    result.value = result.boxed;
    return result;
  }
}

LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode_C *node) {
  if (!node)
    return nullptr;

  switch (node->type) {
  case AST_INT_LITERAL:
    return llvm_vm_val_int(backend, node->value.int_value);

  case AST_FLOAT_LITERAL: {
    LLVMValueRef f =
        LLVMConstReal(backend->float_type, node->value.float_value);
    return llvm_build_vm_val_float(backend, f);
  }

  case AST_BOOL_LITERAL:
    return llvm_vm_val_bool(backend, node->value.bool_value);

  case AST_STRING_LITERAL: {
    // Call runtime: vm_alloc_string(vm, "str", len)
    // For now passing nullptr as VM context (dangerous but temp)
    LLVMValueRef const_str = LLVMBuildGlobalStringPtr(
        backend->builder, node->value.string_value, "str_lit");
    int len = strlen(node->value.string_value);

    LLVMValueRef args[] = {LLVMConstNull(backend->ptr_type), // vm (nullptr)
                           const_str,
                           LLVMConstInt(backend->int32_type, len, 0)};

    LLVMValueRef str_obj = LLVMBuildCall2(
        backend->builder, LLVMGlobalGetValueType(backend->func_vm_alloc_string),
        backend->func_vm_alloc_string, args, 3, "alloc_str");
    return llvm_build_vm_val_obj(backend, str_obj);
  }

  case AST_ARRAY_LITERAL: {
    // 1. Allocate Array: vm_allocate_array(vm)
    LLVMValueRef alloc_args[] = {LLVMConstNull(backend->ptr_type)};
    LLVMValueRef arr_obj = LLVMBuildCall2(
        backend->builder,
        LLVMGlobalGetValueType(backend->func_vm_allocate_array),
        backend->func_vm_allocate_array, alloc_args, 1, "alloc_arr");

    // 2. Loop elements and push: vm_array_push_wrapper(vm, arr, val)
    if (node->elements) {
      for (int i = 0; i < node->element_count; i++) {
        LLVMValueRef val = codegen_expression(backend, node->elements[i]);
        LLVMValueRef val_ptr = LLVMBuildAlloca(
            backend->builder, backend->vm_value_type, "arr_lit_val_ptr");
        LLVMBuildStore(backend->builder, val, val_ptr);
        LLVMValueRef val_void = LLVMBuildBitCast(
            backend->builder, val_ptr, backend->ptr_type, "arr_lit_val_void");
        LLVMValueRef push_args[] = {LLVMConstNull(backend->ptr_type), arr_obj,
                                    val_void};
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_array_push),
                       backend->func_vm_array_push, push_args, 3, "");
      }
    }
    return llvm_build_vm_val_obj(backend, arr_obj);
  }

  case AST_OBJECT_LITERAL: {
    // 1. Allocate Object: vm_allocate_object(nullptr)
    LLVMValueRef args[] = {LLVMConstPointerNull(backend->ptr_type)};
    LLVMValueRef obj_ptr =
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_allocate_object),
                       backend->func_vm_allocate_object, args, 1, "alloc_obj");

    // 2. Iterate keys/values and Set
    for (int i = 0; i < node->object_count; i++) {
      char *key_str = node->object_keys[i];
      LLVMValueRef key_global =
          LLVMBuildGlobalStringPtr(backend->builder, key_str, "key_str");

      LLVMValueRef val = codegen_expression(backend, node->object_values[i]);

      LLVMValueRef val_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "obj_lit_val_ptr");
      LLVMBuildStore(backend->builder, val, val_ptr);
      LLVMValueRef val_void = LLVMBuildBitCast(
          backend->builder, val_ptr, backend->ptr_type, "obj_lit_val_void");

      LLVMValueRef set_args[] = {
          LLVMConstPointerNull(backend->ptr_type), // VM*
          obj_ptr,                                 // ObjObject*
          key_global,                              // char* key
          val_void                                 // VMValue* value
      };
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_vm_object_set),
                     backend->func_vm_object_set, set_args, 4, "");
    }

    return llvm_build_vm_val_obj(backend, obj_ptr);
  }

  case AST_ARRAY_ACCESS: {
    // Typed-struct fast path: when the receiver is a bare identifier
    // backed by a typed-struct local AND the index is a literal string
    // matching one of the struct's field names, lower this access to
    // a single getelementptr + load of the field's native i64 (or
    // bool-as-i64). Result is wrapped back into a VMValue with INT/BOOL
    // tag so the surrounding expression evaluator stays oblivious.
    // Falls through to the generic vm_get_element runtime call below
    // when the receiver isn't typed (regular json/array) or the index
    // isn't a known field name.
    //
    // The C-bridge stores the receiver in `node->left` (an AST_IDENTIFIER
    // for `p.x`), with the receiver's name on that child node. `node->name`
    // on ArrayAccess itself is unset by the C++ Parser conversion.
    {
      const char *receiver_name = nullptr;
      if (node->left && node->left->type == AST_IDENTIFIER && node->left->name) {
        receiver_name = node->left->name;
      } else if (node->name) {
        receiver_name = node->name;  // legacy path some callers may use
      }
      if (receiver_name && node->index &&
          node->index->type == AST_STRING_LITERAL &&
          node->index->value.string_value) {
        const char *struct_name = get_local_struct_type(backend, receiver_name);
        if (struct_name) {
          StructTypeEntry *st = find_struct_type(backend, struct_name);
          int idx = struct_type_field_index(st, node->index->value.string_value);
          if (st && idx >= 0) {
            LLVMValueRef alloca_ptr = get_local(backend, receiver_name);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                backend->builder, st->llvm_type, alloca_ptr,
                (unsigned)idx, "struct.field.ptr");
            LLVMValueRef field_val = LLVMBuildLoad2(
                backend->builder, backend->int_type, field_ptr, "struct.field");
            DataType ft = st->field_types[idx];
            if (ft == TYPE_BOOL) {
              LLVMValueRef as_bool = LLVMBuildICmp(
                  backend->builder, LLVMIntNE, field_val,
                  LLVMConstInt(backend->int_type, 0, 0), "field.tobool");
              return llvm_vm_val_bool_val(backend, as_bool);
            }
            return llvm_vm_val_int_val(backend, field_val);
          }
        }
      }
    }

    LLVMValueRef left_val = nullptr;

    // Parser stores first identifier access in node->name, not node->left
    if (node->name) {
      LLVMValueRef val_ptr = get_local(backend, node->name);
      if (val_ptr) {
        left_val = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                  val_ptr, node->name);
      } else {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "'%s' değişkeni dizi/sözlük erişiminde tanımsız",
                 node->name);
        report_codegen_error_with_suggestion(backend, node->line, "hata", msg, node->name,
                             "değişkenin önce tanımlandığından emin olun");
      }
    } else if (node->left) {
      // Nested access uses node->left
      left_val = codegen_expression(backend, node->left);
    }

    LLVMValueRef idx_val = codegen_expression(backend, node->index);

    // Fallback to avoid crash
    if (!left_val)
      left_val = llvm_vm_val_int(backend, 0);
    if (!idx_val)
      idx_val = llvm_vm_val_int(backend, 0);

    // Allocas hoisted to the function entry block — when this expression
    // appears inside a hot inner loop (e.g. BubbleSort `arr[j] > arr[j+1]`),
    // per-iteration LLVMBuildAlloca grows the stack frame on every loop turn
    // and overflows after a few hundred thousand iterations.

    // Check if index is a string literal (object key access)
    // In that case, use generic vm_get_element for object property access
    if (node->index && node->index->type == AST_STRING_LITERAL) {
      // Object property access: obj["key"] - use generic function
      // vm_get_element_ptr takes (VMValue*, VMValue*) and returns VMValue
      LLVMValueRef target_temp = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "target_tmp");
      LLVMBuildStore(backend->builder, left_val, target_temp);
      LLVMValueRef index_temp = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "index_tmp");
      LLVMBuildStore(backend->builder, idx_val, index_temp);

      LLVMValueRef args[] = {target_temp, index_temp};
      return llvm_call_vmvalue_func(backend, backend->func_vm_get_element, args, 2, "obj_element");
    }

    // ============================================================
    // SAFE ELEMENT ACCESS - Use runtime function for all access
    // This handles arrays, strings, and objects correctly with type checking
    // ============================================================

    // Use generic vm_get_element_ptr for all element access
    // This function properly handles: arrays (int index), strings (int index),
    // and objects (string key)
    LLVMValueRef target_temp =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, "target_tmp");
    LLVMBuildStore(backend->builder, left_val, target_temp);
    LLVMValueRef index_temp =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, "index_tmp");
    LLVMBuildStore(backend->builder, idx_val, index_temp);

    LLVMValueRef args[] = {target_temp, index_temp};
    return llvm_call_vmvalue_func(backend, backend->func_vm_get_element, args, 2, "element");
  }

  case AST_INCREMENT: {
    // Typed-int fast path: top-level `int x = ...;` registers x as a native
    // i64 global with no boxed VMValue local. get_local() would return NULL
    // in that case, so check the typed/native side first.
    InferredType vt_inc = get_local_type(backend, node->name);
    LLVMValueRef nat_inc = get_local_native(backend, node->name);
    if (vt_inc == INFERRED_INT && nat_inc) {
      LLVMValueRef old_int = LLVMBuildLoad2(backend->builder, backend->int_type,
                                            nat_inc, node->name);
      LLVMValueRef new_int =
          LLVMBuildAdd(backend->builder, old_int,
                       LLVMConstInt(backend->int_type, 1, 0), "inc");
      LLVMBuildStore(backend->builder, new_int, nat_inc);
      return llvm_vm_val_int_val(backend, old_int); // post-increment
    }

    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      LLVMValueRef old_val = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, val_ptr, node->name);
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, old_val);
      LLVMValueRef new_int =
          LLVMBuildAdd(backend->builder, int_val,
                       LLVMConstInt(backend->int_type, 1, 0), "inc");
      LLVMValueRef new_val = llvm_vm_val_int_val(backend, new_int);
      LLVMBuildStore(backend->builder, new_val, val_ptr);
      return old_val; // Post-increment returns old value
    }
    {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "'%s' değişkeni artırılamaz — tanımlı değil", node->name);
      report_codegen_error_with_suggestion(backend, node->line, "hata", msg, node->name,
                           "kullanmadan önce tanımlayın");
    }
    return llvm_vm_val_int(backend, 0);
  }

  case AST_DECREMENT: {
    InferredType vt_dec = get_local_type(backend, node->name);
    LLVMValueRef nat_dec = get_local_native(backend, node->name);
    if (vt_dec == INFERRED_INT && nat_dec) {
      LLVMValueRef old_int = LLVMBuildLoad2(backend->builder, backend->int_type,
                                            nat_dec, node->name);
      LLVMValueRef new_int =
          LLVMBuildSub(backend->builder, old_int,
                       LLVMConstInt(backend->int_type, 1, 0), "dec");
      LLVMBuildStore(backend->builder, new_int, nat_dec);
      return llvm_vm_val_int_val(backend, old_int);
    }

    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      LLVMValueRef old_val = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, val_ptr, node->name);
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, old_val);
      LLVMValueRef new_int =
          LLVMBuildSub(backend->builder, int_val,
                       LLVMConstInt(backend->int_type, 1, 0), "dec");
      LLVMValueRef new_val = llvm_vm_val_int_val(backend, new_int);
      LLVMBuildStore(backend->builder, new_val, val_ptr);
      return old_val;
    }
    {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "'%s' değişkeni azaltılamaz — tanımlı değil", node->name);
      report_codegen_error_with_suggestion(backend, node->line, "hata", msg, node->name,
                           "kullanmadan önce tanımlayın");
    }
    return llvm_vm_val_int(backend, 0);
  }

  case AST_COMPOUND_ASSIGN: {
    // Typed-int fast path: do unboxed +=/-=/*=//= directly on the native i64.
    InferredType vt_ca = get_local_type(backend, node->name);
    LLVMValueRef nat_ca = get_local_native(backend, node->name);
    if (vt_ca == INFERRED_INT && nat_ca) {
      LLVMValueRef old_int = LLVMBuildLoad2(backend->builder, backend->int_type,
                                            nat_ca, node->name);
      TypedValue rhs_tv = codegen_typed_expr(backend, node->right);
      LLVMValueRef rhs_int;
      if (rhs_tv.type == INFERRED_INT || rhs_tv.type == INFERRED_BOOL) {
        rhs_int = rhs_tv.value;
      } else if (rhs_tv.boxed) {
        rhs_int = llvm_extract_vm_val_int(backend, rhs_tv.boxed);
      } else {
        rhs_int = LLVMConstInt(backend->int_type, 0, 0);
      }
      LLVMValueRef new_int = nullptr;
      switch (node->op) {
      case TOKEN_PLUS_EQUAL:
        new_int = LLVMBuildAdd(backend->builder, old_int, rhs_int, "addeq");
        break;
      case TOKEN_MINUS_EQUAL:
        new_int = LLVMBuildSub(backend->builder, old_int, rhs_int, "subeq");
        break;
      case TOKEN_MULTIPLY_EQUAL:
        new_int = LLVMBuildMul(backend->builder, old_int, rhs_int, "muleq");
        break;
      case TOKEN_DIVIDE_EQUAL:
        new_int = LLVMBuildSDiv(backend->builder, old_int, rhs_int, "diveq");
        break;
      default:
        new_int = old_int; // unsupported op falls through, leave value
        break;
      }
      LLVMBuildStore(backend->builder, new_int, nat_ca);
      return llvm_vm_val_int_val(backend, new_int);
    }

    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (!val_ptr) {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "'%s' değişkeni bileşik atamada tanımsız", node->name);
      report_codegen_error_with_suggestion(backend, node->line, "hata", msg, node->name,
                           "değişkenin önce tanımlandığından emin olun");
      return llvm_vm_val_int(backend, 0);
    }

    LLVMValueRef old_val = LLVMBuildLoad2(
        backend->builder, backend->vm_value_type, val_ptr, node->name);
    LLVMValueRef right_val = codegen_expression(backend, node->right);

    // Perform operation: old op right
    // Reuse binary op logic via runtime call
    LLVMValueRef args[] = {
        LLVMConstPointerNull(backend->ptr_type), old_val, right_val,
        LLVMConstInt(backend->int32_type, node->op, 0),
        LLVMConstPointerNull(backend->ptr_type)}; // res_void not needed for
                                                  // call? NO, wrapper needs it?

    // Use vm_binary_op wrapper or direct call?
    // vm_binary_op(vm, L, R, op, res_out). Allocas hoisted to entry; this
    // path is hit by every comparison / arithmetic op in the boxed codegen,
    // so per-iteration stack growth here causes overflows in inner loops.
    LLVMValueRef res_ptr =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, "res_ptr");
    LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                             backend->ptr_type, "res_void");

    // Bitcast L/R to void*
    LLVMValueRef L_ptr =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr");
    LLVMBuildStore(backend->builder, old_val, L_ptr);
    LLVMValueRef L_void =
        LLVMBuildBitCast(backend->builder, L_ptr, backend->ptr_type, "L_void");

    LLVMValueRef R_ptr =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr");
    LLVMBuildStore(backend->builder, right_val, R_ptr);
    LLVMValueRef R_void =
        LLVMBuildBitCast(backend->builder, R_ptr, backend->ptr_type, "R_void");

    int op_token = node->op;
    switch (op_token) {
    case TOKEN_PLUS_EQUAL:
      op_token = TOKEN_PLUS;
      break;
    case TOKEN_MINUS_EQUAL:
      op_token = TOKEN_MINUS;
      break;
    case TOKEN_MULTIPLY_EQUAL:
      op_token = TOKEN_MULTIPLY;
      break;
    case TOKEN_DIVIDE_EQUAL:
      op_token = TOKEN_DIVIDE;
      break;
    default:
      break;
    }

    LLVMValueRef bin_args[] = {
        LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
        LLVMConstInt(backend->int32_type, op_token, 0), res_void};

    LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                   backend->func_vm_binary_op, bin_args, 5, "");

    LLVMValueRef new_val = LLVMBuildLoad2(
        backend->builder, backend->vm_value_type, res_ptr, "compound_res");
    LLVMBuildStore(backend->builder, new_val, val_ptr);
    return new_val;
  }

  case AST_IDENTIFIER: {
    // Native typed-int local/global: load i64 and box back to VMValue.
    InferredType vt = get_local_type(backend, node->name);
    LLVMValueRef native = get_local_native(backend, node->name);
    if (vt == INFERRED_INT && native) {
      LLVMValueRef ival = LLVMBuildLoad2(backend->builder, backend->int_type,
                                         native, node->name);
      return llvm_vm_val_int_val(backend, ival);
    }

    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      // Load the full VMValue struct
      return LLVMBuildLoad2(backend->builder, backend->vm_value_type, val_ptr,
                            node->name);
    }
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "'%s' değişkeni tanımlanmamış", node->name);
      report_codegen_error_with_suggestion(
          backend, node->line, "hata", msg, node->name,
          "değişken adını yanlış yazmış olabilirsiniz veya henüz "
          "tanımlamadınız");
    }
    return nullptr;
  }

  // TODO: Binary Ops Update
  case AST_BINARY_OP: {
    LLVMValueRef L = codegen_expression(backend, node->left);
    LLVMValueRef R = codegen_expression(backend, node->right);
    if (!L || !R)
      return llvm_vm_val_int(backend, 0);

    if (node->op == TOKEN_AND || node->op == TOKEN_OR) {
      LLVMValueRef l_truthy = llvm_build_is_truthy(backend, L);
      LLVMValueRef r_truthy = llvm_build_is_truthy(backend, R);
      LLVMValueRef bool_res =
          (node->op == TOKEN_AND)
              ? LLVMBuildAnd(backend->builder, l_truthy, r_truthy, "and_bool")
              : LLVMBuildOr(backend->builder, l_truthy, r_truthy, "or_bool");
      return llvm_vm_val_bool_val(backend, bool_res);
    }

    // Extract types for fast path checking
    LLVMValueRef l_type =
        LLVMBuildExtractValue(backend->builder, L, 0, "l_type");
    LLVMValueRef r_type =
        LLVMBuildExtractValue(backend->builder, R, 0, "r_type");

    // Check if both are INT (type == 1)
    LLVMValueRef l_is_int =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                      LLVMConstInt(backend->int32_type, 0, 0), "l_int");  // VM_VAL_INT = 0
    LLVMValueRef r_is_int =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                      LLVMConstInt(backend->int32_type, 0, 0), "r_int");  // VM_VAL_INT = 0
    LLVMValueRef both_int =
        LLVMBuildAnd(backend->builder, l_is_int, r_is_int, "both_int");

    // Check if both are FLOAT (type == 2)
    LLVMValueRef l_is_float =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                      LLVMConstInt(backend->int32_type, 1, 0), "l_float");  // VM_VAL_FLOAT = 1
    LLVMValueRef r_is_float =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                      LLVMConstInt(backend->int32_type, 1, 0), "r_float");  // VM_VAL_FLOAT = 1
    LLVMValueRef both_float =
        LLVMBuildAnd(backend->builder, l_is_float, r_is_float, "both_float");

    LLVMValueRef func = backend->current_function;
    LLVMBasicBlockRef int_block = LLVMAppendBasicBlock(func, "op_int");
    LLVMBasicBlockRef float_block = LLVMAppendBasicBlock(func, "op_float");
    LLVMBasicBlockRef fallback_block =
        LLVMAppendBasicBlock(func, "op_fallback");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "op_merge");

    // Branch: int -> int_block, else check float
    LLVMBasicBlockRef check_float_block =
        LLVMAppendBasicBlock(func, "check_float");
    LLVMBuildCondBr(backend->builder, both_int, int_block, check_float_block);

    // Check float block
    LLVMPositionBuilderAtEnd(backend->builder, check_float_block);
    LLVMBuildCondBr(backend->builder, both_float, float_block, fallback_block);

    // --- Integer Block ---
    // VMValue struct: {i32 type, pad, i64 as}
    LLVMPositionBuilderAtEnd(backend->builder, int_block);
    LLVMValueRef l_val = LLVMBuildExtractValue(backend->builder, L, 2, "l_val");
    LLVMValueRef r_val = LLVMBuildExtractValue(backend->builder, R, 2, "r_val");
    LLVMValueRef int_res = nullptr;
    int is_bool_res = 0;

    switch (node->op) {
    case TOKEN_PLUS:
      int_res = LLVMBuildAdd(backend->builder, l_val, r_val, "add");
      break;
    case TOKEN_MINUS:
      int_res = LLVMBuildSub(backend->builder, l_val, r_val, "sub");
      break;
    case TOKEN_MULTIPLY:
      int_res = LLVMBuildMul(backend->builder, l_val, r_val, "mul");
      break;
    case TOKEN_DIVIDE:
      int_res = LLVMBuildSDiv(backend->builder, l_val, r_val, "div");
      break;
    case TOKEN_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntEQ, l_val, r_val, "eq"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_NOT_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntNE, l_val, r_val, "neq"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_LESS:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSLT, l_val, r_val, "lt"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_GREATER:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSGT, l_val, r_val, "gt"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_LESS_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSLE, l_val, r_val, "le"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_GREATER_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSGE, l_val, r_val, "ge"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_AND:
      // Logical AND: both non-zero -> 1, else 0
      {
        LLVMValueRef l_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, l_val,
                          LLVMConstInt(backend->int_type, 0, 0), "l_nz");
        LLVMValueRef r_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, r_val,
                          LLVMConstInt(backend->int_type, 0, 0), "r_nz");
        LLVMValueRef and_res =
            LLVMBuildAnd(backend->builder, l_nz, r_nz, "and");
        int_res = LLVMBuildZExt(backend->builder, and_res, backend->int_type,
                                "zext_and");
        is_bool_res = 1;
      }
      break;
    case TOKEN_OR:
      // Logical OR: any non-zero -> 1, else 0
      {
        LLVMValueRef l_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, l_val,
                          LLVMConstInt(backend->int_type, 0, 0), "l_nz");
        LLVMValueRef r_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, r_val,
                          LLVMConstInt(backend->int_type, 0, 0), "r_nz");
        LLVMValueRef or_res = LLVMBuildOr(backend->builder, l_nz, r_nz, "or");
        int_res = LLVMBuildZExt(backend->builder, or_res, backend->int_type,
                                "zext_or");
        is_bool_res = 1;
      }
      break;
    default:
      int_res = nullptr;
    }

    LLVMValueRef int_vm_res;
    if (int_res) {
      if (is_bool_res) {
        // Result is BOOL (type 3)
        int_vm_res = llvm_vm_val_bool(backend, 0); // dummy init
        // Manually build struct to avoid constant restrictions if needed,
        // but llvm_vm_val_int_val handles runtime values for INT, need one for
        // BOOL?

        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 2, 0), 0,
                                 "");  // VM_VAL_BOOL = 2
        s = LLVMBuildInsertValue(backend->builder, s, int_res, 2, "");
        int_vm_res = s;
      } else {
        // Result is INT (type 1)
        int_vm_res = llvm_vm_val_int_val(backend, int_res);
      }
      LLVMBuildBr(backend->builder, merge_block);
    } else {
      // Op not supported for fast path
      LLVMBuildBr(backend->builder, fallback_block);
    }
    LLVMBasicBlockRef int_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Float Block (Fast Path for float operations) ---
    LLVMPositionBuilderAtEnd(backend->builder, float_block);
    LLVMValueRef l_float_bits =
        LLVMBuildExtractValue(backend->builder, L, 2, "l_float_bits");
    LLVMValueRef r_float_bits =
        LLVMBuildExtractValue(backend->builder, R, 2, "r_float_bits");
    // Reinterpret i64 bits as double
    LLVMValueRef l_float = LLVMBuildBitCast(backend->builder, l_float_bits,
                                            LLVMDoubleType(), "l_double");
    LLVMValueRef r_float = LLVMBuildBitCast(backend->builder, r_float_bits,
                                            LLVMDoubleType(), "r_double");

    LLVMValueRef float_res = nullptr;
    int float_is_bool = 0;

    switch (node->op) {
    case TOKEN_PLUS:
      float_res = LLVMBuildFAdd(backend->builder, l_float, r_float, "fadd");
      break;
    case TOKEN_MINUS:
      float_res = LLVMBuildFSub(backend->builder, l_float, r_float, "fsub");
      break;
    case TOKEN_MULTIPLY:
      float_res = LLVMBuildFMul(backend->builder, l_float, r_float, "fmul");
      break;
    case TOKEN_DIVIDE:
      float_res = LLVMBuildFDiv(backend->builder, l_float, r_float, "fdiv");
      break;
    case TOKEN_LESS:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOLT, l_float, r_float, "flt");
      float_is_bool = 1;
      break;
    case TOKEN_GREATER:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOGT, l_float, r_float, "fgt");
      float_is_bool = 1;
      break;
    case TOKEN_LESS_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOLE, l_float, r_float, "fle");
      float_is_bool = 1;
      break;
    case TOKEN_GREATER_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOGE, l_float, r_float, "fge");
      float_is_bool = 1;
      break;
    case TOKEN_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOEQ, l_float, r_float, "feq");
      float_is_bool = 1;
      break;
    case TOKEN_NOT_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealONE, l_float, r_float, "fne");
      float_is_bool = 1;
      break;
    default:
      float_res = nullptr;
    }

    LLVMValueRef float_vm_res;
    if (float_res) {
      if (float_is_bool) {
        // Result is BOOL (type 3)
        LLVMValueRef bool_ext = LLVMBuildZExt(backend->builder, float_res,
                                              backend->int_type, "bool_zext");
        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 2, 0), 0,
                                 "");  // VM_VAL_BOOL = 2
        s = LLVMBuildInsertValue(backend->builder, s, bool_ext, 2, "");
        float_vm_res = s;
      } else {
        // Result is FLOAT (type 2) - convert double back to i64 bits
        LLVMValueRef res_bits = LLVMBuildBitCast(backend->builder, float_res,
                                                 backend->int_type, "res_bits");
        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 1, 0), 0,
                                 "");  // VM_VAL_FLOAT = 1
        s = LLVMBuildInsertValue(backend->builder, s, res_bits, 2, "");
        float_vm_res = s;
      }
      LLVMBuildBr(backend->builder, merge_block);
    } else {
      // Op not supported for float fast path
      LLVMBuildBr(backend->builder, fallback_block);
    }
    LLVMBasicBlockRef float_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Fallback Block (Runtime Call) ---
    LLVMPositionBuilderAtEnd(backend->builder, fallback_block);

    LLVMValueRef fallback_res;

    // For TOKEN_PLUS, check if both are strings and use fast concat
    if (node->op == TOKEN_PLUS) {
      // Check if both are STRING (type == 4)
      LLVMValueRef l_is_str =
          LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                        LLVMConstInt(backend->int32_type, 4, 0), "l_str");
      LLVMValueRef r_is_str =
          LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                        LLVMConstInt(backend->int32_type, 4, 0), "r_str");
      LLVMValueRef both_str =
          LLVMBuildAnd(backend->builder, l_is_str, r_is_str, "both_str");

      LLVMBasicBlockRef str_concat_block =
          LLVMAppendBasicBlock(func, "str_concat");
      LLVMBasicBlockRef generic_block =
          LLVMAppendBasicBlock(func, "generic_op");
      LLVMBasicBlockRef fallback_merge =
          LLVMAppendBasicBlock(func, "fallback_merge");

      LLVMBuildCondBr(backend->builder, both_str, str_concat_block,
                      generic_block);

      // --- String Concat Fast Path ---
      LLVMPositionBuilderAtEnd(backend->builder, str_concat_block);
      LLVMValueRef L_ptr_sc =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr_sc");
      LLVMBuildStore(backend->builder, L, L_ptr_sc);
      LLVMValueRef R_ptr_sc =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr_sc");
      LLVMBuildStore(backend->builder, R, R_ptr_sc);
      LLVMValueRef L_void_sc = LLVMBuildBitCast(backend->builder, L_ptr_sc,
                                                backend->ptr_type, "L_void_sc");
      LLVMValueRef R_void_sc = LLVMBuildBitCast(backend->builder, R_ptr_sc,
                                                backend->ptr_type, "R_void_sc");
      LLVMValueRef str_args[] = {L_void_sc, R_void_sc};
      LLVMValueRef str_result = llvm_call_vmvalue_func(
          backend, backend->func_aot_string_concat_fast, str_args, 2, "str_concat_res");
      LLVMBuildBr(backend->builder, fallback_merge);
      LLVMBasicBlockRef str_block_end = LLVMGetInsertBlock(backend->builder);

      // --- Generic Runtime Path ---
      LLVMPositionBuilderAtEnd(backend->builder, generic_block);
      LLVMValueRef L_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr");
      LLVMBuildStore(backend->builder, L, L_ptr);
      LLVMValueRef L_void = LLVMBuildBitCast(backend->builder, L_ptr,
                                             backend->ptr_type, "L_void");

      LLVMValueRef R_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr");
      LLVMBuildStore(backend->builder, R, R_ptr);
      LLVMValueRef R_void = LLVMBuildBitCast(backend->builder, R_ptr,
                                             backend->ptr_type, "R_void");

      LLVMValueRef res_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "res_ptr");
      LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                               backend->ptr_type, "res_void");

      LLVMValueRef args[] = {
          LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
          LLVMConstInt(backend->int32_type, node->op, 0), res_void};

      LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                     backend->func_vm_binary_op, args, 5, "");
      LLVMValueRef generic_res = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, res_ptr, "generic_res");
      LLVMBuildBr(backend->builder, fallback_merge);
      LLVMBasicBlockRef generic_block_end =
          LLVMGetInsertBlock(backend->builder);

      // --- Fallback Merge ---
      LLVMPositionBuilderAtEnd(backend->builder, fallback_merge);
      LLVMValueRef fallback_phi = LLVMBuildPhi(
          backend->builder, backend->vm_value_type, "fallback_phi");
      LLVMValueRef fb_vals[] = {str_result, generic_res};
      LLVMBasicBlockRef fb_blocks[] = {str_block_end, generic_block_end};
      LLVMAddIncoming(fallback_phi, fb_vals, fb_blocks, 2);
      fallback_res = fallback_phi;
    } else {
      // Non-PLUS operations - use generic path
      LLVMValueRef L_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr");
      LLVMBuildStore(backend->builder, L, L_ptr);
      LLVMValueRef L_void = LLVMBuildBitCast(backend->builder, L_ptr,
                                             backend->ptr_type, "L_void");

      LLVMValueRef R_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr");
      LLVMBuildStore(backend->builder, R, R_ptr);
      LLVMValueRef R_void = LLVMBuildBitCast(backend->builder, R_ptr,
                                             backend->ptr_type, "R_void");

      LLVMValueRef res_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "res_ptr");
      LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                               backend->ptr_type, "res_void");

      LLVMValueRef args[] = {
          LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
          LLVMConstInt(backend->int32_type, node->op, 0), res_void};

      LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                     backend->func_vm_binary_op, args, 5, "");
      fallback_res = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                    res_ptr, "fallback_res");
    }

    LLVMBuildBr(backend->builder, merge_block);
    LLVMBasicBlockRef fallback_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Merge Block ---
    LLVMPositionBuilderAtEnd(backend->builder, merge_block);
    LLVMValueRef phi =
        LLVMBuildPhi(backend->builder, backend->vm_value_type, "op_res");

    // Add incoming values based on which paths were valid
    if (int_res && float_res) {
      // Both int and float fast paths valid
      LLVMValueRef incoming_vals[] = {int_vm_res, float_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {int_block_end, float_block_end,
                                             fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 3);
    } else if (int_res) {
      // Only int fast path valid
      LLVMValueRef incoming_vals[] = {int_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {int_block_end, fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    } else if (float_res) {
      // Only float fast path valid
      LLVMValueRef incoming_vals[] = {float_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {float_block_end,
                                             fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    } else {
      // Only fallback was valid
      LLVMAddIncoming(phi, &fallback_res, &fallback_block_end, 1);
    }

    return phi;
  }

  case AST_UNARY_OP: {
    LLVMValueRef operand = codegen_expression(backend, node->left);
    if (!operand)
      return llvm_vm_val_int(backend, 0);

    // Logical NOT
    if (node->op == TOKEN_BANG) {
      LLVMValueRef truthy = llvm_build_is_truthy(backend, operand);
      LLVMValueRef inverted =
          LLVMBuildNot(backend->builder, truthy, "not_truthy");
      return llvm_vm_val_bool_val(backend, inverted);
    }

    // Unary minus for int/float
    if (node->op == TOKEN_MINUS) {
      LLVMValueRef type_val =
          LLVMBuildExtractValue(backend->builder, operand, 0, "unary_type");
      LLVMValueRef is_int = LLVMBuildICmp(
          backend->builder, LLVMIntEQ, type_val,
          LLVMConstInt(backend->int32_type, 0, 0), "unary_is_int");  // VM_VAL_INT = 0
      LLVMValueRef is_float = LLVMBuildICmp(
          backend->builder, LLVMIntEQ, type_val,
          LLVMConstInt(backend->int32_type, 1, 0), "unary_is_float");  // VM_VAL_FLOAT = 1

      LLVMValueRef func = backend->current_function;
      if (!func)
        func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(backend->builder));

      LLVMBasicBlockRef check_float =
          LLVMAppendBasicBlock(func, "unary_check_float");
      LLVMBasicBlockRef int_block = LLVMAppendBasicBlock(func, "unary_int");
      LLVMBasicBlockRef float_block = LLVMAppendBasicBlock(func, "unary_float");
      LLVMBasicBlockRef fallback_block =
          LLVMAppendBasicBlock(func, "unary_fallback");
      LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "unary_merge");

      LLVMBuildCondBr(backend->builder, is_int, int_block, check_float);

      LLVMPositionBuilderAtEnd(backend->builder, check_float);
      LLVMBuildCondBr(backend->builder, is_float, float_block, fallback_block);

      LLVMPositionBuilderAtEnd(backend->builder, int_block);
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, operand);
      LLVMValueRef neg_int = LLVMBuildNeg(backend->builder, int_val, "neg_int");
      LLVMValueRef int_vm = llvm_vm_val_int_val(backend, neg_int);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef int_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, float_block);
      LLVMValueRef float_bits = LLVMBuildExtractValue(backend->builder, operand,
                                                      1, "unary_float_bits");
      LLVMValueRef float_val = LLVMBuildBitCast(
          backend->builder, float_bits, backend->float_type, "unary_float");
      LLVMValueRef neg_float =
          LLVMBuildFNeg(backend->builder, float_val, "neg_float");
      LLVMValueRef float_vm = llvm_build_vm_val_float(backend, neg_float);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef float_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, fallback_block);
      LLVMValueRef fallback_vm = llvm_vm_val_int(backend, 0);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef fallback_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, merge_block);
      LLVMValueRef phi = LLVMBuildPhi(backend->builder, backend->vm_value_type,
                                      "unary_minus_res");
      LLVMValueRef incoming_vals[] = {int_vm, float_vm, fallback_vm};
      LLVMBasicBlockRef incoming_blocks[] = {int_end, float_end, fallback_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 3);
      return phi;
    }

    return llvm_vm_val_int(backend, 0);
  }

  // TODO: Function Call Update
  case AST_FUNCTION_CALL: {
    // Method-call resolution: when the parser saw `<recv>.<name>(args)`
    // it stored `<recv>` in node->receiver and left node->name as the
    // unmangled member name. Decide here whether to dispatch as an
    // import-alias call (`<recv>__<name>`) or a method call
    // (`<name>(<recv>, args...)`). The helper mutates the node in place
    // so the rest of this case stays oblivious to the receiver.
    if (node->receiver) {
      auto func_in_module = [](const char *raw, void *ctx) -> int {
        char prefixed[300];
        snprintf(prefixed, sizeof(prefixed), "t_%s", raw);
        LLVMModuleRef m = static_cast<LLVMModuleRef>(ctx);
        if (LLVMGetNamedFunction(m, prefixed)) return 1;
        if (LLVMGetNamedFunction(m, raw)) return 1;
        return 0;
      };
      resolve_qualified_call(node, func_in_module, backend->module);
    }

    // call(func_name) -> dynamic dispatch
    if (node->name && strcmp(node->name, "call") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      // Alloca to pass by value/pointer as needed by ABI
      LLVMValueRef arg_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "call_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);

      // Load it back? No, wait.
      // aot_call_dynamic signature: VMValue(VMValue) (pass by value usually for
      // small structs, but VMValue is 16 bytes) LLVM ABI for struct passing can
      // be complex. Let's passed by pointer to be safe or check signature. Step
      // 1729 registration: LLVMFunctionType(backend->vm_value_type,
      // call_dyn_params, 1, 0); Params are BY VALUE in LLVM IR if not pointer
      // type. So we pass 'arg' directly.

      LLVMValueRef args[] = {arg};
      return llvm_call_vmvalue_func(backend, backend->func_aot_call_dynamic, args, 1, "call_res");
    }

    // Check for builtin "print" function
    if (node->name && strcmp(node->name, "print") == 0) {
      // Generate print calls for each argument on the same line
      for (int i = 0; i < node->argument_count; i++) {
        ASTNode_C *arg = node->arguments[i];

        // Add space between arguments (except first)
        if (i > 0) {
          LLVMValueRef space_fmt =
              LLVMBuildGlobalStringPtr(backend->builder, " ", "space_fmt");
          LLVMValueRef space_args[] = {space_fmt};
          LLVMBuildCall2(backend->builder,
                         LLVMGlobalGetValueType(backend->func_printf),
                         backend->func_printf, space_args, 1, "");
        }

        // Special case: string literal - use printf directly (no newline)
        if (arg->type == AST_STRING_LITERAL) {
          LLVMValueRef fmt =
              LLVMBuildGlobalStringPtr(backend->builder, "%s", "fmt_str");
          LLVMValueRef str = LLVMBuildGlobalStringPtr(
              backend->builder, arg->value.string_value, "str_arg");
          LLVMValueRef printf_args[] = {fmt, str};
          LLVMBuildCall2(backend->builder,
                         LLVMGlobalGetValueType(backend->func_printf),
                         backend->func_printf, printf_args, 2, "");
        } else if (
            (arg->type == AST_IDENTIFIER && arg->name &&
             get_local_struct_type(backend, arg->name)) ||
            (arg->type == AST_FUNCTION_CALL && arg->name && [&]() {
               for (int j = 0; j < backend->function_count; j++) {
                 if (strcmp(backend->functions[j].name, arg->name) == 0) {
                   return backend->functions[j].return_struct_name != nullptr;
                 }
               }
               return false;
             }())) {
          // Typed-struct print: emit `Name { f1: <int>, f2: <int>, ... }`
          // directly via printf calls, GEP-loading each field's i64
          // payload. Avoids feeding a raw struct alloca through the
          // VMValue print pipeline (which would mis-read the bytes).
          // Trivially-unboxable (int/bool) is the only path PR3-PR5 takes,
          // so every field is an i64 here. bool prints as 0/1 — same as
          // the boxed VMValue print path treats it.
          //
          // Two sources for the struct alloca:
          //   - identifier:    the local's pinned alloca (no allocation)
          //   - function call: alloca a temp of the callee's return
          //                    struct, pin the hint, evaluate the call
          //                    (writes into our temp), then format from
          //                    the temp.
          const char *struct_name = nullptr;
          StructTypeEntry *st = nullptr;
          LLVMValueRef alloca = nullptr;
          if (arg->type == AST_IDENTIFIER) {
            struct_name = get_local_struct_type(backend, arg->name);
            st = find_struct_type(backend, struct_name);
            alloca = get_local(backend, arg->name);
          } else {
            for (int j = 0; j < backend->function_count; j++) {
              if (strcmp(backend->functions[j].name, arg->name) == 0) {
                struct_name = backend->functions[j].return_struct_name;
                break;
              }
            }
            st = find_struct_type(backend, struct_name);
            if (st) {
              alloca = llvm_build_alloca_at_entry(
                  backend, st->llvm_type, "print.struct.call");
              LLVMBuildStore(backend->builder,
                             LLVMConstNull(st->llvm_type), alloca);
              backend->pending_struct_result_ptr = alloca;
              backend->pending_struct_result_name = st->name;
              (void)codegen_expression(backend, arg);
              backend->pending_struct_result_ptr = nullptr;
              backend->pending_struct_result_name = nullptr;
            }
          }
          if (st && alloca) {
            char hdr[256];
            snprintf(hdr, sizeof(hdr), "%s { ", st->name);
            LLVMValueRef hdr_str =
                LLVMBuildGlobalStringPtr(backend->builder, hdr,
                                          "struct.print.hdr");
            LLVMValueRef hdr_args[] = {hdr_str};
            LLVMBuildCall2(backend->builder,
                           LLVMGlobalGetValueType(backend->func_printf),
                           backend->func_printf, hdr_args, 1, "");
            LLVMValueRef sep_fmt = LLVMBuildGlobalStringPtr(
                backend->builder, ", ", "struct.print.sep");
            for (int f = 0; f < st->field_count; f++) {
              if (f > 0) {
                LLVMValueRef sep_args[] = {sep_fmt};
                LLVMBuildCall2(
                    backend->builder,
                    LLVMGlobalGetValueType(backend->func_printf),
                    backend->func_printf, sep_args, 1, "");
              }
              char fname_lit[256];
              snprintf(fname_lit, sizeof(fname_lit), "%s: %%lld",
                       st->field_names[f]);
              LLVMValueRef fname_fmt = LLVMBuildGlobalStringPtr(
                  backend->builder, fname_lit, "struct.print.field");
              LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                  backend->builder, st->llvm_type, alloca,
                  (unsigned)f, "struct.print.gep");
              LLVMValueRef field_val = LLVMBuildLoad2(
                  backend->builder, backend->int_type, field_ptr,
                  "struct.print.fld");
              LLVMValueRef pf_args[] = {fname_fmt, field_val};
              LLVMBuildCall2(backend->builder,
                             LLVMGlobalGetValueType(backend->func_printf),
                             backend->func_printf, pf_args, 2, "");
            }
            LLVMValueRef tail_str = LLVMBuildGlobalStringPtr(
                backend->builder, " }", "struct.print.tail");
            LLVMValueRef tail_args[] = {tail_str};
            LLVMBuildCall2(backend->builder,
                           LLVMGlobalGetValueType(backend->func_printf),
                           backend->func_printf, tail_args, 1, "");
            continue;
          }
          // Fall through to the generic VMValue path if for any reason
          // the struct entry wasn't resolvable (defensive — well-typed
          // code shouldn't hit this).
        }
        if (arg->type != AST_STRING_LITERAL) {
          // Other types: use print_value_inline(VMValue*) - no newline
          // Need to pass pointer for ABI compatibility
          LLVMValueRef arg_val = codegen_expression(backend, arg);
          if (arg_val) {
            // Alloca, store, then pass pointer (entry-hoisted to avoid
            // per-iteration stack growth in print-in-loop programs).
            LLVMValueRef temp = llvm_build_alloca_at_entry(
                backend, backend->vm_value_type, "print_temp");
            LLVMBuildStore(backend->builder, arg_val, temp);
            LLVMValueRef args[] = {temp};
            LLVMBuildCall2(
                backend->builder,
                LLVMGlobalGetValueType(backend->func_print_value_inline),
                backend->func_print_value_inline, args, 1, "");
          }
        }
      }
      // Print newline at end of print statement
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_print_newline),
                     backend->func_print_newline, nullptr, 0, "");
      return llvm_vm_val_int(backend, 0); // void return
    }

    // ====== Builtin Functions ======

    // toString(value) -> string (for print, returns char*)
    if (node->name && strcmp(node->name, "toString") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "to_str_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "to_str_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_to_string, args, 1, "to_str");
    }

    // clock_ms() -> float
    if (node->name && strcmp(node->name, "clock_ms") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_clock_ms, nullptr, 0, "clock_res");
    }

    // toInt(value) -> int
    if (node->name && strcmp(node->name, "toInt") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "to_int_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "to_int_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_int),
          backend->func_aot_to_int, args, 1, "to_int");
      return llvm_vm_val_int_val(backend, result);
    }

    // toJson(value) -> String
    if (node->name && strcmp(node->name, "toJson") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "to_json_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "to_json_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_to_json, args, 1, "to_json");
    }

    // toFloat(value) -> float
    if (node->name && strcmp(node->name, "toFloat") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "to_float_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "to_float_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_float),
          backend->func_aot_to_float, args, 1, "to_float");
      return llvm_build_vm_val_float(backend, result);
    }

    // len(value) -> int
    if (node->name && strcmp(node->name, "len") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "len_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "len_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_len),
          backend->func_aot_len, args, 1, "len_result");
      return llvm_vm_val_int_val(backend, result);
    }

    // length(value) -> int (alias for len)
    if (node->name && strcmp(node->name, "length") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      if (!arg) return llvm_vm_val_int(backend, 0);
      LLVMValueRef arg_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "len_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "len_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_len),
          backend->func_aot_len, args, 1, "len_result");
      return llvm_vm_val_int_val(backend, result);
    }

    // push(array, value) - pointer ABI
    if (node->name && strcmp(node->name, "push") == 0 &&
        node->argument_count >= 2) {
      // Heap-promote escape path (Plan 04 v2): if arg[1] is a typed-struct
      // identifier OR a struct-returning call, lift the struct contents to
      // a heap ObjStruct first. The boxed VMValue we hand to aot_array_push
      // then carries an Obj* whose payload survives caller scope cleanup.
      // Without this, the existing fallback below would push a "zero
      // VMValue" placeholder and the array would silently lose the data —
      // exactly what Plan 04 PR1..PR6 deferred to this PR.
      ASTNode_C *val_arg = node->arguments[1];
      LLVMValueRef val = nullptr;
      if (val_arg && val_arg->type == AST_IDENTIFIER && val_arg->name) {
        const char *src_struct = get_local_struct_type(backend, val_arg->name);
        LLVMValueRef src_alloca = get_local(backend, val_arg->name);
        if (src_struct && src_alloca) {
          StructTypeEntry *st = find_struct_type(backend, src_struct);
          if (st && struct_is_trivially_unboxable(st)) {
            LLVMValueRef name_str = LLVMBuildGlobalStringPtr(
                backend->builder, st->name, "struct.type.name");
            LLVMValueRef field_count = LLVMConstInt(
                backend->int32_type, (unsigned)st->field_count, 0);
            LLVMValueRef alloc_args[] = {name_str, field_count, src_alloca};
            val = llvm_call_vmvalue_func(
                backend, backend->func_aot_struct_alloc_from_fields,
                alloc_args, 3, "push.struct.heap");
          }
        }
      } else if (val_arg && val_arg->type == AST_FUNCTION_CALL &&
                 val_arg->name) {
        const char *ret_struct = nullptr;
        for (int j = 0; j < backend->function_count; j++) {
          if (strcmp(backend->functions[j].name, val_arg->name) == 0) {
            ret_struct = backend->functions[j].return_struct_name;
            break;
          }
        }
        if (ret_struct) {
          StructTypeEntry *st = find_struct_type(backend, ret_struct);
          if (st && struct_is_trivially_unboxable(st)) {
            LLVMValueRef tmp = llvm_build_alloca_at_entry(
                backend, st->llvm_type, "push.struct.call.tmp");
            LLVMBuildStore(backend->builder,
                           LLVMConstNull(st->llvm_type), tmp);
            backend->pending_struct_result_ptr = tmp;
            backend->pending_struct_result_name = st->name;
            (void)codegen_expression(backend, val_arg);
            backend->pending_struct_result_ptr = nullptr;
            backend->pending_struct_result_name = nullptr;
            LLVMValueRef name_str = LLVMBuildGlobalStringPtr(
                backend->builder, st->name, "struct.type.name");
            LLVMValueRef field_count = LLVMConstInt(
                backend->int32_type, (unsigned)st->field_count, 0);
            LLVMValueRef alloc_args[] = {name_str, field_count, tmp};
            val = llvm_call_vmvalue_func(
                backend, backend->func_aot_struct_alloc_from_fields,
                alloc_args, 3, "push.struct.call.heap");
          }
        }
      }
      LLVMValueRef arr = codegen_expression(backend, node->arguments[0]);
      if (!val) {
        val = codegen_expression(backend, val_arg);
      }
      if (!arr || !val) return llvm_vm_val_int(backend, 0);

      // Allocate temps and store values for pointer ABI. Hoist to entry so a
      // tight `for(i=0..N) push(arr, i)` loop doesn't grow the stack frame
      // by 16 bytes per iteration.
      LLVMValueRef arr_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "push_arr_ptr");
      LLVMBuildStore(backend->builder, arr, arr_ptr);
      LLVMValueRef val_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "push_val_ptr");
      LLVMBuildStore(backend->builder, val, val_ptr);

      LLVMValueRef args[] = {arr_ptr, val_ptr};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_array_push),
                     backend->func_aot_array_push, args, 2, "");
      return llvm_vm_val_int(backend, 0);
    }

    // pop(array) -> value
    if (node->name && strcmp(node->name, "pop") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arr = codegen_expression(backend, node->arguments[0]);
      if (!arr) return llvm_vm_val_int(backend, 0);
      LLVMValueRef args[] = {arr};
      return llvm_call_vmvalue_func(backend, backend->func_aot_array_pop, args, 1, "pop_result");
    }

    // input() -> String
    if (node->name && strcmp(node->name, "env") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_env, args, 1, "env_res");
    }
    if (node->name && strcmp(node->name, "arena_save") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_arena_save,
                                    nullptr, 0, "arena_save_res");
    }
    if (node->name && strcmp(node->name, "now_iso8601") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_now_iso8601,
                                    nullptr, 0, "now_iso");
    }
    if (node->name && strcmp(node->name, "format_iso8601") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_format_iso8601,
                                    args, 1, "fmt_iso");
    }
    if (node->name && strcmp(node->name, "parse_iso8601") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_parse_iso8601,
                                    args, 1, "parse_iso");
    }
    // regex builtins
    if (node->name && strcmp(node->name, "regex_match") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_regex_match,
                                    args, 2, "rxm");
    }
    if (node->name && strcmp(node->name, "regex_search") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_regex_search,
                                    args, 2, "rxs");
    }
    if (node->name && strcmp(node->name, "regex_capture") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_regex_capture,
                                    args, 2, "rxc");
    }
    if (node->name && strcmp(node->name, "regex_replace") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_regex_replace,
                                    args, 3, "rxr");
    }
    if (node->name && strcmp(node->name, "weekday") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_weekday,
                                    args, 1, "wday");
    }
    if (node->name && strcmp(node->name, "date_add_seconds") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_date_add_seconds,
                                    args, 2, "date_add");
    }
    if (node->name && strcmp(node->name, "file_glob") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_file_glob,
                                    args, 1, "glob");
    }
    if (node->name && strcmp(node->name, "csv_parse") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_csv_parse,
                                    args, 1, "csv_p");
    }
    if (node->name && strcmp(node->name, "csv_emit") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_csv_emit,
                                    args, 1, "csv_e");
    }
    if (node->name && strcmp(node->name, "keys") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_keys,
                                    args, 1, "keys");
    }
    // http_request(method, url, body) — outbound HTTP/1.0 client.
    if (node->name && strcmp(node->name, "http_request") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {
          codegen_expression(backend, node->arguments[0]),
          codegen_expression(backend, node->arguments[1]),
          codegen_expression(backend, node->arguments[2])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_http_request,
                                    args, 3, "http_req");
    }
    if (node->name && strcmp(node->name, "arena_restore") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_arena_restore,
                                    args, 1, "arena_restore_res");
    }
    if (node->name && strcmp(node->name, "input") == 0) {
      if (!backend->func_aot_input)
        fprintf(stderr, "Fatal: func_aot_input is nullptr\n");
      return llvm_call_vmvalue_func(backend, backend->func_aot_input, nullptr, 0, "input_res");
    }

    // trim(str) -> String
    if (node->name && strcmp(node->name, "trim") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "trim_arg");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "trim_arg_ptr");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_trim, args, 1, "trim_res");
    }

    // replace(str, old, new) -> String
    if (node->name && strcmp(node->name, "replace") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef str = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef oldVal = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef newVal = codegen_expression(backend, node->arguments[2]);
      LLVMValueRef str_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "replace_str_ptr");
      LLVMBuildStore(backend->builder, str, str_ptr);
      LLVMValueRef old_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "replace_old_ptr");
      LLVMBuildStore(backend->builder, oldVal, old_ptr);
      LLVMValueRef new_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "replace_new_ptr");
      LLVMBuildStore(backend->builder, newVal, new_ptr);
      LLVMValueRef str_void = LLVMBuildBitCast(
          backend->builder, str_ptr, backend->ptr_type, "replace_str_void");
      LLVMValueRef old_void = LLVMBuildBitCast(
          backend->builder, old_ptr, backend->ptr_type, "replace_old_void");
      LLVMValueRef new_void = LLVMBuildBitCast(
          backend->builder, new_ptr, backend->ptr_type, "replace_new_void");
      LLVMValueRef args[] = {str_void, old_void, new_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_replace, args, 3, "replace_res");
    }

    // split(str, del) -> Array
    if (node->name && strcmp(node->name, "split") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef str = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef del = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef str_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "split_str_ptr");
      LLVMBuildStore(backend->builder, str, str_ptr);
      LLVMValueRef del_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "split_del_ptr");
      LLVMBuildStore(backend->builder, del, del_ptr);
      LLVMValueRef str_void = LLVMBuildBitCast(
          backend->builder, str_ptr, backend->ptr_type, "split_str_void");
      LLVMValueRef del_void = LLVMBuildBitCast(
          backend->builder, del_ptr, backend->ptr_type, "split_del_void");
      LLVMValueRef args[] = {str_void, del_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_split, args, 2, "split_res");
    }

    if (strcmp(node->name, "read_file") == 0) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "read_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "read_arg_void");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_read_file, args, 1, "read_res");
    }
    if (strcmp(node->name, "write_file") == 0) {
      LLVMValueRef p = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef c = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef p_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "write_path_ptr");
      LLVMBuildStore(backend->builder, p, p_ptr);
      LLVMValueRef c_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "write_content_ptr");
      LLVMBuildStore(backend->builder, c, c_ptr);
      LLVMValueRef p_void = LLVMBuildBitCast(
          backend->builder, p_ptr, backend->ptr_type, "write_path_void");
      LLVMValueRef c_void = LLVMBuildBitCast(
          backend->builder, c_ptr, backend->ptr_type, "write_content_void");
      LLVMValueRef args[] = {p_void, c_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_write_file, args, 2, "write_res");
    }
    if (strcmp(node->name, "append_file") == 0) {
      LLVMValueRef p = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef c = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef p_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "append_path_ptr");
      LLVMBuildStore(backend->builder, p, p_ptr);
      LLVMValueRef c_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "append_content_ptr");
      LLVMBuildStore(backend->builder, c, c_ptr);
      LLVMValueRef p_void = LLVMBuildBitCast(
          backend->builder, p_ptr, backend->ptr_type, "append_path_void");
      LLVMValueRef c_void = LLVMBuildBitCast(
          backend->builder, c_ptr, backend->ptr_type, "append_content_void");
      LLVMValueRef args[] = {p_void, c_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_append_file, args, 2, "append_res");
    }
    if (strcmp(node->name, "file_exists") == 0) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "exists_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "exists_arg_void");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_file_exists, args, 1, "exists_res");
    }
    if (strcmp(node->name, "sha256") == 0) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "sha256_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "sha256_arg_void");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_sha256, args, 1, "sha256_res");
    }

    if (strcmp(node->name, "db_open") == 0) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_open_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "db_open_arg_void");
      LLVMValueRef args[] = {arg_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_open, args, 1, "db_open_res");
    }

    if (strcmp(node->name, "db_close") == 0) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_close_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "db_close_arg_void");
      LLVMValueRef args[] = {arg_void};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_db_close),
                     backend->func_aot_db_close, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    if (strcmp(node->name, "db_execute") == 0) {
      LLVMValueRef db = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef sql = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef db_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_exec_db_ptr");
      LLVMBuildStore(backend->builder, db, db_ptr);
      LLVMValueRef sql_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_exec_sql_ptr");
      LLVMBuildStore(backend->builder, sql, sql_ptr);
      LLVMValueRef db_void = LLVMBuildBitCast(
          backend->builder, db_ptr, backend->ptr_type, "db_exec_db_void");
      LLVMValueRef sql_void = LLVMBuildBitCast(
          backend->builder, sql_ptr, backend->ptr_type, "db_exec_sql_void");
      LLVMValueRef args[] = {db_void, sql_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_execute, args, 2, "db_exec_res");
    }

    if (strcmp(node->name, "db_query") == 0) {
      LLVMValueRef db = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef sql = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef db_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_query_db_ptr");
      LLVMBuildStore(backend->builder, db, db_ptr);
      LLVMValueRef sql_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "db_query_sql_ptr");
      LLVMBuildStore(backend->builder, sql, sql_ptr);
      LLVMValueRef db_void = LLVMBuildBitCast(
          backend->builder, db_ptr, backend->ptr_type, "db_query_db_void");
      LLVMValueRef sql_void = LLVMBuildBitCast(
          backend->builder, sql_ptr, backend->ptr_type, "db_query_sql_void");
      LLVMValueRef args[] = {db_void, sql_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_query, args, 2, "db_query_res");
    }

    // Socket Functions
    if (strcmp(node->name, "socket_server") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_server, args, 2, "sock_fd");
    }
    if (strcmp(node->name, "socket_client") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_client, args, 2, "sock_fd");
    }
    if (strcmp(node->name, "socket_accept") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_accept, args, 1, "client_fd");
    }
    if (strcmp(node->name, "socket_send") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_send, args, 2, "bytes_sent");
    }
    if (strcmp(node->name, "socket_receive") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_receive, args, 2, "recv_data");
    }
    if (strcmp(node->name, "socket_close") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_socket_close,
                                    args, 1, "sock_close_res");
    }
    if (strcmp(node->name, "socket_set_nonblocking") == 0 &&
        node->argument_count == 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(
          backend, backend->func_aot_socket_set_nonblocking, args, 1,
          "sock_nonblock_res");
    }
    if (strcmp(node->name, "socket_poll") == 0 &&
        node->argument_count == 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(
          backend, backend->func_aot_socket_poll, args, 2, "sock_poll_res");
    }
    // TLS server primitives. Same dispatch shape as socket_*; the
    // builtin names match what `lib/wings_tls.tpr` calls.
    if (strcmp(node->name, "tls_init") == 0 &&
        node->argument_count == 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_init,
                                    args, 2, "tls_ctx");
    }
    if (strcmp(node->name, "tls_accept") == 0 &&
        node->argument_count == 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_accept,
                                    args, 2, "tls_ssl");
    }
    if (strcmp(node->name, "tls_recv") == 0 &&
        node->argument_count == 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_recv,
                                    args, 2, "tls_recv_data");
    }
    if (strcmp(node->name, "tls_send") == 0 &&
        node->argument_count == 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_send,
                                    args, 2, "tls_send_n");
    }
    if (strcmp(node->name, "tls_close") == 0 &&
        node->argument_count == 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_close,
                                    args, 1, "tls_close_res");
    }
    if (strcmp(node->name, "tls_ctx_free") == 0 &&
        node->argument_count == 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_tls_ctx_free,
                                    args, 1, "tls_ctx_free_res");
    }

    // ====== Threading Functions ======
    // thread_create(func_name, arg) -> thread_id
    //
    // First arg is an IDENTIFIER (the user function's name in source).
    // User-defined functions are emitted with a `t_` prefix in the
    // module so lookup must mirror the dlsym path in
    // `aot_call_dynamic`: try the prefixed name first, fall back to
    // the literal name (covers `main` and intrinsics that don't get
    // prefixed). Without the fallback, `thread_create(my_worker, x)`
    // silently passed a NULL function pointer and the worker thread
    // would segfault on first dispatch.
    if (strcmp(node->name, "thread_create") == 0 && node->argument_count >= 2) {
      LLVMValueRef func_ptr = nullptr;
      if (node->arguments[0]->type == AST_IDENTIFIER) {
        const char *raw = node->arguments[0]->name;
        char prefixed[256];
        snprintf(prefixed, sizeof(prefixed), "t_%s", raw);
        LLVMValueRef func = LLVMGetNamedFunction(backend->module, prefixed);
        if (!func) func = LLVMGetNamedFunction(backend->module, raw);
        if (func) func_ptr = func;
      }
      if (!func_ptr) {
        func_ptr = LLVMConstPointerNull(backend->ptr_type);
      }
      LLVMValueRef arg = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef args[] = {func_ptr, arg};
      return llvm_call_vmvalue_func(backend, backend->func_aot_thread_create, args, 2, "thread_id");
    }

    // thread_join(thread_id) — VMValue-returning since the ABI fix.
    if (strcmp(node->name, "thread_join") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_thread_join,
                                    args, 1, "thread_join_res");
    }

    // thread_detach(thread_id) — same ABI as thread_join.
    if (strcmp(node->name, "thread_detach") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_thread_detach,
                                    args, 1, "thread_detach_res");
    }

    // mutex_create() -> mutex_ptr
    if (strcmp(node->name, "mutex_create") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_mutex_create, nullptr, 0, "mutex_ptr");
    }

    // mutex_lock / unlock / destroy — VMValue-returning since the ABI fix.
    if (strcmp(node->name, "mutex_lock") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_mutex_lock,
                                    args, 1, "mutex_lock_res");
    }
    if (strcmp(node->name, "mutex_unlock") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_mutex_unlock,
                                    args, 1, "mutex_unlock_res");
    }
    if (strcmp(node->name, "mutex_destroy") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_mutex_destroy,
                                    args, 1, "mutex_destroy_res");
    }

    // ====== HTTP Functions ======
    // http_parse_request(raw) -> object
    if (strcmp(node->name, "http_parse_request") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_http_parse_request, args, 1, "parsed_req");
    }

    // http_create_response(status, content_type, body[, headers[, keep]]) -> string
    // 3-arg: status + ct + body. 4-arg adds custom headers map.
    // 5-arg further passes a keep-alive flag (1 = "keep-alive", 0 = "close")
    // — required by the keep-alive request loop in lib/wings.tpr / router.tpr.
    if (strcmp(node->name, "http_create_response") == 0 &&
        node->argument_count >= 5) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2]),
                             codegen_expression(backend, node->arguments[3]),
                             codegen_expression(backend, node->arguments[4])};
      return llvm_call_vmvalue_func(backend,
                                    backend->func_aot_http_create_response_keepalive,
                                    args, 5, "http_resp_keep");
    }
    if (strcmp(node->name, "http_create_response") == 0 &&
        node->argument_count >= 4) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2]),
                             codegen_expression(backend, node->arguments[3])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_http_create_response_full, args, 4, "http_resp_full");
    }
    if (strcmp(node->name, "http_create_response") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_http_create_response, args, 3, "http_resp");
    }

    // http_should_keepalive(parsed_request) -> int (0/1)
    if (strcmp(node->name, "http_should_keepalive") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend,
                                    backend->func_aot_http_should_keepalive,
                                    args, 1, "should_keepalive");
    }

    // http_recv_request(client_fd, max_bytes) -> str
    if (strcmp(node->name, "http_recv_request") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend,
                                    backend->func_aot_http_recv_request,
                                    args, 2, "http_recv_req");
    }

    // http_status_text(status) -> string
    if (strcmp(node->name, "http_status_text") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_http_status_text, args, 1, "status_text");
    }

    // wings_build_response(result, default_headers, keep) -> wire string
    // Fused replacement for the Tulpar-side _wings_build_response —
    // skips three function-call hops per request and folds the envelope
    // check + toJson + framing into one native call.
    if (strcmp(node->name, "wings_build_response") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return llvm_call_vmvalue_func(backend,
                                    backend->func_aot_wings_build_response,
                                    args, 3, "wings_build_resp");
    }

    // wings_find_route(routes, method, path) -> {"index", "params"}
    // Native replacement for the Tulpar `_find_route_with_params`
    // serve-path lookup. Saves N json[get]s per route iteration on
    // every request.
    if (strcmp(node->name, "wings_find_route") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return llvm_call_vmvalue_func(backend,
                                    backend->func_aot_wings_find_route,
                                    args, 3, "wings_find_route");
    }

    // string_pin(s) -> str (permanent copy outside the per-request arena)
    if (strcmp(node->name, "string_pin") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_string_pin,
                                    args, 1, "string_pin");
    }

    // path_match(pattern, path) -> {matched, params}
    if (strcmp(node->name, "path_match") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_path_match, args, 2, "path_match");
    }

    // parse_query(str) -> object
    if (strcmp(node->name, "parse_query") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_parse_query, args, 1, "parse_query");
    }

    // parse_cookies(str) -> object
    if (strcmp(node->name, "parse_cookies") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_parse_cookies, args, 1, "parse_cookies");
    }

    // exit(code) -> noreturn
    // Extract the i64 from VMValue, truncate to i32, call libc-backed
    // aot_exit_i32. Placeholder VMValue return keeps surrounding IR
    // well-typed; control never returns past the call.
    if (strcmp(node->name, "exit") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef code_vm = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef code_i64 = llvm_extract_vm_val_int(backend, code_vm);
      LLVMValueRef code_i32 = LLVMBuildTrunc(backend->builder, code_i64,
                                             backend->int32_type, "exit_i32");
      LLVMValueRef args[] = {code_i32};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_exit),
                     backend->func_aot_exit, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

// ====== Math Functions - Single Param ======
#define MATH1_FUNC(func_name, field)                                           \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef v = codegen_expression(backend, node->arguments[0]);          \
    LLVMValueRef v_ptr = LLVMBuildAlloca(                                      \
        backend->builder, backend->vm_value_type, func_name "_arg_ptr");       \
    LLVMBuildStore(backend->builder, v, v_ptr);                                \
    LLVMValueRef v_void = LLVMBuildBitCast(                                    \
        backend->builder, v_ptr, backend->ptr_type, func_name "_arg_void");    \
    LLVMValueRef m1_args[] = {v_void};                                         \
    return llvm_call_vmvalue_func(backend, backend->field, m1_args, 1,         \
                                  func_name "_res");                           \
  }

    // abs(v) -> passed by pointer with result pointer
    if (strcmp(node->name, "abs") == 0 && node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);

      LLVMValueRef arg_temp =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "abs_arg");
      LLVMBuildStore(backend->builder, arg, arg_temp);

      LLVMValueRef res_temp = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "abs_res_slot");

      LLVMValueRef args[] = {res_temp, arg_temp};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_math_abs),
                     backend->func_aot_math_abs, args, 2, "");

      return LLVMBuildLoad2(backend->builder, backend->vm_value_type, res_temp,
                            "abs_res");
    }

    MATH1_FUNC("sqrt", func_aot_math_sqrt)
    MATH1_FUNC("floor", func_aot_math_floor)
    MATH1_FUNC("ceil", func_aot_math_ceil)
    MATH1_FUNC("round", func_aot_math_round)
    MATH1_FUNC("sin", func_aot_math_sin)
    MATH1_FUNC("cos", func_aot_math_cos)
    MATH1_FUNC("tan", func_aot_math_tan)
    MATH1_FUNC("asin", func_aot_math_asin)
    MATH1_FUNC("acos", func_aot_math_acos)
    MATH1_FUNC("atan", func_aot_math_atan)
    MATH1_FUNC("exp", func_aot_math_exp)
    MATH1_FUNC("log", func_aot_math_log)
    MATH1_FUNC("log10", func_aot_math_log10)
    MATH1_FUNC("log2", func_aot_math_log2)
    MATH1_FUNC("sinh", func_aot_math_sinh)
    MATH1_FUNC("cosh", func_aot_math_cosh)
    MATH1_FUNC("tanh", func_aot_math_tanh)
    MATH1_FUNC("cbrt", func_aot_math_cbrt)
    MATH1_FUNC("trunc", func_aot_math_trunc)

#undef MATH1_FUNC

// ====== Math Functions - Two Params ======
#define MATH2_FUNC(func_name, field)                                           \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 2) {       \
    LLVMValueRef v1 = codegen_expression(backend, node->arguments[0]);         \
    LLVMValueRef v2 = codegen_expression(backend, node->arguments[1]);         \
    LLVMValueRef v1_ptr = LLVMBuildAlloca(                                     \
        backend->builder, backend->vm_value_type, func_name "_arg1_ptr");      \
    LLVMBuildStore(backend->builder, v1, v1_ptr);                              \
    LLVMValueRef v2_ptr = LLVMBuildAlloca(                                     \
        backend->builder, backend->vm_value_type, func_name "_arg2_ptr");      \
    LLVMBuildStore(backend->builder, v2, v2_ptr);                              \
    LLVMValueRef v1_void = LLVMBuildBitCast(                                   \
        backend->builder, v1_ptr, backend->ptr_type, func_name "_arg1_void");  \
    LLVMValueRef v2_void = LLVMBuildBitCast(                                   \
        backend->builder, v2_ptr, backend->ptr_type, func_name "_arg2_void");  \
    LLVMValueRef m2_args[] = {v1_void, v2_void};                               \
    return llvm_call_vmvalue_func(backend, backend->field, m2_args, 2,         \
                                  func_name "_res");                           \
  }

    MATH2_FUNC("pow", func_aot_math_pow)
    MATH2_FUNC("atan2", func_aot_math_atan2)
    MATH2_FUNC("hypot", func_aot_math_hypot)
    MATH2_FUNC("fmod", func_aot_math_fmod)
    MATH2_FUNC("mod", func_aot_math_mod)
    MATH2_FUNC("min", func_aot_math_min)
    MATH2_FUNC("max", func_aot_math_max)
    MATH2_FUNC("randint", func_aot_math_randint)

#undef MATH2_FUNC

    // random() -> float (0.0 - 1.0)
    if (strcmp(node->name, "random") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_math_random, nullptr, 0, "random_res");
    }

// ====== String Functions - Single Param ======
#define STR1_FUNC(func_name, field)                                            \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef v = codegen_expression(backend, node->arguments[0]);          \
    LLVMValueRef v_ptr = LLVMBuildAlloca(                                      \
        backend->builder, backend->vm_value_type, func_name "_arg_ptr");       \
    LLVMBuildStore(backend->builder, v, v_ptr);                                \
    LLVMValueRef v_void = LLVMBuildBitCast(                                    \
        backend->builder, v_ptr, backend->ptr_type, func_name "_arg_void");    \
    LLVMValueRef s1_args[] = {v_void};                                         \
    return llvm_call_vmvalue_func(backend, backend->field, s1_args, 1,         \
                                  func_name "_res");                           \
  }

    STR1_FUNC("upper", func_aot_string_upper)
    STR1_FUNC("lower", func_aot_string_lower)
    STR1_FUNC("reverse", func_aot_string_reverse)
    STR1_FUNC("isEmpty", func_aot_string_is_empty)
    STR1_FUNC("capitalize", func_aot_string_capitalize)
    STR1_FUNC("isDigit", func_aot_string_is_digit)
    STR1_FUNC("isAlpha", func_aot_string_is_alpha)

#undef STR1_FUNC

// ====== String Functions - Two Params ======
#define STR2_FUNC(func_name, field)                                            \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 2) {       \
    LLVMValueRef v1 = codegen_expression(backend, node->arguments[0]);         \
    LLVMValueRef v2 = codegen_expression(backend, node->arguments[1]);         \
    LLVMValueRef v1_ptr = LLVMBuildAlloca(                                     \
        backend->builder, backend->vm_value_type, func_name "_arg1_ptr");      \
    LLVMBuildStore(backend->builder, v1, v1_ptr);                              \
    LLVMValueRef v2_ptr = LLVMBuildAlloca(                                     \
        backend->builder, backend->vm_value_type, func_name "_arg2_ptr");      \
    LLVMBuildStore(backend->builder, v2, v2_ptr);                              \
    LLVMValueRef v1_void = LLVMBuildBitCast(                                   \
        backend->builder, v1_ptr, backend->ptr_type, func_name "_arg1_void");  \
    LLVMValueRef v2_void = LLVMBuildBitCast(                                   \
        backend->builder, v2_ptr, backend->ptr_type, func_name "_arg2_void");  \
    LLVMValueRef s2_args[] = {v1_void, v2_void};                               \
    return llvm_call_vmvalue_func(backend, backend->field, s2_args, 2,         \
                                  func_name "_res");                           \
  }

    STR2_FUNC("contains", func_aot_string_contains)
    STR2_FUNC("startsWith", func_aot_string_starts_with)
    STR2_FUNC("endsWith", func_aot_string_ends_with)
    STR2_FUNC("indexOf", func_aot_string_index_of)
    STR2_FUNC("repeat", func_aot_string_repeat)
    STR2_FUNC("count", func_aot_string_count)
    STR2_FUNC("join", func_aot_string_join)

#undef STR2_FUNC

    // substring(str, start, end) -> string
    if (strcmp(node->name, "substring") == 0 && node->argument_count >= 3) {
      LLVMValueRef v0 = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef v1 = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef v2 = codegen_expression(backend, node->arguments[2]);
      LLVMValueRef v0_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "substring_arg0_ptr");
      LLVMBuildStore(backend->builder, v0, v0_ptr);
      LLVMValueRef v1_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "substring_arg1_ptr");
      LLVMBuildStore(backend->builder, v1, v1_ptr);
      LLVMValueRef v2_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "substring_arg2_ptr");
      LLVMBuildStore(backend->builder, v2, v2_ptr);
      LLVMValueRef v0_void = LLVMBuildBitCast(
          backend->builder, v0_ptr, backend->ptr_type, "substring_arg0_void");
      LLVMValueRef v1_void = LLVMBuildBitCast(
          backend->builder, v1_ptr, backend->ptr_type, "substring_arg1_void");
      LLVMValueRef v2_void = LLVMBuildBitCast(
          backend->builder, v2_ptr, backend->ptr_type, "substring_arg2_void");
      LLVMValueRef args[] = {v0_void, v1_void, v2_void};
      return llvm_call_vmvalue_func(backend, backend->func_aot_string_substring, args, 3, "substring_res");
    }

    // ====== Time Functions ======
    // timestamp() -> int (unix timestamp)
    if (strcmp(node->name, "timestamp") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_timestamp, nullptr, 0, "timestamp_res");
    }

    // time_ms() -> int (milliseconds)
    if (strcmp(node->name, "time_ms") == 0) {
      return llvm_call_vmvalue_func(backend, backend->func_aot_time_ms, nullptr, 0, "time_ms_res");
    }

    // sleep(ms)
    if (strcmp(node->name, "sleep") == 0 && node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef arg_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "sleep_arg_ptr");
      LLVMBuildStore(backend->builder, arg, arg_ptr);
      LLVMValueRef arg_void = LLVMBuildBitCast(
          backend->builder, arg_ptr, backend->ptr_type, "sleep_arg_void");
      LLVMValueRef args[] = {arg_void};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_sleep),
                     backend->func_aot_sleep, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // ====== JSON Functions ======
    // fromJson(str) -> object/array
    if (strcmp(node->name, "fromJson") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_from_json, args, 1, "from_json_res");
    }

    // ====== Input Functions ======
    // input_int(prompt) -> int
    if (strcmp(node->name, "input_int") == 0) {
      LLVMValueRef prompt =
          node->argument_count >= 1
              ? codegen_expression(backend, node->arguments[0])
              : llvm_vm_val_int(backend, 0);
      LLVMValueRef args[] = {prompt};
      return llvm_call_vmvalue_func(backend, backend->func_aot_input_int, args, 1, "input_int_res");
    }

    // input_float(prompt) -> float
    if (strcmp(node->name, "input_float") == 0) {
      LLVMValueRef prompt =
          node->argument_count >= 1
              ? codegen_expression(backend, node->arguments[0])
              : llvm_vm_val_int(backend, 0);
      LLVMValueRef args[] = {prompt};
      return llvm_call_vmvalue_func(backend, backend->func_aot_input_float, args, 1, "input_float_res");
    }

    // ====== Range Function ======
    // range(end) -> array [0, 1, ..., end-1]
    if (strcmp(node->name, "range") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_range, args, 1, "range_res");
    }

    // ====== SQLite Database Functions ======
    // db_open(path) -> db_handle
    if (strcmp(node->name, "db_open") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_open, args, 1, "db_handle");
    }

    // db_close(db)
    if (strcmp(node->name, "db_close") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_db_close),
                     backend->func_aot_db_close, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // db_execute(db, sql) -> bool
    if (strcmp(node->name, "db_execute") == 0 && node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_execute, args, 2, "db_exec_res");
    }

    // db_query(db, sql) -> array of objects
    if (strcmp(node->name, "db_query") == 0 && node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_query, args, 2, "db_query_res");
    }

    // db_last_insert_id(db) -> int64
    if (strcmp(node->name, "db_last_insert_id") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_last_insert_id, args, 1, "db_last_id");
    }

    // db_error(db) -> string
    if (strcmp(node->name, "db_error") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return llvm_call_vmvalue_func(backend, backend->func_aot_db_error, args, 1, "db_err");
    }

// ====== Type Checking Functions ======
#define TYPE_CHECK_FUNC(func_name, field)                                      \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef tc_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0])};                      \
    return llvm_call_vmvalue_func(backend, backend->field, tc_args, 1,         \
                                  func_name "_res");                           \
  }

    TYPE_CHECK_FUNC("typeof", func_aot_typeof)
    TYPE_CHECK_FUNC("isInt", func_aot_is_int)
    TYPE_CHECK_FUNC("isFloat", func_aot_is_float)
    TYPE_CHECK_FUNC("isString", func_aot_is_string)
    TYPE_CHECK_FUNC("isArray", func_aot_is_array)
    TYPE_CHECK_FUNC("isObject", func_aot_is_object)
    TYPE_CHECK_FUNC("isBool", func_aot_is_bool)

#undef TYPE_CHECK_FUNC

    // ====== StringBuilder Functions ======
    // StringBuilder(capacity) -> ptr (stored as int in VMValue)
    if (strcmp(node->name, "StringBuilder") == 0) {
      int capacity = 1024; // default
      if (node->argument_count >= 1) {
        LLVMValueRef cap_val = codegen_expression(backend, node->arguments[0]);
        LLVMValueRef cap_int = llvm_extract_vm_val_int(backend, cap_val);
        capacity = 0; // Will use runtime value
        LLVMValueRef cap_i32 = LLVMBuildTrunc(backend->builder, cap_int,
                                              backend->int32_type, "cap_i32");
        LLVMValueRef args[] = {cap_i32};
        LLVMValueRef sb_ptr = LLVMBuildCall2(
            backend->builder,
            LLVMGlobalGetValueType(backend->func_aot_stringbuilder_new),
            backend->func_aot_stringbuilder_new, args, 1, "sb_ptr");
        // Store pointer as int64 in VMValue
        LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(
            backend->builder, sb_ptr, backend->int_type, "ptr_int");
        return llvm_vm_val_int_val(backend, ptr_as_int);
      } else {
        LLVMValueRef args[] = {LLVMConstInt(backend->int32_type, 1024, 0)};
        LLVMValueRef sb_ptr = LLVMBuildCall2(
            backend->builder,
            LLVMGlobalGetValueType(backend->func_aot_stringbuilder_new),
            backend->func_aot_stringbuilder_new, args, 1, "sb_ptr");
        LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(
            backend->builder, sb_ptr, backend->int_type, "ptr_int");
        return llvm_vm_val_int_val(backend, ptr_as_int);
      }
    }

    // sb_append(sb, value) -> void
    if (strcmp(node->name, "sb_append") == 0 && node->argument_count >= 2) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef val = codegen_expression(backend, node->arguments[1]);
      // Extract pointer from VMValue (stored as int64)
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr, val};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(
                         backend->func_aot_stringbuilder_append_vmvalue),
                     backend->func_aot_stringbuilder_append_vmvalue, args, 2,
                     "");
      return llvm_vm_val_int(backend, 0);
    }

    // sb_tostring(sb) -> VMValue string
    if (strcmp(node->name, "sb_tostring") == 0 && node->argument_count >= 1) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr};
      return llvm_call_vmvalue_func(backend, backend->func_aot_stringbuilder_to_string, args, 1, "sb_str");
    }

    // sb_free(sb) -> void
    if (strcmp(node->name, "sb_free") == 0 && node->argument_count >= 1) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr};
      LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_stringbuilder_free),
          backend->func_aot_stringbuilder_free, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // User-defined or Runtime function call
    char func_name[256];
    if (strcmp(node->name, "main") == 0) {
      snprintf(func_name, sizeof(func_name), "main");
    } else {
      snprintf(func_name, sizeof(func_name), "t_%s", node->name);
    }

    LLVMValueRef func = LLVMGetNamedFunction(backend->module, func_name);
    // Fallback: Check original name (for builtins or non-prefixed)
    if (!func) {
      func = LLVMGetNamedFunction(backend->module, node->name);
    }
    if (func) {
      // Check if it is a registered user function (uses NEW ABI: void
      // func(ret*, args*...))
      LLVMTypeRef user_func_type = nullptr;
      for (int i = 0; i < backend->function_count; i++) {
        if (strcmp(backend->functions[i].name, node->name) == 0) {
          user_func_type = backend->functions[i].type;
          break;
        }
      }

      if (user_func_type) {
        // Check if this is a native ABI function (returns i64, not void)
        LLVMTypeRef ret_type = LLVMGetReturnType(user_func_type);
        if (ret_type == backend->int_type) {
          // --- NATIVE ABI: i64 func(i64, i64, ...) ---
          int arg_count = node->argument_count;
          LLVMValueRef *args = nullptr;
          if (arg_count > 0) {
            args = static_cast<LLVMValueRef*>(malloc(sizeof(LLVMValueRef) * arg_count));
            for (int i = 0; i < arg_count; i++) {
              LLVMValueRef val =
                  codegen_expression(backend, node->arguments[i]);
              // Extract i64 from VMValue
              args[i] =
                  LLVMBuildExtractValue(backend->builder, val, 2, "arg_i64");
            }
          }

          LLVMValueRef result =
              LLVMBuildCall2(backend->builder, user_func_type, func, args,
                             arg_count, "native_call");
          if (args)
            free(args);

          // Box result back to VMValue
          return llvm_vm_val_int_val(backend, result);
        }

        // --- NEW ABI (User Function) ---
        // Look up the callee's FunctionEntry to discover whether any
        // params are typed structs and whether the return is a typed
        // struct. NULL slots stay on the existing VMValue-pointer ABI.
        FunctionEntry *callee_entry = nullptr;
        for (int i = 0; i < backend->function_count; i++) {
          if (strcmp(backend->functions[i].name, node->name) == 0) {
            callee_entry = &backend->functions[i];
            break;
          }
        }

        // 1. Allocate Result Slot. Entry-hoisted: a recursive function (e.g.
        // fib(n-1) + fib(n-2)) emits this alloca many times per stack frame
        // and unbounded recursion would explode the stack otherwise.
        //
        // Struct-returning callee: alloca the struct type instead so the
        // function writes a native struct directly into our slot. The
        // caller code below loads the i64 slot 0 just to keep the
        // expression's VMValue contract (zero) — well-typed code should
        // consume the struct return through the VAR_DECL path, which
        // intercepts before we even get here. Pure expression-context use
        // of a struct return falls back to a placeholder VMValue.
        LLVMValueRef res_ptr;
        StructTypeEntry *ret_st = nullptr;
        if (callee_entry && callee_entry->return_struct_name) {
          ret_st = find_struct_type(backend,
                                     callee_entry->return_struct_name);
        }
        // VAR_DECL hint: when `Point p = make_point();` is being lowered,
        // VAR_DECL has already allocated a typed-struct local for `p` and
        // pinned its alloca on backend->pending_struct_result_ptr (with
        // the matching struct name). Use that alloca directly so the call
        // writes its return into `p` — avoids an intermediate temp + copy.
        if (ret_st && backend->pending_struct_result_ptr &&
            backend->pending_struct_result_name &&
            strcmp(backend->pending_struct_result_name,
                   ret_st->name) == 0) {
          res_ptr = backend->pending_struct_result_ptr;
        } else if (ret_st) {
          res_ptr = llvm_build_alloca_at_entry(
              backend, ret_st->llvm_type, "call_res_struct_ptr");
          // Zero-init so the function's default-return path (which would
          // already write the struct) plus any partial writes still leave
          // the alloca well-defined.
          LLVMBuildStore(backend->builder, LLVMConstNull(ret_st->llvm_type),
                         res_ptr);
        } else {
          res_ptr = llvm_build_alloca_at_entry(
              backend, backend->vm_value_type, "call_res_ptr");
        }
        // Clear the hint slot regardless of whether we consumed it — leaving
        // it set across nested expressions would attach the same alloca to
        // an inner call that happens to have a matching return type.
        backend->pending_struct_result_ptr = nullptr;
        backend->pending_struct_result_name = nullptr;

        // 2. Prepare Args: [ResultPtr, Arg1Ptr, Arg2Ptr...]
        int call_arg_count = node->argument_count + 1;
        LLVMValueRef *args =
            static_cast<LLVMValueRef *>(malloc(sizeof(LLVMValueRef) * call_arg_count));
        args[0] = res_ptr;

        for (int i = 0; i < node->argument_count; i++) {
          // Typed-struct param slot: expect a typed-struct identifier on
          // the caller side and pass its alloca pointer directly. Falls
          // back to the boxed path below for non-identifier exprs (e.g.
          // a struct-returning call as an argument — would need a temp
          // alloca; deferred to a future PR).
          const char *expected_struct = nullptr;
          if (callee_entry && callee_entry->param_struct_names &&
              i < callee_entry->param_count) {
            expected_struct = callee_entry->param_struct_names[i];
          }
          if (expected_struct) {
            ASTNode_C *arg = node->arguments[i];
            const char *src_struct = nullptr;
            LLVMValueRef src_alloca = nullptr;
            if (arg && arg->type == AST_IDENTIFIER && arg->name) {
              src_struct = get_local_struct_type(backend, arg->name);
              src_alloca = get_local(backend, arg->name);
            }
            if (src_struct && src_alloca &&
                strcmp(src_struct, expected_struct) == 0) {
              args[i + 1] = src_alloca;
              continue;
            }
            // Object literal as a struct arg (`f({ x: 1, y: 2 })`):
            // build a temp struct alloca, populate per-field, pass its
            // pointer. Same shape as the VAR_DECL literal path.
            if (arg && arg->type == AST_OBJECT_LITERAL) {
              StructTypeEntry *st = find_struct_type(backend, expected_struct);
              bool keys_ok = st && struct_is_trivially_unboxable(st);
              if (keys_ok) {
                for (int k = 0; k < arg->object_count; k++) {
                  if (struct_type_field_index(st, arg->object_keys[k]) < 0) {
                    keys_ok = false;
                    break;
                  }
                }
              }
              if (keys_ok) {
                LLVMValueRef tmp = llvm_build_alloca_at_entry(
                    backend, st->llvm_type, "arg.struct.lit");
                LLVMBuildStore(backend->builder, LLVMConstNull(st->llvm_type),
                               tmp);
                for (int k = 0; k < arg->object_count; k++) {
                  int idx = struct_type_field_index(st, arg->object_keys[k]);
                  LLVMValueRef vbox =
                      codegen_expression(backend, arg->object_values[k]);
                  LLVMValueRef i64v = LLVMBuildExtractValue(
                      backend->builder, vbox, 2, "arg.lit.i64");
                  LLVMValueRef fp = LLVMBuildStructGEP2(
                      backend->builder, st->llvm_type, tmp,
                      (unsigned)idx, "arg.lit.field.ptr");
                  LLVMBuildStore(backend->builder, i64v, fp);
                }
                args[i + 1] = tmp;
                continue;
              }
            }
            // Struct-returning call as a struct arg (`f(make_v3(...))`):
            // alloca a temp of the expected struct type, pin it on the
            // pending_struct_result_ptr hint, then evaluate the inner
            // call expression. The inner call's codegen sees the hint
            // and writes its return directly into our temp; its VMValue
            // result is a zero placeholder we discard. Without this, the
            // boxed fallback below would hand the inner callee a 16-byte
            // VMValue temp where it expects a struct alloca, and the
            // function would read past the end of the boxed slot.
            if (arg && arg->type == AST_FUNCTION_CALL && arg->name) {
              const char *inner_ret_struct = nullptr;
              for (int j = 0; j < backend->function_count; j++) {
                if (strcmp(backend->functions[j].name, arg->name) == 0) {
                  inner_ret_struct = backend->functions[j].return_struct_name;
                  break;
                }
              }
              if (inner_ret_struct &&
                  strcmp(inner_ret_struct, expected_struct) == 0) {
                StructTypeEntry *st =
                    find_struct_type(backend, expected_struct);
                if (st) {
                  LLVMValueRef tmp = llvm_build_alloca_at_entry(
                      backend, st->llvm_type, "arg.struct.call");
                  LLVMBuildStore(backend->builder,
                                 LLVMConstNull(st->llvm_type), tmp);
                  // Pin the hint so the inner AST_FUNCTION_CALL writes
                  // through to our temp. The call clears the hint after
                  // consuming it; we re-clear defensively to avoid
                  // leaking state into sibling args.
                  backend->pending_struct_result_ptr = tmp;
                  backend->pending_struct_result_name = st->name;
                  (void)codegen_expression(backend, arg);
                  backend->pending_struct_result_ptr = nullptr;
                  backend->pending_struct_result_name = nullptr;
                  args[i + 1] = tmp;
                  continue;
                }
              }
            }
            // Fall through to the boxed VMValue path on any mismatch —
            // the call will likely misbehave at runtime, but preserving
            // the existing path keeps the rest of the program compilable
            // and lets typeinfer surface the user-visible diagnostic.
          }
          // Evaluate argument
          LLVMValueRef val = codegen_expression(backend, node->arguments[i]);
          // Store to temp alloca to get pointer (entry-hoisted)
          LLVMValueRef arg_temp = llvm_build_alloca_at_entry(
              backend, backend->vm_value_type, "arg_tmp");
          LLVMBuildStore(backend->builder, val, arg_temp);
          args[i + 1] = arg_temp;
        }

        // 3. Call Void Function
        LLVMBuildCall2(backend->builder, user_func_type, func, args,
                       call_arg_count, "");
        free(args);

        // 4. Load Result
        // Struct-returning callee in plain expression context: there is no
        // sensible 16-byte VMValue projection of a multi-i64 struct, so we
        // hand back a zero VMValue placeholder. The VAR_DECL path for
        // `Point p = make_point();` intercepts struct-return calls before
        // they reach this branch, so well-typed code never observes the
        // zero. Anything else is a typeinfer-level issue.
        if (ret_st) {
          return llvm_vm_val_int(backend, 0);
        }
        return LLVMBuildLoad2(backend->builder, backend->vm_value_type, res_ptr,
                              "call_res_loaded");
      }

      // --- LEGACY ABI (Runtime Function) ---
      LLVMTypeRef func_type = LLVMGlobalGetValueType(func);

      // Prepare arguments (By Value)
      LLVMValueRef *args = static_cast<LLVMValueRef *>(
          malloc(sizeof(LLVMValueRef) * (node->argument_count + 1)));
      int arg_offset = 0;

      // If function expects VM* as first arg, pass nullptr for now
      unsigned param_count = LLVMCountParams(func);
      if (param_count > 0 && node->argument_count < (int)param_count) {
        args[0] = LLVMConstPointerNull(backend->ptr_type);
        arg_offset = 1;
      }

      for (int i = 0; i < node->argument_count; i++) {
        args[i + arg_offset] = codegen_expression(backend, node->arguments[i]);
      }

      LLVMValueRef result =
          LLVMBuildCall2(backend->builder, func_type, func, args,
                         node->argument_count + arg_offset, "call_result");

      free(args);
      return result;
    }

    {
      char msg[256];
      snprintf(msg, sizeof(msg),
               "'%s' adında bir fonksiyon bulunamadı", node->name);
      report_codegen_error_with_suggestion(
          backend, node->line, "hata", msg, node->name,
          "fonksiyon adını doğru yazdığınızdan ve gerekli modülü import "
          "ettiğinizden emin olun");
    }
    return llvm_vm_val_int(backend, 0);
  }

  default:
    return llvm_vm_val_int(backend, 0); // nullptr/void
  }
}

LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode_C *node) {
  if (!node)
    return nullptr;
  switch (node->type) {
  case AST_VARIABLE_DECL: {
    // Native typed-int global (registered in Pass 0.1 for `int x = ...;`)?
    LLVMValueRef existing_global =
        LLVMGetNamedGlobal(backend->module, node->name);
    if (existing_global &&
        LLVMGlobalGetValueType(existing_global) == backend->int_type) {
      LLVMValueRef int_init;
      if (node->right) {
        TypedValue tv = codegen_typed_expr(backend, node->right);
        if (tv.type == INFERRED_INT || tv.type == INFERRED_BOOL) {
          int_init = tv.value;
        } else if (tv.boxed) {
          int_init = LLVMBuildExtractValue(backend->builder, tv.boxed, 2,
                                           "init_int");
        } else {
          int_init = LLVMConstInt(backend->int_type, 0, 0);
        }
      } else {
        int_init = LLVMConstInt(backend->int_type, 0, 0);
      }
      LLVMBuildStore(backend->builder, int_init, existing_global);
      return existing_global;
    }

    // Native typed-struct path: when the declaration is for a registered
    // user struct AND every field is trivially unboxable (int/bool), allocate
    // the LLVM struct type directly. Field access via `p.x` then lowers to
    // a single getelementptr + load instead of vm_get_element. Falls back
    // to the boxed VMValue path below for:
    //   - structs whose entry was never registered (parser had no TypeDecl)
    //   - structs with non-trivial field types (string, array, nested struct)
    //   - decls whose initializer is something other than the supported
    //     forms below (e.g. `Point p = other_p;` plain copy — still on the
    //     boxed Object path until we lower struct-typed identifier copies).
    //
    // Supported initializer shapes on the typed path:
    //   - no init           -> alloca + zero-fill
    //   - object literal    -> alloca + zero-fill + per-field GEP+store
    //   - struct-returning  -> alloca + pin as pending_struct_result_ptr,
    //     function call        let AST_FUNCTION_CALL write its return
    //                          straight into the alloca (Plan 04 PR5).
    bool init_is_object_literal =
        node->right && node->right->type == AST_OBJECT_LITERAL;
    bool init_is_struct_call = false;
    {
      // Lookahead: peek the FunctionEntry to see if the call returns this
      // declaration's struct type. Cheap (linear scan over function table)
      // and only runs when the rhs is a function call, so the cost stays
      // proportional to actually-typed-struct VAR_DECLs.
      if (node->right && node->right->type == AST_FUNCTION_CALL &&
          node->right->name) {
        for (int i = 0; i < backend->function_count; i++) {
          if (strcmp(backend->functions[i].name,
                     node->right->name) == 0) {
            if (backend->functions[i].return_struct_name) {
              init_is_struct_call = true;
            }
            break;
          }
        }
      }
    }
    // Heap-struct unpack path (Plan 04 v2): `V3 p = points[0];` /
    // `V3 p = some_dict["key"];` — the RHS is a generic VMValue that
    // (we hope) holds an OBJ_STRUCT pointer at runtime. Same stack
    // alloca as the typed paths so subsequent `.x`/`.y` reads keep the
    // GEP+load fast path; the fields are populated via aot_struct_unpack_to
    // at the alloca site. The runtime helper zero-fills on type
    // mismatch so misuse is silent (typeinfer can flag it later).
    bool init_is_heap_unpack = false;
    if (node->data_type == TYPE_CUSTOM && node->right &&
        !init_is_object_literal && !init_is_struct_call) {
      switch (node->right->type) {
      case AST_ARRAY_ACCESS:
      case AST_IDENTIFIER:
      case AST_FUNCTION_CALL:
        init_is_heap_unpack = true;
        break;
      default:
        break;
      }
    }
    if (node->data_type == TYPE_CUSTOM &&
        (!node->right || init_is_object_literal || init_is_struct_call ||
         init_is_heap_unpack)) {
      const char *struct_name = nullptr;
      if (node->return_custom_type) struct_name = node->return_custom_type;
      // VAR_DECL stores the custom type name in `field_custom_types` slot 0
      // when the parser captured an identifier-shaped type. The C-bridge
      // doesn't expose a dedicated `custom_type` field; the parser pipes it
      // via `field_custom_types[0]` for VAR_DECL nodes (see parser.cpp).
      // We accept either source so the codegen stays robust to where the
      // upstream stashes the name.
      if (!struct_name && node->field_custom_types && node->field_count > 0) {
        struct_name = node->field_custom_types[0];
      }
      StructTypeEntry *st = find_struct_type(backend, struct_name);
      bool can_take_typed_path = st && struct_is_trivially_unboxable(st);
      // For a literal init, every key must resolve to a known field of the
      // struct. If any key is bogus (`Point p = { z: 1 };`), fall through to
      // the boxed Object path so the existing object_set runtime can still
      // store it — that's the path codegen already uses for boxed json
      // assignments and produces a sensible runtime error today.
      if (can_take_typed_path && init_is_object_literal) {
        for (int k = 0; k < node->right->object_count; k++) {
          if (struct_type_field_index(st, node->right->object_keys[k]) < 0) {
            can_take_typed_path = false;
            break;
          }
        }
      }
      // Struct-call init must agree on the struct name — otherwise we'd
      // pin the wrong alloca and the function would write a different
      // layout into our slot.
      if (can_take_typed_path && init_is_struct_call) {
        const char *callee_ret_struct = nullptr;
        for (int i = 0; i < backend->function_count; i++) {
          if (strcmp(backend->functions[i].name,
                     node->right->name) == 0) {
            callee_ret_struct = backend->functions[i].return_struct_name;
            break;
          }
        }
        if (!callee_ret_struct ||
            strcmp(callee_ret_struct, st->name) != 0) {
          can_take_typed_path = false;
        }
      }
      if (can_take_typed_path) {
        LLVMValueRef typed_alloca = llvm_build_alloca_at_entry(
            backend, st->llvm_type, node->name);
        // Zero-initialize: matches the legacy boxed path's "int 0" defaults
        // for all fields, and saves the user from reading uninitialised
        // memory before they assign to fields.
        LLVMBuildStore(backend->builder, LLVMConstNull(st->llvm_type),
                       typed_alloca);
        if (init_is_object_literal) {
          // `Point p = { x: 3, y: 4 };` — store each provided field's i64
          // payload via GEP. Slot 2 of the boxed VMValue holds the int/bool
          // payload (true/false box as 1/0). Anything else (string, array)
          // would be miscompiled here, but struct_is_trivially_unboxable()
          // already guarantees every field is int/bool, and the typeinfer
          // strict-mode catches obvious string/array literals against an
          // int field at typecheck time.
          for (int k = 0; k < node->right->object_count; k++) {
            const char *fname = node->right->object_keys[k];
            int idx = struct_type_field_index(st, fname);
            LLVMValueRef val_box = codegen_expression(
                backend, node->right->object_values[k]);
            LLVMValueRef i64_val = LLVMBuildExtractValue(
                backend->builder, val_box, 2, "lit.i64");
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                backend->builder, st->llvm_type, typed_alloca,
                (unsigned)idx, "struct.lit.field.ptr");
            LLVMBuildStore(backend->builder, i64_val, field_ptr);
          }
        } else if (init_is_struct_call) {
          // `Point p = make_point();` — pin the alloca on the hint slot,
          // then let the AST_FUNCTION_CALL codegen pick it up as the
          // call's result_ptr (no extra temp + copy).
          backend->pending_struct_result_ptr = typed_alloca;
          backend->pending_struct_result_name = st->name;
          codegen_expression(backend, node->right);
          // AST_FUNCTION_CALL clears the hint after consuming it, but
          // defensively re-clear here in case the lookup mismatched and
          // the call took the regular VMValue path instead.
          backend->pending_struct_result_ptr = nullptr;
          backend->pending_struct_result_name = nullptr;
        } else if (init_is_heap_unpack) {
          // `V3 p = points[0];` — codegen the RHS as a regular VMValue,
          // store it to a temp alloca, hand the pointer to
          // aot_struct_unpack_to. The helper either fills our slots
          // from a heap ObjStruct or leaves them zero (already done by
          // the LLVMConstNull store above). Field reads on `p`
          // afterwards use the existing GEP+load path; no per-access
          // runtime call.
          LLVMValueRef rhs_val = codegen_expression(backend, node->right);
          if (rhs_val) {
            LLVMValueRef rhs_tmp = llvm_build_alloca_at_entry(
                backend, backend->vm_value_type, "unpack.rhs.tmp");
            LLVMBuildStore(backend->builder, rhs_val, rhs_tmp);
            LLVMValueRef field_count = LLVMConstInt(
                backend->int32_type, (unsigned)st->field_count, 0);
            LLVMValueRef unpack_args[] = {rhs_tmp, field_count, typed_alloca};
            LLVMBuildCall2(
                backend->builder,
                LLVMGlobalGetValueType(backend->func_aot_struct_unpack_to),
                backend->func_aot_struct_unpack_to, unpack_args, 3, "");
          }
        }
        add_local_struct(backend, node->name, typed_alloca, st->name);
        return typed_alloca;
      }
    }

    // printf("[AOT] Declaring var: %s\n", node->name);
    LLVMValueRef init;
    if (node->right) {
      init = codegen_expression(backend, node->right);
    } else if (node->data_type == TYPE_CUSTOM) {
      // `Point p;` form: allocate an empty Object so subsequent `p.x = ...`
      // (which desugars to `p["x"] = ...`) hits the object set path
      // instead of erroring on an int target.
      LLVMValueRef args[] = {LLVMConstPointerNull(backend->ptr_type)};
      LLVMValueRef obj_ptr = LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_vm_allocate_object),
          backend->func_vm_allocate_object, args, 1, "decl_obj");
      init = llvm_build_vm_val_obj(backend, obj_ptr);
    } else {
      init = llvm_vm_val_int(backend, 0);
    }

    // Check if it's already a (boxed) global (from pre-scan).
    if (existing_global) {
      LLVMBuildStore(backend->builder, init, existing_global);
      return existing_global;
    }

    LLVMValueRef alloca =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, node->name);
    LLVMBuildStore(backend->builder, init, alloca);
    add_local(backend, node->name, alloca);
    return alloca;
  }
  case AST_ASSIGNMENT: {
    // Native typed-int target fast path: avoid boxing the rhs.
    if (node->name) {
      InferredType vt_t = get_local_type(backend, node->name);
      LLVMValueRef nat_t = get_local_native(backend, node->name);
      if (vt_t == INFERRED_INT && nat_t) {
        // Atomic-RMW fast path. Pattern: `name = name + delta` (or its
        // commutative twin) where `name` is on the counter whitelist.
        // Compiles to a single `atomicrmw add monotonic` so concurrent
        // wings worker threads can't drop increments. We only handle
        // the exact `name + expr` shape — anything more complex
        // (`name = name * 2 + 1` etc.) falls through to the regular
        // load+add+store path; the whitelist exists precisely so
        // non-counter writes pay no atomic cost.
        if (global_needs_atomic_rmw(node->name) && node->right &&
            node->right->type == AST_BINARY_OP &&
            node->right->op == TOKEN_PLUS) {
          ASTNode_C *delta_node = nullptr;
          if (node->right->left &&
              node->right->left->type == AST_IDENTIFIER &&
              node->right->left->name &&
              strcmp(node->right->left->name, node->name) == 0) {
            delta_node = node->right->right;
          } else if (node->right->right &&
                     node->right->right->type == AST_IDENTIFIER &&
                     node->right->right->name &&
                     strcmp(node->right->right->name, node->name) == 0) {
            delta_node = node->right->left;
          }
          if (delta_node) {
            TypedValue dt = codegen_typed_expr(backend, delta_node);
            LLVMValueRef delta_i64;
            if (dt.type == INFERRED_INT || dt.type == INFERRED_BOOL) {
              delta_i64 = dt.value;
            } else if (dt.boxed) {
              delta_i64 = LLVMBuildExtractValue(backend->builder, dt.boxed,
                                                2, "atomic.delta");
            } else {
              delta_i64 = LLVMConstInt(backend->int_type, 0, 0);
            }
            LLVMValueRef old_val = LLVMBuildAtomicRMW(
                backend->builder, LLVMAtomicRMWBinOpAdd, nat_t, delta_i64,
                LLVMAtomicOrderingMonotonic, /*singleThread=*/false);
            // atomicrmw yields the *old* value; reconstruct the new
            // one for the assignment-expression result. Wings counter
            // increments are statements (result unused), so this is
            // mostly cosmetic — but cheap and keeps the shape right.
            LLVMValueRef new_val = LLVMBuildAdd(backend->builder, old_val,
                                                delta_i64, "atomic.new");
            return llvm_vm_val_int_val(backend, new_val);
          }
        }

        TypedValue tv = codegen_typed_expr(backend, node->right);
        LLVMValueRef int_val;
        if (tv.type == INFERRED_INT || tv.type == INFERRED_BOOL) {
          int_val = tv.value;
        } else if (tv.boxed) {
          int_val = LLVMBuildExtractValue(backend->builder, tv.boxed, 2,
                                          "assign_int");
        } else {
          int_val = LLVMConstInt(backend->int_type, 0, 0);
        }
        LLVMBuildStore(backend->builder, int_val, nat_t);
        return llvm_vm_val_int_val(backend, int_val);
      }
    }

    LLVMValueRef val = codegen_expression(backend, node->right);

    // Typed-struct fast path mirror of the AST_ARRAY_ACCESS reader: when
    // assigning to `<typed_struct>.<field>` (parser desugars to
    // ArrayAccess with a string-literal index), lower to a single GEP +
    // store of the native i64 extracted from the rhs VMValue. Falls
    // through to the generic vm_set_element runtime call below for
    // regular json / array writes.
    //
    // The receiver name lives on the ArrayAccess child's `left`
    // identifier (matching the C-bridge conversion); ArrayAccess.name is
    // unset by the parser path.
    if (node->left && node->left->type == AST_ARRAY_ACCESS &&
        node->left->index &&
        node->left->index->type == AST_STRING_LITERAL &&
        node->left->index->value.string_value) {
      const char *recv_name = nullptr;
      if (node->left->left && node->left->left->type == AST_IDENTIFIER &&
          node->left->left->name) {
        recv_name = node->left->left->name;
      } else if (node->left->name) {
        recv_name = node->left->name;
      }
      if (recv_name) {
        const char *struct_name = get_local_struct_type(backend, recv_name);
        if (struct_name) {
          StructTypeEntry *st = find_struct_type(backend, struct_name);
          int idx = struct_type_field_index(
              st, node->left->index->value.string_value);
          if (st && idx >= 0) {
            LLVMValueRef alloca_ptr = get_local(backend, recv_name);
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                backend->builder, st->llvm_type, alloca_ptr,
                (unsigned)idx, "struct.field.ptr");
            // Extract the i64 payload from the boxed VMValue rhs (slot 2
            // of {tag, pad, i64}). The bool case writes the same i64
            // because true/false box with int_val=1 / int_val=0.
            LLVMValueRef rhs_i64 = LLVMBuildExtractValue(
                backend->builder, val, 2, "rhs.i64");
            LLVMBuildStore(backend->builder, rhs_i64, field_ptr);
            return val;
          }
        }
      }
    }

    // Check for Array/Object Assignment: arr[i] = val OR obj["k"] = val
    if (node->left && node->left->type == AST_ARRAY_ACCESS) {
      ASTNode_C *access = node->left;
      LLVMValueRef target = nullptr;

      // Array access can be either:
      // 1. Simple: arr[i] - name is in access->name, left is nullptr
      // 2. Nested: arr[i][j] - left has previous access
      if (access->name) {
        // Simple access: get array from variable
        LLVMValueRef var_ptr = get_local(backend, access->name);
        if (var_ptr) {
          target = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                  var_ptr, access->name);
        }
      } else if (access->left) {
        // Nested access: evaluate left expression
        target = codegen_expression(backend, access->left);
      }

      LLVMValueRef index = codegen_expression(backend, access->index);

      if (target && index) {
        // ============================================================
        // SAFE ELEMENT SET - Use runtime function for all set access
        // This handles arrays, strings, and objects correctly with type
        // checking
        // ============================================================

        // Use generic vm_set_element_ptr for all element set. Allocas hoisted
        // to entry block to avoid stack growth in hot loops.
        LLVMValueRef target_temp = llvm_build_alloca_at_entry(
            backend, backend->vm_value_type, "target_set_tmp");
        LLVMBuildStore(backend->builder, target, target_temp);
        LLVMValueRef index_temp = llvm_build_alloca_at_entry(
            backend, backend->vm_value_type, "index_set_tmp");
        LLVMBuildStore(backend->builder, index, index_temp);
        LLVMValueRef val_temp = llvm_build_alloca_at_entry(
            backend, backend->vm_value_type, "val_set_tmp");
        LLVMBuildStore(backend->builder, val, val_temp);

        LLVMValueRef args[] = {LLVMConstNull(backend->ptr_type), target_temp,
                               index_temp, val_temp};
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_set_element),
                       backend->func_vm_set_element, args, 4, "");
      }
      return val;
    }

    // Standard Variable Assignment
    if (node->name) {
      LLVMValueRef target = get_local(backend, node->name);
      if (target)
        LLVMBuildStore(backend->builder, val, target);
      else {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "'%s' değişkeni atamada tanımsız", node->name);
        report_codegen_error_with_suggestion(backend, node->line, "hata", msg, node->name,
                             "atadığınız değişkeni önce 'int x = ...' "
                             "veya benzer bir bildirimle tanımlayın");
      }
    }
    return val;
  }
  case AST_BLOCK: {
    LLVMValueRef last = nullptr;
    if (node->statements) {
      for (int i = 0; i < node->statement_count; i++) {
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
          break;
        last = codegen_statement(backend, node->statements[i]);
      }
    }
    return last;
  }
  case AST_IF: {
    LLVMValueRef cond = codegen_expression(backend, node->condition);
    cond = llvm_build_is_truthy(backend, cond);
    LLVMBasicBlockRef thenB =
        LLVMAppendBasicBlock(backend->current_function, "then");
    LLVMBasicBlockRef elseB =
        LLVMAppendBasicBlock(backend->current_function, "else");
    LLVMBasicBlockRef mergeB =
        LLVMAppendBasicBlock(backend->current_function, "merge");
    LLVMBuildCondBr(backend->builder, cond, thenB, elseB);
    LLVMPositionBuilderAtEnd(backend->builder, thenB);
    codegen_statement(backend, node->then_branch);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, mergeB);
    LLVMPositionBuilderAtEnd(backend->builder, elseB);
    if (node->else_branch)
      codegen_statement(backend, node->else_branch);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, mergeB);
    LLVMPositionBuilderAtEnd(backend->builder, mergeB);
    return nullptr;
  }
  case AST_WHILE: {
    LLVMBasicBlockRef condB =
        LLVMAppendBasicBlock(backend->current_function, "cond");
    LLVMBasicBlockRef bodyB =
        LLVMAppendBasicBlock(backend->current_function, "body");
    LLVMBasicBlockRef exitB =
        LLVMAppendBasicBlock(backend->current_function, "exit");
    LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, condB);
    LLVMValueRef c = codegen_expression(backend, node->condition);
    c = llvm_build_is_truthy(backend, c);
    LLVMBuildCondBr(backend->builder, c, bodyB, exitB);
    LLVMPositionBuilderAtEnd(backend->builder, bodyB);
    // Push this loop's break/continue targets so any nested AST_BREAK /
    // AST_CONTINUE inside `node->body` knows where to jump.
    if (backend->loop_depth < 32) {
      backend->loop_stack[backend->loop_depth].continue_block = condB;
      backend->loop_stack[backend->loop_depth].break_block = exitB;
      backend->loop_depth++;
    }
    codegen_statement(backend, node->body);
    if (backend->loop_depth > 0) backend->loop_depth--;
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, exitB);
    return nullptr;
  }
  case AST_FOR_IN: {
    // Lower in place to a desugared C-style for over an index, then re-
    // dispatch through the now-AST_BLOCK case. Done lazily on first visit
    // so the AST stays small until/unless this codegen pass touches it.
    lower_for_in_in_place(node);
    return codegen_statement(backend, node);
  }
  case AST_FOR: {
    // for (init; condition; increment) { body }
    // Create new scope for loop variable
    enter_scope(backend);

    if (node->init)
      codegen_statement(backend, node->init);

    LLVMBasicBlockRef condB =
        LLVMAppendBasicBlock(backend->current_function, "for_cond");
    LLVMBasicBlockRef bodyB =
        LLVMAppendBasicBlock(backend->current_function, "for_body");
    LLVMBasicBlockRef incrB =
        LLVMAppendBasicBlock(backend->current_function, "for_incr");
    LLVMBasicBlockRef exitB =
        LLVMAppendBasicBlock(backend->current_function, "for_exit");

    LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, condB);

    LLVMValueRef c =
        node->condition
            ? llvm_build_is_truthy(backend,
                                   codegen_expression(backend, node->condition))
            : LLVMConstInt(backend->bool_type, 1, 0); // true if no condition
    LLVMBuildCondBr(backend->builder, c, bodyB, exitB);

    LLVMPositionBuilderAtEnd(backend->builder, bodyB);
    // continue jumps to incrB (so the loop variable still increments),
    // break jumps to exitB.
    if (backend->loop_depth < 32) {
      backend->loop_stack[backend->loop_depth].continue_block = incrB;
      backend->loop_stack[backend->loop_depth].break_block = exitB;
      backend->loop_depth++;
    }
    codegen_statement(backend, node->body);
    if (backend->loop_depth > 0) backend->loop_depth--;
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, incrB);

    LLVMPositionBuilderAtEnd(backend->builder, incrB);
    if (node->increment)
      codegen_statement(backend, node->increment);
    LLVMBuildBr(backend->builder, condB);

    LLVMPositionBuilderAtEnd(backend->builder, exitB);

    // Exit loop scope
    exit_scope(backend);
    return nullptr;
  }
  case AST_BREAK: {
    if (backend->loop_depth > 0) {
      LLVMBuildBr(backend->builder,
                  backend->loop_stack[backend->loop_depth - 1].break_block);
      // Builder is now positioned at a terminated block; the next
      // statement in this scope (if any) becomes dead code. Spawn a
      // throwaway block so subsequent codegen still has somewhere
      // to write — LLVMVerifier would otherwise reject the function.
      LLVMBasicBlockRef dead = LLVMAppendBasicBlock(
          backend->current_function, "after_break");
      LLVMPositionBuilderAtEnd(backend->builder, dead);
    }
    return nullptr;
  }
  case AST_CONTINUE: {
    if (backend->loop_depth > 0) {
      LLVMBuildBr(backend->builder,
                  backend->loop_stack[backend->loop_depth - 1].continue_block);
      LLVMBasicBlockRef dead = LLVMAppendBasicBlock(
          backend->current_function, "after_continue");
      LLVMPositionBuilderAtEnd(backend->builder, dead);
    }
    return nullptr;
  }
  case AST_RETURN: {
    // Struct-returning function: result_ptr (param 0) is a struct*. Source
    // is expected to be either a typed-struct identifier (already a struct
    // alloca), an object literal we can lower directly into the result, or
    // a function call returning the same struct (handled by routing the
    // call's result_ptr to ours). Anything else (boxed VMValue) is a type
    // mismatch — typeinfer's strict mode catches the obvious cases and the
    // fallback below preserves current behaviour.
    if (backend->current_function_returns_struct && node->return_value) {
      StructTypeEntry *st = find_struct_type(
          backend, backend->current_function_returns_struct);
      LLVMValueRef res_ptr = LLVMGetParam(backend->current_function, 0);
      ASTNode_C *rv = node->return_value;
      if (st && rv->type == AST_IDENTIFIER && rv->name) {
        const char *sn = get_local_struct_type(backend, rv->name);
        LLVMValueRef src = get_local(backend, rv->name);
        if (sn && src && strcmp(sn, st->name) == 0) {
          LLVMValueRef loaded = LLVMBuildLoad2(
              backend->builder, st->llvm_type, src, "ret.struct.load");
          LLVMBuildStore(backend->builder, loaded, res_ptr);
          return LLVMBuildRetVoid(backend->builder);
        }
      }
      // `return make_v3(...)` — let the inner call write through to our
      // result pointer (param 0) instead of doing a load+store dance via
      // the boxed VMValue fallback. Same pin-the-hint trick as the
      // VAR_DECL and struct-arg paths.
      if (st && rv->type == AST_FUNCTION_CALL && rv->name) {
        const char *inner_ret_struct = nullptr;
        for (int i = 0; i < backend->function_count; i++) {
          if (strcmp(backend->functions[i].name, rv->name) == 0) {
            inner_ret_struct = backend->functions[i].return_struct_name;
            break;
          }
        }
        if (inner_ret_struct && strcmp(inner_ret_struct, st->name) == 0) {
          backend->pending_struct_result_ptr = res_ptr;
          backend->pending_struct_result_name = st->name;
          (void)codegen_expression(backend, rv);
          backend->pending_struct_result_ptr = nullptr;
          backend->pending_struct_result_name = nullptr;
          return LLVMBuildRetVoid(backend->builder);
        }
      }
      if (st && rv->type == AST_OBJECT_LITERAL) {
        // `return { x: 1, y: 2 };` — same field-validation + zero-init +
        // GEP+store pattern as the VAR_DECL literal path, but we write
        // straight into the caller-supplied result pointer.
        bool keys_ok = struct_is_trivially_unboxable(st);
        if (keys_ok) {
          for (int k = 0; k < rv->object_count; k++) {
            if (struct_type_field_index(st, rv->object_keys[k]) < 0) {
              keys_ok = false;
              break;
            }
          }
        }
        if (keys_ok) {
          LLVMBuildStore(backend->builder, LLVMConstNull(st->llvm_type),
                         res_ptr);
          for (int k = 0; k < rv->object_count; k++) {
            int idx = struct_type_field_index(st, rv->object_keys[k]);
            LLVMValueRef val_box =
                codegen_expression(backend, rv->object_values[k]);
            LLVMValueRef i64_val = LLVMBuildExtractValue(
                backend->builder, val_box, 2, "ret.lit.i64");
            LLVMValueRef field_ptr = LLVMBuildStructGEP2(
                backend->builder, st->llvm_type, res_ptr,
                (unsigned)idx, "ret.lit.field.ptr");
            LLVMBuildStore(backend->builder, i64_val, field_ptr);
          }
          return LLVMBuildRetVoid(backend->builder);
        }
      }
      // Fallback: behave as before (store boxed VMValue). The caller side
      // either won't reach here for typed struct returns or will mis-read
      // the result; typeinfer is the right place to surface this.
    }
    LLVMValueRef ret =
        node->return_value
            ? codegen_expression(backend, node->return_value)
            : llvm_vm_val_int(backend, 0); // Return 0/Void if no value

    // ABI Change: Store to Result Pointer (Param 0)
    LLVMValueRef res_ptr = LLVMGetParam(backend->current_function, 0);
    LLVMBuildStore(backend->builder, ret, res_ptr);
    return LLVMBuildRetVoid(backend->builder);
  }
  case AST_TRY_CATCH: {
    // jmp_buf* buf = aot_try_push()
    LLVMValueRef buf = LLVMBuildCall2(
        backend->builder, LLVMGlobalGetValueType(backend->func_aot_try_push),
        backend->func_aot_try_push, nullptr, 0, "eh_buf");

    // int result = setjmp(buf)  -- on Windows x64 we actually call
    // _setjmpex(buf, frame_addr) so the SEH frame is recorded; longjmp
    // later needs that to walk the stack without crashing.
    LLVMValueRef result;
    if (backend->func_frameaddress) {
      LLVMValueRef fa_args[] = {LLVMConstInt(backend->int32_type, 0, 0)};
      LLVMValueRef frame_addr = LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_frameaddress),
          backend->func_frameaddress, fa_args, 1, "eh_frame");
      LLVMValueRef setjmp_args[] = {buf, frame_addr};
      result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_setjmp),
          backend->func_setjmp, setjmp_args, 2, "setjmp_res");
    } else {
      LLVMValueRef setjmp_args[] = {buf};
      result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_setjmp),
          backend->func_setjmp, setjmp_args, 1, "setjmp_res");
    }

    // if (result == 0) { try_block } else { catch_block }
    LLVMValueRef is_try =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, result,
                      LLVMConstInt(backend->int32_type, 0, 0), "is_try");

    LLVMBasicBlockRef tryB =
        LLVMAppendBasicBlock(backend->current_function, "try");
    LLVMBasicBlockRef catchB =
        LLVMAppendBasicBlock(backend->current_function, "catch");
    LLVMBasicBlockRef finallyB =
        node->finally_block
            ? LLVMAppendBasicBlock(backend->current_function, "finally")
            : nullptr;
    LLVMBasicBlockRef endB =
        LLVMAppendBasicBlock(backend->current_function, "try_end");

    LLVMBuildCondBr(backend->builder, is_try, tryB, catchB);

    // Try block
    LLVMPositionBuilderAtEnd(backend->builder, tryB);
    codegen_statement(backend, node->try_block);

    // Pop handler on normal exit ONLY if not terminated
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder))) {
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_try_pop),
                     backend->func_aot_try_pop, nullptr, 0, "");
      LLVMBuildBr(backend->builder, finallyB ? finallyB : endB);
    }

    // Catch block
    LLVMPositionBuilderAtEnd(backend->builder, catchB);
    if (node->catch_var && node->catch_block) {
      // Get exception: VMValue e = aot_get_exception()
      LLVMValueRef exc = llvm_call_vmvalue_func(
          backend, backend->func_aot_get_exception, nullptr, 0, "exception");
      // Store to catch variable (at entry)
      LLVMValueRef alloca = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, node->catch_var);
      LLVMBuildStore(backend->builder, exc, alloca);
      add_local(backend, node->catch_var, alloca);

      codegen_statement(backend, node->catch_block);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, finallyB ? finallyB : endB);

    // Finally block
    if (finallyB) {
      LLVMPositionBuilderAtEnd(backend->builder, finallyB);
      codegen_statement(backend, node->finally_block);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
        LLVMBuildBr(backend->builder, endB);
    }

    LLVMPositionBuilderAtEnd(backend->builder, endB);
    return nullptr;
  }
  case AST_THROW: {
    LLVMValueRef exc = node->throw_expr
                           ? codegen_expression(backend, node->throw_expr)
                           : llvm_vm_val_int(backend, 0);
    LLVMValueRef exc_ptr = LLVMBuildAlloca(
        backend->builder, backend->vm_value_type, "throw_exc_ptr");
    LLVMBuildStore(backend->builder, exc, exc_ptr);
    LLVMValueRef exc_void = LLVMBuildBitCast(
        backend->builder, exc_ptr, backend->ptr_type, "throw_exc_void");
    LLVMValueRef args[] = {exc_void};
    LLVMBuildCall2(backend->builder,
                   LLVMGlobalGetValueType(backend->func_aot_throw),
                   backend->func_aot_throw, args, 1, "");
    LLVMBuildUnreachable(backend->builder);
    return nullptr;
  }
  case AST_FUNCTION_DECL:
    return nullptr;
  case AST_FUNCTION_CALL:
    return codegen_expression(backend, node);
  case AST_IMPORT: {
    const char *rel_path = node->value.string_value;
    // Check duplication
    for (int i = 0; i < backend->imported_count; i++) {
      if (strcmp(backend->imported_files[i], rel_path) == 0)
        return nullptr;
    }
    backend->imported_files[backend->imported_count++] = strdup(rel_path);

    if (!backend->quiet)
      printf("[AOT] Importing: %s\n", rel_path);

    char *source = nullptr;
    // Track the resolved file's directory so nested imports inside a
    // multi-file bundle (Plan 02 PR3) can find their siblings. Empty
    // when we resolve via embedded libs / cwd-rooted candidates.
    char resolved_dir[256] = "";
    // Check embedded libs first
    const char *embedded_code = get_embedded_lib(rel_path);
    if (embedded_code) {
      source = strdup(embedded_code);
    } else {
      // Resolution order for `import "name"`:
      //   0. <current_import_dir>/<name>.tpr     (bundle-local sibling)
      //   1. literal `name` (relative path or absolute file)
      //   2. literal + `.tpr` extension
      //   3. tulpar_modules/<name>/<name>.tpr    (vendored entry point)
      //   4. tulpar_modules/<name>.tpr           (single-file vendor)
      //
      // (3) + (4) are how `tulpar pkg install` makes a dep usable: it
      // copies a path: spec into tulpar_modules/<name>/, and the
      // convention is that `<name>.tpr` inside that dir is the entry.
      // (0) is what makes multi-file bundles work — `tulpar_modules/foo/
      // main.tpr` doing `import "util"` finds `tulpar_modules/foo/util.tpr`
      // before falling back to the cwd-rooted candidates that would
      // either miss the file or grab the wrong unrelated package.
      FILE *f = nullptr;
      char resolved_path[512] = "";
      if (backend->current_import_dir[0] != '\0') {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf), "%s/%s.tpr",
                 backend->current_import_dir, rel_path);
        f = fopen(path_buf, "rb");
        if (f) snprintf(resolved_path, sizeof(resolved_path), "%s", path_buf);
      }
      if (!f) {
        f = fopen(rel_path, "rb");
        if (f) snprintf(resolved_path, sizeof(resolved_path), "%s", rel_path);
      }
      if (!f) {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf), "%s.tpr", rel_path);
        f = fopen(path_buf, "rb");
        if (f) snprintf(resolved_path, sizeof(resolved_path), "%s", path_buf);
      }
      if (!f) {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf),
                 "tulpar_modules/%s/%s.tpr", rel_path, rel_path);
        f = fopen(path_buf, "rb");
        if (f) snprintf(resolved_path, sizeof(resolved_path), "%s", path_buf);
      }
      if (!f) {
        char path_buf[512];
        snprintf(path_buf, sizeof(path_buf),
                 "tulpar_modules/%s.tpr", rel_path);
        f = fopen(path_buf, "rb");
        if (f) snprintf(resolved_path, sizeof(resolved_path), "%s", path_buf);
      }
      if (!f) {
        fprintf(stderr, tulpar::i18n::tr_for_en("Error: Could not import file '%s'\n"), rel_path);
        return nullptr;
      }
      fseek(f, 0, SEEK_END);
      long fsize = ftell(f);
      fseek(f, 0, SEEK_SET);

      source = static_cast<char*>(malloc(fsize + 1));
      size_t read_size = fread(source, 1, fsize, f);
      (void)read_size;
      source[fsize] = 0;
      fclose(f);

      // Compute the directory for nested imports. dirname() handling:
      // strip the last `/` (or `\`) segment. If no separator, the file
      // lived in cwd → leave resolved_dir empty so nested imports use
      // the existing cwd-rooted probes only.
      const char *last_slash = nullptr;
      for (const char *p = resolved_path; *p; p++) {
        if (*p == '/' || *p == '\\') last_slash = p;
      }
      if (last_slash && last_slash > resolved_path) {
        size_t dir_len = (size_t)(last_slash - resolved_path);
        if (dir_len >= sizeof(resolved_dir)) dir_len = sizeof(resolved_dir) - 1;
        memcpy(resolved_dir, resolved_path, dir_len);
        resolved_dir[dir_len] = '\0';
      }
    }

    Lexer *lexer = lexer_create(source);
    int token_capacity = 1024;
    int token_count = 0;
    Token **tokens = static_cast<Token**>(malloc(sizeof(Token *) * token_capacity));
    Token *token;
    while ((token = lexer_next_token(lexer))->type() != TOKEN_EOF) {
      if (token_count >= token_capacity) {
        token_capacity *= 2;
        tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
      }
      tokens[token_count++] = token;
    }
    tokens[token_count++] = token; // EOF

    lexer_free(lexer);

    Parser_C *parser = parser_create(tokens, token_count);
    ASTNode_C *module_ast = parser_parse(parser);
    parser_free(parser);

    // Apply `import "x" as alias;` namespacing before any codegen sees the
    // module. The C-bridge stashes the alias in `node->name` (empty string
    // when omitted, in which case apply_import_alias is a no-op).
    if (module_ast && node->name && *node->name) {
      apply_import_alias(module_ast, node->name);
    }

    if (module_ast) {
      if (module_ast->type == AST_PROGRAM && module_ast->statements) {
        // Pass 0.1: Pre-scan for Global Variables (Forward Declaration).
        // Mirror the top-level rule: pure-int globals (declared as
        // `int x = ...;`) get a native i64 global plus a typed-local
        // registration, so codegen_typed_expr can emit unboxed
        // load/store paths for them. Non-int globals stay boxed
        // VMValue. Without this, wings' int counters declared inside
        // the imported module would land on the boxed path and miss
        // the typed-int fast paths (atomic RMW for race-free counter
        // updates included).
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type == AST_VARIABLE_DECL) {
            ASTNode_C *decl = module_ast->statements[i];
            if (!LLVMGetNamedGlobal(backend->module, decl->name)) {
              if (backend->use_static_typing && decl->data_type == TYPE_INT) {
                LLVMValueRef ig = LLVMAddGlobal(
                    backend->module, backend->int_type, decl->name);
                LLVMSetInitializer(ig,
                                   LLVMConstInt(backend->int_type, 0, 0));
                LLVMSetLinkage(ig, LLVMInternalLinkage);
                add_local_typed(backend, decl->name, nullptr,
                                INFERRED_INT, ig);
                if (global_needs_tls(decl->name)) {
                  LLVMSetThreadLocalMode(ig, LLVMGeneralDynamicTLSModel);
                }
              } else {
                LLVMValueRef global_var = LLVMAddGlobal(
                    backend->module, backend->vm_value_type, decl->name);
                LLVMSetInitializer(global_var,
                                   LLVMConstNull(backend->vm_value_type));
                LLVMSetLinkage(global_var, LLVMInternalLinkage);
                if (global_needs_tls(decl->name)) {
                  LLVMSetThreadLocalMode(global_var,
                                         LLVMGeneralDynamicTLSModel);
                }
              }
            }
          }
        }

        // Pass 0.15: Predeclare this module's function signatures BEFORE
        // recursing into nested imports. Cross-module references (e.g.
        // http_utils.tpr referring to router.tpr's json_response) need the
        // parent's signatures visible when the child's bodies are emitted.
        // Without this, importing in the order:
        //   examples/X.tpr -> lib/router.tpr -> lib/http_utils.tpr
        // would emit http_utils bodies first and fail to find json_response.
        {
          LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(backend->builder);
          LLVMValueRef saved_func = backend->current_function;
          for (int i = 0; i < module_ast->statement_count; i++) {
            if (module_ast->statements[i]->type == AST_FUNCTION_DECL)
              predeclare_func_signature(backend, module_ast->statements[i]);
          }
          if (saved_block) LLVMPositionBuilderAtEnd(backend->builder, saved_block);
          backend->current_function = saved_func;
        }

        // Pass 0.2: Process nested Imports LAST (before functions).
        // Save/restore current_import_dir so a bundle's nested imports
        // see THIS module's directory, not the parent's (Plan 02 PR3).
        char saved_import_dir[256];
        snprintf(saved_import_dir, sizeof(saved_import_dir), "%s",
                 backend->current_import_dir);
        snprintf(backend->current_import_dir,
                 sizeof(backend->current_import_dir), "%s", resolved_dir);
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type == AST_IMPORT) {
            codegen_statement(backend, module_ast->statements[i]);
          }
        }
        snprintf(backend->current_import_dir,
                 sizeof(backend->current_import_dir), "%s", saved_import_dir);

        // Pass 1: Compile function definitions from module (bodies)
        // codegen_func_def reuses the pre-declared signature created above.
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type == AST_FUNCTION_DECL) {
            codegen_func_def(backend, module_ast->statements[i]);
          }
        }

        // Pass 2: Execute top-level statements in current scope (skip funcs and imports)
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type != AST_FUNCTION_DECL &&
              module_ast->statements[i]->type != AST_IMPORT) {
            codegen_statement(backend, module_ast->statements[i]);
          }
        }
      }
    }

    for (int i = 0; i < token_count; i++) {
      token_free(tokens[i]);
    }
    free(tokens);
    free(source);
    return nullptr;
  }
  default:
    return codegen_expression(backend, node);
  }
}

// Check if function has all integer parameters (eligible for native ABI)
int is_all_int_params(ASTNode_C *node) {
  if (!node)
    return 0;
  // Allow zero param functions too
  for (int i = 0; i < node->param_count; i++) {
    if (!node->parameters[i] || node->parameters[i]->data_type != TYPE_INT) {
      return 0;
    }
  }
  return 1;
}

// Native function name (adds "_native" suffix)
char *get_native_func_name(const char *name) {
  size_t len = strlen(name) + 8;
  char *native_name = static_cast<char*>(malloc(len));
  snprintf(native_name, len, "%s_native", name);
  return native_name;
}

// Whether the native (i64-only) codegen path can correctly emit `stmt`.
// Forward declared; defined recursively with the body version below.
static int native_codegen_supports_stmt(ASTNode_C *stmt);

// Whether the native codegen path can correctly emit every statement in `body`.
// `body` is expected to be an AST_BLOCK. The native path's body walker handles
// only a fixed set of statement types (return / if / var-decl / assignment /
// while / for) and silently drops everything else — function calls as
// statements (`push(...)`, `print(...)`), throws, try/catch, etc. would turn
// into a no-op exe with no diagnostic. Returning 0 here forces the caller to
// fall back to the regular VMValue codegen.
static int native_codegen_supports_body(ASTNode_C *body) {
  if (!body) return 1;
  if (body->type != AST_BLOCK) return native_codegen_supports_stmt(body);
  if (!body->statements) return 1;
  for (int i = 0; i < body->statement_count; i++) {
    if (!native_codegen_supports_stmt(body->statements[i])) return 0;
  }
  return 1;
}

static int native_codegen_supports_stmt(ASTNode_C *stmt) {
  if (!stmt) return 1;
  switch (stmt->type) {
  case AST_RETURN:
    return 1;
  case AST_ASSIGNMENT:
    // Native locals are i64. `acc[key] = value` needs `acc` loaded as a
    // 16-byte VMValue, which OOB-reads off our 8-byte alloca and crashes
    // codegen (the parser sets `node->name` for plain `x = v` and leaves
    // it null when the lhs is an ArrayAccess subscript). Bail to the
    // boxed function path whenever we see a subscript-assign so the
    // function gets the regular VMValue ABI, where the lhs has the
    // right shape. Plain `x = expr` keeps working.
    if (stmt->name) return 1;
    if (stmt->left && stmt->left->type == AST_ARRAY_ACCESS) return 0;
    return 1;
  case AST_VARIABLE_DECL:
    // Native locals are unconditionally allocated as i64, so only
    // int/bool decls survive the round-trip. `json arr = []`,
    // `string s = "..."`, `Point p;`, etc. silently lose their tag info.
    //
    // `var x = ...` (TYPE_UNKNOWN) is *usually* fine because the
    // initialiser tends to be int — except when it's an object literal
    // (`var acc = {"x": 0}`) or array literal (`var arr = [1,2]`),
    // where the i64 alloca can't hold the boxed VMValue and any later
    // subscript read/write reaches into garbage. Inspect the initialiser
    // shape and bail to the boxed function path for those.
    if (stmt->data_type == TYPE_INT || stmt->data_type == TYPE_BOOL) {
      return 1;
    }
    if (stmt->data_type == TYPE_UNKNOWN) {
      if (stmt->right) {
        switch (stmt->right->type) {
        case AST_OBJECT_LITERAL:
        case AST_ARRAY_LITERAL:
        case AST_STRING_LITERAL:
          return 0;
        default:
          break;
        }
      }
      return 1;
    }
    return 0;
  case AST_IF:
    return native_codegen_supports_body(stmt->then_branch) &&
           native_codegen_supports_body(stmt->else_branch);
  case AST_WHILE:
    return native_codegen_supports_body(stmt->body);
  case AST_FOR:
    // The for-init is a single VAR_DECL/ASSIGNMENT and the increment is an
    // ASSIGNMENT — all already covered by the recursive checks above when we
    // descend into the body, but explicitly verify here for clarity.
    if (stmt->init && !native_codegen_supports_stmt(stmt->init)) return 0;
    if (stmt->increment && !native_codegen_supports_stmt(stmt->increment))
      return 0;
    return native_codegen_supports_body(stmt->body);
  default:
    // Any AST_FUNCTION_CALL / AST_PRINT / AST_THROW / AST_TRY_CATCH /
    // AST_BLOCK / unknown statement type — bail.
    return 0;
  }
}

// Generate a pure native function with i64 parameters and return
// This is called BEFORE codegen_func_def to create a fast path
void codegen_native_func_def(LLVMBackend *backend, ASTNode_C *node) {
  if (!is_all_int_params(node))
    return;

  int param_count = node->param_count;

  // Use existing pre-declared function if available (Pass 1a),
  // otherwise create the signature now.
  LLVMValueRef func = LLVMGetNamedFunction(backend->module, node->name);
  LLVMTypeRef func_type;

  if (func) {
    func_type = LLVMGlobalGetValueType(func);
    // If the function already has a body block, skip re-emission.
    if (LLVMCountBasicBlocks(func) > 0)
      return;
  } else {
    // Native ABI: i64 func(i64, i64, ...)
    LLVMTypeRef *param_types =
        static_cast<LLVMTypeRef*>(malloc(sizeof(LLVMTypeRef) * param_count));
    for (int i = 0; i < param_count; i++) {
      param_types[i] = backend->int_type; // All i64
    }

    func_type =
        LLVMFunctionType(backend->int_type, param_types, param_count, 0);

    // Use original name for function (not _native_ prefix) so calls can find it
    func = LLVMAddFunction(backend->module, node->name, func_type);
    register_function(backend, node->name, func_type); // Register for lookups
    free(param_types);
  }

  // Add function-level attributes.
  //
  //   nounwind         this function doesn't throw exceptions
  //   inlinehint       hint the inliner to lean toward inlining; matches
  //                    the regular boxed-typed function path
  //   uwtable          keep the unwind table so longjmp can SEH-walk
  //                    through this frame on Windows x64 (Issue #53)
  //   frame-pointer=all required for the same SEH-walk path
  //
  // Historically this path also set `noinline,optnone` "to prevent
  // LLVM from eliminating loops via closed-form solutions" — i.e. to
  // protect the loopsum benchmark from being folded to its analytic
  // result by SCEV. That handicapped EVERY typed function: fib paid
  // 2x because mem2reg, GVN, instcombine could not touch alloca'd
  // params, redundant zext/icmp, etc. Benchmarks have been switched
  // to env-based loop bounds (TULPAR_BENCH_N) so the fold can no
  // longer happen, and the optnone gag is removed here.
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(backend->context,
                              LLVMGetEnumAttributeKindForName("inlinehint", 10),
                              0));
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("uwtable", 7), 2));
  LLVMAddTargetDependentFunctionAttr(func, "frame-pointer", "all");

  LLVMValueRef prev_func = backend->current_function;
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(backend->builder);
  backend->current_function = func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);

  // Add parameters as native i64 locals
  for (int i = 0; i < param_count; i++) {
    if (node->parameters[i]) {
      LLVMValueRef param = LLVMGetParam(func, i);
      LLVMValueRef alloca = LLVMBuildAlloca(backend->builder, backend->int_type,
                                            node->parameters[i]->name);
      LLVMBuildStore(backend->builder, param, alloca);

      // Add as typed local (INFERRED_INT with native alloca)
      add_local_typed(backend, node->parameters[i]->name, nullptr, INFERRED_INT,
                      alloca);
    }
  }

  // Generate body using typed expression codegen
  if (node->body && node->body->type == AST_BLOCK && node->body->statements) {
    for (int i = 0; i < node->body->statement_count; i++) {
      ASTNode_C *stmt = node->body->statements[i];

      if (stmt->type == AST_RETURN) {
        // Use typed codegen for return value
        TypedValue ret = codegen_typed_expr(backend, stmt->return_value);
        if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
          LLVMBuildRet(backend->builder, ret.value);
        } else {
          // Extract int from boxed value
          LLVMValueRef boxed = box_typed_value(backend, ret);
          LLVMValueRef int_val =
              LLVMBuildExtractValue(backend->builder, boxed, 2, "ret_int");
          LLVMBuildRet(backend->builder, int_val);
        }
      } else if (stmt->type == AST_IF) {
        // Handle if statement in native context
        TypedValue cond = codegen_typed_expr(backend, stmt->condition);
        LLVMValueRef cond_bool;
        if (cond.type == INFERRED_INT || cond.type == INFERRED_BOOL) {
          cond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, cond.value,
                            LLVMConstInt(backend->int_type, 0, 0), "cond");
        } else {
          LLVMValueRef boxed = box_typed_value(backend, cond);
          LLVMValueRef val =
              LLVMBuildExtractValue(backend->builder, boxed, 2, "cond_val");
          cond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, val,
                            LLVMConstInt(backend->int_type, 0, 0), "cond");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlock(func, "then");
        LLVMBasicBlockRef else_bb =
            stmt->else_branch ? LLVMAppendBasicBlock(func, "else") : nullptr;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "merge");

        LLVMBuildCondBr(backend->builder, cond_bool, then_bb,
                        else_bb ? else_bb : merge_bb);

        // Then branch
        LLVMPositionBuilderAtEnd(backend->builder, then_bb);
        if (stmt->then_branch) {
          // Recursively handle then branch (simplified: just handle return)
          if (stmt->then_branch->type == AST_RETURN) {
            TypedValue ret =
                codegen_typed_expr(backend, stmt->then_branch->return_value);
            if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
              LLVMBuildRet(backend->builder, ret.value);
            } else {
              LLVMValueRef boxed = box_typed_value(backend, ret);
              LLVMValueRef int_val =
                  LLVMBuildExtractValue(backend->builder, boxed, 2, "ret_int");
              LLVMBuildRet(backend->builder, int_val);
            }
          } else if (stmt->then_branch->type == AST_BLOCK &&
                     stmt->then_branch->statements) {
            for (int j = 0; j < stmt->then_branch->statement_count; j++) {
              ASTNode_C *inner = stmt->then_branch->statements[j];
              if (inner->type == AST_RETURN) {
                TypedValue ret =
                    codegen_typed_expr(backend, inner->return_value);
                if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
                  LLVMBuildRet(backend->builder, ret.value);
                } else {
                  LLVMValueRef boxed = box_typed_value(backend, ret);
                  LLVMValueRef int_val = LLVMBuildExtractValue(
                      backend->builder, boxed, 2, "ret_int");
                  LLVMBuildRet(backend->builder, int_val);
                }
                break;
              }
            }
          }
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
          LLVMBuildBr(backend->builder, merge_bb);

        // Else branch
        if (else_bb) {
          LLVMPositionBuilderAtEnd(backend->builder, else_bb);
          // Similar handling for else...
          if (!LLVMGetBasicBlockTerminator(
                  LLVMGetInsertBlock(backend->builder)))
            LLVMBuildBr(backend->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(backend->builder, merge_bb);
      } else if (stmt->type == AST_VARIABLE_DECL) {
        // Native variable declaration
        LLVMValueRef alloca =
            LLVMBuildAlloca(backend->builder, backend->int_type, stmt->name);
        if (stmt->right) {
          TypedValue init = codegen_typed_expr(backend, stmt->right);
          if (init.type == INFERRED_INT || init.type == INFERRED_BOOL) {
            LLVMBuildStore(backend->builder, init.value, alloca);
          } else if (init.boxed) {
            LLVMValueRef int_val = LLVMBuildExtractValue(
                backend->builder, init.boxed, 2, "init_int");
            LLVMBuildStore(backend->builder, int_val, alloca);
          } else {
            LLVMBuildStore(backend->builder,
                           LLVMConstInt(backend->int_type, 0, 0), alloca);
          }
        } else {
          LLVMBuildStore(backend->builder,
                         LLVMConstInt(backend->int_type, 0, 0), alloca);
        }
        add_local_typed(backend, stmt->name, nullptr, INFERRED_INT, alloca);
      } else if (stmt->type == AST_ASSIGNMENT) {
        // Native assignment
        LLVMValueRef var_ptr = get_local_native(
            backend, stmt->left ? stmt->left->name : stmt->name);
        if (var_ptr) {
          TypedValue val = codegen_typed_expr(backend, stmt->right);
          if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
            LLVMBuildStore(backend->builder, val.value, var_ptr);
          } else if (val.boxed) {
            LLVMValueRef int_val = LLVMBuildExtractValue(
                backend->builder, val.boxed, 2, "assign_int");
            LLVMBuildStore(backend->builder, int_val, var_ptr);
          }
        }
      } else if (stmt->type == AST_WHILE) {
        // Native while loop (typed-int locals path)
        LLVMBasicBlockRef w_cond = LLVMAppendBasicBlock(func, "while.cond");
        LLVMBasicBlockRef w_body = LLVMAppendBasicBlock(func, "while.body");
        LLVMBasicBlockRef w_end  = LLVMAppendBasicBlock(func, "while.end");

        LLVMBuildBr(backend->builder, w_cond);

        // Condition
        LLVMPositionBuilderAtEnd(backend->builder, w_cond);
        TypedValue wcond = codegen_typed_expr(backend, stmt->condition);
        LLVMValueRef wcond_bool;
        if (wcond.type == INFERRED_INT || wcond.type == INFERRED_BOOL) {
          wcond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, wcond.value,
                            LLVMConstInt(backend->int_type, 0, 0), "w_cond");
        } else {
          wcond_bool = LLVMConstInt(backend->bool_type, 1, 0);
        }
        LLVMBuildCondBr(backend->builder, wcond_bool, w_body, w_end);

        // Body
        LLVMPositionBuilderAtEnd(backend->builder, w_body);
        if (stmt->body && stmt->body->type == AST_BLOCK) {
          for (int j = 0; j < stmt->body->statement_count; j++) {
            ASTNode_C *body_stmt = stmt->body->statements[j];
            if (body_stmt->type == AST_ASSIGNMENT) {
              LLVMValueRef var_ptr = get_local_native(
                  backend,
                  body_stmt->left ? body_stmt->left->name : body_stmt->name);
              if (var_ptr) {
                TypedValue val = codegen_typed_expr(backend, body_stmt->right);
                if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                  LLVMBuildStore(backend->builder, val.value, var_ptr);
                }
              }
            } else if (body_stmt->type == AST_VARIABLE_DECL) {
              LLVMValueRef alloca = LLVMBuildAlloca(
                  backend->builder, backend->int_type, body_stmt->name);
              if (body_stmt->right) {
                TypedValue init = codegen_typed_expr(backend, body_stmt->right);
                if (init.type == INFERRED_INT) {
                  LLVMBuildStore(backend->builder, init.value, alloca);
                } else {
                  LLVMBuildStore(backend->builder,
                                 LLVMConstInt(backend->int_type, 0, 0), alloca);
                }
              } else {
                LLVMBuildStore(backend->builder,
                               LLVMConstInt(backend->int_type, 0, 0), alloca);
              }
              add_local_typed(backend, body_stmt->name, nullptr, INFERRED_INT,
                              alloca);
            }
          }
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
          LLVMBuildBr(backend->builder, w_cond);

        LLVMPositionBuilderAtEnd(backend->builder, w_end);
      } else if (stmt->type == AST_FOR) {
        // Native for loop
        // Initialize
        if (stmt->init) {
          if (stmt->init->type == AST_VARIABLE_DECL) {
            LLVMValueRef alloca = LLVMBuildAlloca(
                backend->builder, backend->int_type, stmt->init->name);
            if (stmt->init->right) {
              TypedValue init = codegen_typed_expr(backend, stmt->init->right);
              if (init.type == INFERRED_INT) {
                LLVMBuildStore(backend->builder, init.value, alloca);
              } else {
                LLVMBuildStore(backend->builder,
                               LLVMConstInt(backend->int_type, 0, 0), alloca);
              }
            } else {
              LLVMBuildStore(backend->builder,
                             LLVMConstInt(backend->int_type, 0, 0), alloca);
            }
            add_local_typed(backend, stmt->init->name, nullptr, INFERRED_INT,
                            alloca);
          }
        }

        LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlock(func, "for.cond");
        LLVMBasicBlockRef loop_body = LLVMAppendBasicBlock(func, "for.body");
        LLVMBasicBlockRef loop_inc = LLVMAppendBasicBlock(func, "for.inc");
        LLVMBasicBlockRef loop_end = LLVMAppendBasicBlock(func, "for.end");

        LLVMBuildBr(backend->builder, loop_cond);

        // Condition
        LLVMPositionBuilderAtEnd(backend->builder, loop_cond);
        TypedValue cond = codegen_typed_expr(backend, stmt->condition);
        LLVMValueRef cond_bool;
        if (cond.type == INFERRED_INT || cond.type == INFERRED_BOOL) {
          cond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, cond.value,
                            LLVMConstInt(backend->int_type, 0, 0), "for_cond");
        } else {
          cond_bool = LLVMConstInt(backend->bool_type, 1, 0);
        }
        LLVMBuildCondBr(backend->builder, cond_bool, loop_body, loop_end);

        // Body
        LLVMPositionBuilderAtEnd(backend->builder, loop_body);
        if (stmt->body && stmt->body->type == AST_BLOCK) {
          for (int j = 0; j < stmt->body->statement_count; j++) {
            ASTNode_C *body_stmt = stmt->body->statements[j];
            if (body_stmt->type == AST_ASSIGNMENT) {
              LLVMValueRef var_ptr = get_local_native(
                  backend,
                  body_stmt->left ? body_stmt->left->name : body_stmt->name);
              if (var_ptr) {
                TypedValue val = codegen_typed_expr(backend, body_stmt->right);
                if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                  LLVMBuildStore(backend->builder, val.value, var_ptr);
                }
              }
            } else if (body_stmt->type == AST_VARIABLE_DECL) {
              // Variable declaration inside loop body
              LLVMValueRef alloca = LLVMBuildAlloca(
                  backend->builder, backend->int_type, body_stmt->name);
              if (body_stmt->right) {
                TypedValue init = codegen_typed_expr(backend, body_stmt->right);
                if (init.type == INFERRED_INT) {
                  LLVMBuildStore(backend->builder, init.value, alloca);
                } else {
                  LLVMBuildStore(backend->builder,
                                 LLVMConstInt(backend->int_type, 0, 0), alloca);
                }
              } else {
                LLVMBuildStore(backend->builder,
                               LLVMConstInt(backend->int_type, 0, 0), alloca);
              }
              add_local_typed(backend, body_stmt->name, nullptr, INFERRED_INT,
                              alloca);
            } else if (body_stmt->type == AST_FOR) {
              // Nested for loop - generate inline
              ASTNode_C *inner = body_stmt;

              // Initialize inner loop variable
              if (inner->init && inner->init->type == AST_VARIABLE_DECL) {
                LLVMValueRef inner_alloca = LLVMBuildAlloca(
                    backend->builder, backend->int_type, inner->init->name);
                if (inner->init->right) {
                  TypedValue init_val =
                      codegen_typed_expr(backend, inner->init->right);
                  if (init_val.type == INFERRED_INT) {
                    LLVMBuildStore(backend->builder, init_val.value,
                                   inner_alloca);
                  } else {
                    LLVMBuildStore(backend->builder,
                                   LLVMConstInt(backend->int_type, 0, 0),
                                   inner_alloca);
                  }
                } else {
                  LLVMBuildStore(backend->builder,
                                 LLVMConstInt(backend->int_type, 0, 0),
                                 inner_alloca);
                }
                add_local_typed(backend, inner->init->name, nullptr, INFERRED_INT,
                                inner_alloca);
              }

              LLVMBasicBlockRef inner_cond =
                  LLVMAppendBasicBlock(func, "inner.cond");
              LLVMBasicBlockRef inner_body =
                  LLVMAppendBasicBlock(func, "inner.body");
              LLVMBasicBlockRef inner_inc =
                  LLVMAppendBasicBlock(func, "inner.inc");
              LLVMBasicBlockRef inner_end =
                  LLVMAppendBasicBlock(func, "inner.end");

              LLVMBuildBr(backend->builder, inner_cond);

              // Inner condition
              LLVMPositionBuilderAtEnd(backend->builder, inner_cond);
              TypedValue inner_cond_val =
                  codegen_typed_expr(backend, inner->condition);
              LLVMValueRef inner_cond_bool = LLVMBuildICmp(
                  backend->builder, LLVMIntNE, inner_cond_val.value,
                  LLVMConstInt(backend->int_type, 0, 0), "inner_cond");
              LLVMBuildCondBr(backend->builder, inner_cond_bool, inner_body,
                              inner_end);

              // Inner body
              LLVMPositionBuilderAtEnd(backend->builder, inner_body);
              if (inner->body && inner->body->type == AST_BLOCK) {
                for (int k = 0; k < inner->body->statement_count; k++) {
                  ASTNode_C *inner_stmt = inner->body->statements[k];
                  if (inner_stmt->type == AST_ASSIGNMENT) {
                    LLVMValueRef var_ptr = get_local_native(
                        backend, inner_stmt->left ? inner_stmt->left->name
                                                  : inner_stmt->name);
                    if (var_ptr) {
                      TypedValue val =
                          codegen_typed_expr(backend, inner_stmt->right);
                      if (val.type == INFERRED_INT ||
                          val.type == INFERRED_BOOL) {
                        LLVMBuildStore(backend->builder, val.value, var_ptr);
                      }
                    }
                  }
                }
              }
              LLVMBuildBr(backend->builder, inner_inc);

              // Inner increment
              LLVMPositionBuilderAtEnd(backend->builder, inner_inc);
              if (inner->increment &&
                  inner->increment->type == AST_ASSIGNMENT) {
                LLVMValueRef var_ptr =
                    get_local_native(backend, inner->increment->left
                                                  ? inner->increment->left->name
                                                  : inner->increment->name);
                if (var_ptr) {
                  TypedValue val =
                      codegen_typed_expr(backend, inner->increment->right);
                  if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                    LLVMBuildStore(backend->builder, val.value, var_ptr);
                  }
                }
              }
              LLVMBuildBr(backend->builder, inner_cond);

              // Continue after inner loop
              LLVMPositionBuilderAtEnd(backend->builder, inner_end);
            }
          }
        }
        LLVMBuildBr(backend->builder, loop_inc);

        // Increment
        LLVMPositionBuilderAtEnd(backend->builder, loop_inc);
        if (stmt->increment) {
          if (stmt->increment->type == AST_ASSIGNMENT) {
            LLVMValueRef var_ptr = get_local_native(
                backend, stmt->increment->left ? stmt->increment->left->name
                                               : stmt->increment->name);
            if (var_ptr) {
              TypedValue val =
                  codegen_typed_expr(backend, stmt->increment->right);
              if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                LLVMBuildStore(backend->builder, val.value, var_ptr);
              }
            }
          }
        }
        LLVMBuildBr(backend->builder, loop_cond);

        LLVMPositionBuilderAtEnd(backend->builder, loop_end);
      }
    }
  }

  // Default return 0
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
    LLVMBuildRet(backend->builder, LLVMConstInt(backend->int_type, 0, 0));

  exit_scope(backend);

  backend->current_function = prev_func;
  if (prev_block)
    LLVMPositionBuilderAtEnd(backend->builder, prev_block);
}

void codegen_func_def(LLVMBackend *backend, ASTNode_C *node) {
  // Check if function has explicit return type - use native codegen
  if (backend->use_static_typing && node->return_type == TYPE_INT) {
    // Verify all parameters have types
    int all_typed = 1;
    for (int i = 0; i < node->param_count; i++) {
      if (node->parameters[i]->data_type == TYPE_UNKNOWN) {
        all_typed = 0;
        break;
      }
    }
    // Even with `: int` and all-int params, the native path can only emit a
    // limited subset of statements. If the body contains anything outside
    // that subset (json/string locals, standalone calls, throws, ...), fall
    // through to the regular VMValue codegen — otherwise it would silently
    // drop those statements or miscompile non-int locals.
    if (all_typed && is_all_int_params(node) &&
        native_codegen_supports_body(node->body)) {
      codegen_native_func_def(backend, node);
      return;
    }
  }

  int user_param_count = node->param_count;
  int total_params = user_param_count + 1; // +1 for Result Pointer

  char func_name[256];
  if (strcmp(node->name, "main") == 0) {
    snprintf(func_name, sizeof(func_name), "main");
  } else {
    snprintf(func_name, sizeof(func_name), "t_%s", node->name);
  }

  // Define function type: void func(VMValue* result_ptr, VMValue* arg1, ...)
  // (Assuming Void ABI for user functions)

  // Reuse pre-declared signature from Pass 1a (forward refs / mutual recursion)
  // when one exists; otherwise create it now.
  LLVMValueRef func = LLVMGetNamedFunction(backend->module, func_name);
  LLVMTypeRef func_type;
  LLVMTypeRef *param_types = nullptr;

  if (func) {
    func_type = LLVMGlobalGetValueType(func);
    // Already has a body? Don't re-emit.
    if (LLVMCountBasicBlocks(func) > 0)
      return;
  } else {
    // Param types: [ResultPtr, ArgPtr, ArgPtr...]
    param_types =
        static_cast<LLVMTypeRef*>(malloc(sizeof(LLVMTypeRef) * total_params));
    for (int i = 0; i < total_params; i++)
      param_types[i] = backend->ptr_type;

    func_type =
        LLVMFunctionType(backend->void_type, param_types, total_params, 0);

    func = LLVMAddFunction(backend->module, func_name, func_type);
    register_function(
        backend, node->name,
        func_type); // Register with ORIGINAL name for lookup in compiler?
    // Same struct-info populate as predeclare_user_function — needed when
    // codegen_func_def emits a function that wasn't pre-declared (rare,
    // mostly module-edge-case paths). Keeps the FunctionEntry consistent
    // either way so call sites resolve struct params/returns identically.
    set_function_struct_info(backend, node->name, node);
  }
  // Wait, register_function stores it in `functions` array.
  // When we retrieve it later, we verify existance?
  // Let's modify register_function logic or lookups.
  // Actually, LLVMAddFunction uses mangled name.
  // But our internal lookup table `functions` should use original name?
  // Or simpler: LLVMGetNamedFunction should use mangled name.

  // Correction: Register with original name in our internal struct,
  // but LLVM function has mangled name.
  // But wait, codegen_call uses LLVMGetNamedFunction?
  // Let's check codegen_call logic first.

  // Assuming codegen_call uses internal table or GetNamedFunction.
  // If GetNamedFunction, we need to mangle there too.
  // Let's stick to mangled name everywhere for consistency?
  // No, `node->name` is original.

  // Proceed with replacement. But register logic needs update elsewhere
  // potentially.

  // ============================================================
  // INLINING & OPTIMIZATION ATTRIBUTES
  // ============================================================

  // uwtable + frame-pointer=all: the AOT try/catch lowering on Windows
  // x64 lowers `try { ... } catch { ... }` to `_setjmpex(buf, frame)` /
  // `longjmp(buf, 1)`. `longjmp` on Windows x64 walks SEH unwind info
  // (.pdata/.xdata) of every frame between the throw site and the
  // setjmp site via `RtlUnwindEx`. A function without a uwtable emits
  // *no* .pdata entry; when the unwinder hits such a frame it fails
  // fast (STATUS_BAD_STACK / STATUS_STACK_BUFFER_OVERRUN).
  //
  // We previously marked every user function `nounwind`, which lets
  // LLVM legally omit the unwind table. That was true for functions
  // that don't throw — but a function with `try { call(...) }` *does*
  // need to be unwound past, and a function that contains `throw`
  // unwinds itself. Forcing `uwtable` keeps the .pdata entry. Forcing
  // `frame-pointer=all` keeps RBP usable for the SEH frame address
  // recorded in `_setjmpex(buf, __builtin_frame_address(0))`.
  //
  // Why "uwtable" instead of dropping "nounwind"? `nounwind` is a
  // useful optimizer hint — it lets LLVM eliminate landing pads it
  // would otherwise have to keep around for C++-style exceptions, and
  // we're not using those. We just need the unwind table to exist so
  // `longjmp`'s SEH walker can traverse the function. `uwtable` keeps
  // the table; `nounwind` keeps the optimization. See Issue #53 for
  // the minimal repro that surfaces under redirected stdio.
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("uwtable", 7), 2));
  LLVMAddTargetDependentFunctionAttr(func, "frame-pointer", "all");

  // All user functions get inlinehint
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(backend->context,
                              LLVMGetEnumAttributeKindForName("inlinehint", 10),
                              0));

  LLVMValueRef prev_func = backend->current_function;
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(backend->builder);
  backend->current_function = func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);
  backend->current_function_is_void_abi = 1;
  // Pick up the struct-return marker that predeclare set on this function's
  // entry, if any. Saved/restored around the body so nested function decls
  // (currently disallowed but cheap to be defensive) don't leak the flag.
  const char *prev_returns_struct = backend->current_function_returns_struct;
  backend->current_function_returns_struct = nullptr;
  for (int i = 0; i < backend->function_count; i++) {
    if (strcmp(backend->functions[i].name, node->name) == 0) {
      backend->current_function_returns_struct =
          backend->functions[i].return_struct_name;
      break;
    }
  }

  // Handle parameters
  // Param 0 is Result Pointer (used by return)
  // Params 1..N are User Arguments (pointers)
  for (int i = 0; i < user_param_count; i++) {
    if (node->parameters[i]) {
      // Get pointer to argument (Param i+1)
      LLVMValueRef arg_ptr = LLVMGetParam(func, i + 1);

      // Native typed-struct param: when the caller passed a struct pointer
      // through this slot, materialize a local typed alloca and copy the
      // struct contents in. The caller-side codegen guarantees the slot
      // holds a struct pointer whenever this slot's struct name is set on
      // the FunctionEntry. Copy-on-entry preserves pass-by-value semantics
      // (callee mutating `p.x` does not affect the caller) and reuses the
      // existing typed-struct local infrastructure for field access via
      // LLVMBuildStructGEP2 — once we register the alloca with
      // add_local_struct, AST_ARRAY_ACCESS / AST_ASSIGNMENT field reads &
      // writes "just work" without further callee changes.
      const char *param_struct_name = nullptr;
      if (node->parameters[i]->data_type == TYPE_CUSTOM &&
          node->parameters[i]->return_custom_type) {
        StructTypeEntry *st = find_struct_type(
            backend, node->parameters[i]->return_custom_type);
        if (st && struct_is_trivially_unboxable(st)) {
          param_struct_name = st->name;
          LLVMValueRef typed_alloca = llvm_build_alloca_at_entry(
              backend, st->llvm_type, node->parameters[i]->name);
          // load %struct + store covers all field bytes; LLVM lowers to
          // memcpy / per-field movs after O3 — no need for an explicit
          // memcpy intrinsic.
          LLVMValueRef loaded = LLVMBuildLoad2(
              backend->builder, st->llvm_type, arg_ptr,
              node->parameters[i]->name);
          LLVMBuildStore(backend->builder, loaded, typed_alloca);
          add_local_struct(backend, node->parameters[i]->name, typed_alloca,
                           st->name);
          continue;
        }
      }

      // We must load the value to ensure pass-by-value semantics if the var is
      // modified (Or we can treat it as local var pointer if we trust it won't
      // alias?
      //  To be safe and consistent with AST_VARIABLE_DECL, let's copy to local
      //  alloca)

      LLVMValueRef val =
          LLVMBuildLoad2(backend->builder, backend->vm_value_type, arg_ptr,
                         node->parameters[i]->name);

      LLVMValueRef alloca = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, node->parameters[i]->name);
      LLVMBuildStore(backend->builder, val, alloca);
      add_local(backend, node->parameters[i]->name, alloca);
    }
  }

  codegen_statement(backend, node->body);

  // Default return if missing
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder))) {
    LLVMValueRef res_ptr = LLVMGetParam(func, 0);
    if (backend->current_function_returns_struct) {
      // Struct-returning function with no explicit return: zero-fill the
      // result struct so the caller reads consistent default values rather
      // than uninitialised stack contents (matches the boxed path's default
      // VMValue int 0).
      StructTypeEntry *st = find_struct_type(
          backend, backend->current_function_returns_struct);
      if (st) {
        LLVMBuildStore(backend->builder, LLVMConstNull(st->llvm_type),
                       res_ptr);
      }
    } else {
      LLVMBuildStore(backend->builder, llvm_vm_val_int(backend, 0), res_ptr);
    }
    LLVMBuildRetVoid(backend->builder);
  }

  backend->current_function_returns_struct = prev_returns_struct;
  exit_scope(backend);
  if (param_types)
    free(param_types);

  backend->current_function = prev_func;
  if (prev_block)
    LLVMPositionBuilderAtEnd(backend->builder, prev_block);
}

// Pass 1a helper: declare the signature for a single function so that
// forward references / mutual recursion in Pass 1b can resolve.
static void predeclare_func_signature(LLVMBackend *backend, ASTNode_C *node) {
  if (node->type != AST_FUNCTION_DECL) return;
  if (strcmp(node->name, "main") == 0) return; // main is built separately

  // Native ABI? Must match the eligibility decision in codegen_func_def
  // exactly — predeclare creates the LLVM signature and codegen fills the
  // body, so disagreement between the two passes produces bogus IR (native
  // signature with VMValue body or vice versa).
  bool native_eligible = backend->use_static_typing
                         && node->return_type == TYPE_INT
                         && is_all_int_params(node);
  if (native_eligible) {
    for (int i = 0; i < node->param_count; i++) {
      if (node->parameters[i]->data_type == TYPE_UNKNOWN) {
        native_eligible = false;
        break;
      }
    }
  }
  if (native_eligible && !native_codegen_supports_body(node->body)) {
    native_eligible = false;
  }

  if (native_eligible) {
    if (LLVMGetNamedFunction(backend->module, node->name)) return;
    int pc = node->param_count;
    LLVMTypeRef *pt =
        static_cast<LLVMTypeRef*>(malloc(sizeof(LLVMTypeRef) * pc));
    for (int i = 0; i < pc; i++) pt[i] = backend->int_type;
    LLVMTypeRef ft = LLVMFunctionType(backend->int_type, pt, pc, 0);
    LLVMValueRef f = LLVMAddFunction(backend->module, node->name, ft);
    register_function(backend, node->name, ft);
    LLVMAddAttributeAtIndex(
        f, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(
            backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
    free(pt);
  } else {
    char fn[256];
    snprintf(fn, sizeof(fn), "t_%s", node->name);
    if (LLVMGetNamedFunction(backend->module, fn)) return;
    int total = node->param_count + 1; // +1 result ptr
    LLVMTypeRef *pt =
        static_cast<LLVMTypeRef*>(malloc(sizeof(LLVMTypeRef) * total));
    for (int i = 0; i < total; i++) pt[i] = backend->ptr_type;
    LLVMTypeRef ft = LLVMFunctionType(backend->void_type, pt, total, 0);
    LLVMValueRef f = LLVMAddFunction(backend->module, fn, ft);
    register_function(backend, node->name, ft);
    // Boxed ABI keeps every parameter slot as a generic `ptr`, so struct
    // params/return don't change the LLVM signature — they only change how
    // each pointer is dereferenced. set_function_struct_info records which
    // slots carry typed struct memory so the call-site codegen and the
    // function body codegen agree on the dereference shape.
    set_function_struct_info(backend, node->name, node);
    LLVMAddAttributeAtIndex(
        f, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(
            backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
    free(pt);
  }
}

void llvm_backend_compile(LLVMBackend *backend, ASTNode_C *node) {
  if (node->type != AST_PROGRAM)
    return;

  // MAIN FUNCTION: int main() -> returns raw i32 (OS exit code)
  // We must create it FIRST so that imports (Pass 0) can emit init code into
  // it.
  LLVMTypeRef main_type = LLVMFunctionType(backend->int32_type, nullptr, 0, 0);
  LLVMValueRef main_func = LLVMAddFunction(backend->module, "main", main_type);
  backend->current_function = main_func;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);

  // Initialize Runtime
  LLVMBuildCall2(backend->builder,
                 LLVMGlobalGetValueType(backend->func_aot_runtime_init),
                 backend->func_aot_runtime_init, nullptr, 0, "");

  if (node->statements) {
    // Pass 0.0: Register user-defined struct types so subsequent passes
    // (variable decls, field accesses) can resolve `struct Point { ... }`
    // declarations regardless of textual order. Done before globals so a
    // typed-struct global would also see the layout, though that codepath
    // isn't wired up yet.
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_TYPE_DECL) {
        register_struct_type(backend, node->statements[i]);
      }
    }

    // Pass 0.1: Pre-scan for Global Variables (Forward Declaration)
    // Pure-int top-level globals (declared as `int x = ...;`) get a native
    // i64 global plus a typed-local registration so codegen_typed_expr can
    // emit unboxed loads/stores. Non-int globals stay boxed VMValue.
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_VARIABLE_DECL) {
        ASTNode_C *decl = node->statements[i];
        if (!LLVMGetNamedGlobal(backend->module, decl->name)) {
          if (backend->use_static_typing && decl->data_type == TYPE_INT) {
            LLVMValueRef ig = LLVMAddGlobal(
                backend->module, backend->int_type, decl->name);
            LLVMSetInitializer(ig, LLVMConstInt(backend->int_type, 0, 0));
            add_local_typed(backend, decl->name, nullptr, INFERRED_INT, ig);
            if (global_needs_tls(decl->name)) {
              LLVMSetThreadLocalMode(ig, LLVMGeneralDynamicTLSModel);
            }
          } else {
            LLVMValueRef global_var = LLVMAddGlobal(
                backend->module, backend->vm_value_type, decl->name);
            LLVMSetInitializer(global_var,
                               LLVMConstNull(backend->vm_value_type));
            if (global_needs_tls(decl->name)) {
              LLVMSetThreadLocalMode(global_var,
                                     LLVMGeneralDynamicTLSModel);
            }
          }
        }
      }
    }

    // Pass 0.2: Process Imports LAST (before functions)
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_IMPORT) {
        codegen_statement(backend, node->statements[i]);
      }
    }

    // Pass 1a: Forward-declare all user function signatures so that
    // bodies (Pass 1b) can call recursively / call peers defined later.
    LLVMBasicBlockRef saved_block = LLVMGetInsertBlock(backend->builder);
    LLVMValueRef saved_func = backend->current_function;
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_FUNCTION_DECL)
        predeclare_func_signature(backend, node->statements[i]);
    }
    if (saved_block) LLVMPositionBuilderAtEnd(backend->builder, saved_block);
    backend->current_function = saved_func;

    // Pass 1b: Emit function bodies
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_FUNCTION_DECL)
        codegen_func_def(backend, node->statements[i]);
    }
  }

  if (node->statements) {
    // Pass 2: Main loop logic (Execute Statements)
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type != AST_FUNCTION_DECL &&
          node->statements[i]->type != AST_IMPORT) {
        codegen_statement(backend, node->statements[i]);
      }
    }
  }

  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
    LLVMBuildRet(backend->builder, LLVMConstInt(backend->int32_type, 0, 0));

  exit_scope(backend);
}

// Emitting IR file
int llvm_backend_emit_ir_file(LLVMBackend *backend, const char *filename) {
  char *error = nullptr;
  if (LLVMPrintModuleToFile(backend->module, filename, &error) != 0) {
    fprintf(stderr, "Error emitting IR file: %s\n", error);
    LLVMDisposeMessage(error);
    return 1;
  }
  printf("Generated IR file: %s\n", filename);
  return 0;
}

int llvm_backend_emit_object(LLVMBackend *backend, const char *filename) {
  // Initialize only native target (X86 on Linux/Windows)
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmParser();
  LLVMInitializeNativeAsmPrinter();
  char *triple = LLVMGetDefaultTargetTriple();
  LLVMTargetRef target;
  char *error = nullptr;
  if (LLVMGetTargetFromTriple(triple, &target, &error) != 0)
    return 1;
  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
      target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault,
      LLVMCodeModelDefault);
  LLVMSetModuleDataLayout(backend->module, LLVMCreateTargetDataLayout(machine));
  LLVMSetTarget(backend->module, triple);

  // Verify module
  char *verify_error = nullptr;
  if (LLVMVerifyModule(backend->module, LLVMPrintMessageAction,
                       &verify_error) != 0) {
    fprintf(stderr, "Global module verification failed: %s\n", verify_error);
    LLVMDisposeMessage(verify_error);
    // Continue anyway to see if it links? No, it usually crashes.
    // return 1;
  }

  if (LLVMTargetMachineEmitToFile(machine, backend->module, filename,
                                  LLVMObjectFile, &error) != 0) {
    fprintf(stderr, "Error emitting object file: %s\n", error);
    return 1;
  }
  LLVMDisposeTargetMachine(machine);
  LLVMDisposeMessage(triple);
  return 0;
}

// Optimization Pass enabling using new LLVM Pass Manager
void llvm_backend_optimize(LLVMBackend *backend) {
  // Create pass builder options
  LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();

  // Set optimization options
  LLVMPassBuilderOptionsSetVerifyEach(options, 0);
  LLVMPassBuilderOptionsSetDebugLogging(options, 0);
  LLVMPassBuilderOptionsSetLoopInterleaving(options, 1);
  LLVMPassBuilderOptionsSetLoopVectorization(options, 1);
  LLVMPassBuilderOptionsSetSLPVectorization(options, 1);
  // LoopUnrolling was off historically to protect the loopsum micro-benchmark
  // from being SCEV-folded to its closed-form result. Benchmarks now read
  // their iteration count from an env var (`TULPAR_BENCH_N`) so the fold
  // can no longer happen, and we get the unrolling speedup on real loops.
  LLVMPassBuilderOptionsSetLoopUnrolling(options, 1);
  LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(options, 0);
  LLVMPassBuilderOptionsSetMergeFunctions(options, 1);

  // Full default<O3> pipeline. The previous `default<O2>,function(...)`
  // string was effectively dead code — every emitted typed function had
  // `optnone` set, so no pass could touch it (see codegen_native_func_def).
  // With that gag removed, the pipeline level actually matters; O3 buys
  // inliner aggressiveness, vectorization, and SCC-based passes that move
  // fib/struct workloads materially closer to gcc -O2.
  LLVMErrorRef error =
      LLVMRunPasses(backend->module, "default<O3>", nullptr, options);

  if (error) {
    char *msg = LLVMGetErrorMessage(error);
    fprintf(stderr, tulpar::i18n::tr_for_en("[AOT] Warning: Optimization failed: %s\n"), msg);
    LLVMDisposeErrorMessage(msg);
  } else {
    if (!backend->quiet)
      printf("[AOT] Optimizations (O3) applied successfully.\n");
  }

  LLVMDisposePassBuilderOptions(options);
}

// ============================================================================
// STATIC TYPING / NATIVE TYPE CODEGEN
// ============================================================================

void llvm_backend_enable_static_typing(LLVMBackend *backend) {
  backend->use_static_typing = 1;

  // Declare native runtime functions
  // tulpar_print_int(i64) -> void
  LLVMTypeRef print_int_params[] = {backend->int_type};
  LLVMTypeRef print_int_type =
      LLVMFunctionType(backend->void_type, print_int_params, 1, 0);
  backend->func_tulpar_print_int =
      LLVMAddFunction(backend->module, "tulpar_print_int", print_int_type);
  backend->func_tulpar_println_int =
      LLVMAddFunction(backend->module, "tulpar_println_int", print_int_type);

  // tulpar_print_float(double) -> void
  LLVMTypeRef print_float_params[] = {backend->float_type};
  LLVMTypeRef print_float_type =
      LLVMFunctionType(backend->void_type, print_float_params, 1, 0);
  backend->func_tulpar_print_float =
      LLVMAddFunction(backend->module, "tulpar_print_float", print_float_type);
  backend->func_tulpar_println_float = LLVMAddFunction(
      backend->module, "tulpar_println_float", print_float_type);

  // tulpar_print_bool(i8) -> void
  LLVMTypeRef print_bool_params[] = {backend->bool_type};
  LLVMTypeRef print_bool_type =
      LLVMFunctionType(backend->void_type, print_bool_params, 1, 0);
  backend->func_tulpar_print_bool =
      LLVMAddFunction(backend->module, "tulpar_print_bool", print_bool_type);
  backend->func_tulpar_println_bool =
      LLVMAddFunction(backend->module, "tulpar_println_bool", print_bool_type);

  // tulpar_print_string(i8*) -> void
  LLVMTypeRef print_str_params[] = {backend->string_type};
  LLVMTypeRef print_str_type =
      LLVMFunctionType(backend->void_type, print_str_params, 1, 0);
  backend->func_tulpar_print_string =
      LLVMAddFunction(backend->module, "tulpar_print_string", print_str_type);
  backend->func_tulpar_println_string =
      LLVMAddFunction(backend->module, "tulpar_println_string", print_str_type);

  // tulpar_print_newline() -> void
  LLVMTypeRef print_nl_type = LLVMFunctionType(backend->void_type, nullptr, 0, 0);
  backend->func_tulpar_print_newline =
      LLVMAddFunction(backend->module, "tulpar_print_newline", print_nl_type);

  // tulpar_string_concat(i8*, i8*) -> i8*
  LLVMTypeRef concat_params[] = {backend->string_type, backend->string_type};
  LLVMTypeRef concat_type =
      LLVMFunctionType(backend->string_type, concat_params, 2, 0);
  backend->func_tulpar_string_concat =
      LLVMAddFunction(backend->module, "tulpar_string_concat", concat_type);

  // tulpar_string_length(i8*) -> i64
  LLVMTypeRef strlen_params[] = {backend->string_type};
  LLVMTypeRef strlen_type =
      LLVMFunctionType(backend->int_type, strlen_params, 1, 0);
  backend->func_tulpar_string_length =
      LLVMAddFunction(backend->module, "tulpar_string_length", strlen_type);

  // tulpar_clock_ms() -> double
  LLVMTypeRef clock_type = LLVMFunctionType(backend->float_type, nullptr, 0, 0);
  backend->func_tulpar_clock_ms =
      LLVMAddFunction(backend->module, "tulpar_clock_ms", clock_type);
}

// Convert DataType to LLVM type
LLVMTypeRef datatype_to_llvm(LLVMBackend *backend, DataType type) {
  switch (type) {
  case TYPE_INT:
    return backend->int_type;
  case TYPE_FLOAT:
    return backend->float_type;
  case TYPE_BOOL:
    return backend->bool_type;
  case TYPE_STRING:
    return backend->string_type;
  case TYPE_VOID:
    return backend->void_type;
  default:
    return backend->vm_value_type; // Fallback for complex types
  }
}

// Convert DataType to InferredType
InferredType datatype_to_inferred(DataType type) {
  switch (type) {
  case TYPE_INT:
    return INFERRED_INT;
  case TYPE_FLOAT:
    return INFERRED_FLOAT;
  case TYPE_BOOL:
    return INFERRED_BOOL;
  case TYPE_STRING:
    return INFERRED_STRING;
  case TYPE_ARRAY:
  case TYPE_ARRAY_INT:
  case TYPE_ARRAY_FLOAT:
  case TYPE_ARRAY_STR:
  case TYPE_ARRAY_BOOL:
    return INFERRED_ARRAY;
  default:
    return INFERRED_UNKNOWN;
  }
}

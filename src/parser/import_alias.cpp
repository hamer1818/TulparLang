// See import_alias.hpp for the rationale.

#include "import_alias.hpp"

#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

namespace {

// Replace `node->name` (free old, strdup new). Safe on null `node` or
// null `node->name`.
void rename_field(char *&slot, const std::string &new_name) {
  if (slot) std::free(slot);
  slot = static_cast<char *>(std::malloc(new_name.size() + 1));
  std::memcpy(slot, new_name.c_str(), new_name.size() + 1);
}

// Prefix `<alias>__` onto an existing identifier. Cheap helper kept
// inline so the hot recursion below stays straight-line.
std::string mangle(const std::string &alias, const char *original) {
  std::string out;
  out.reserve(alias.size() + 2 + (original ? std::strlen(original) : 0));
  out.append(alias);
  out.append("__");
  if (original) out.append(original);
  return out;
}

// Walk every node reachable through an ASTNode_C. The C-bridge AST is
// a tagged record with many possible child pointers; we conservatively
// recurse into all of them. The set of locally-defined names is
// captured first so we know which FunctionCall targets to rewrite.
void rewrite_calls(ASTNode_C *node, const std::string &alias,
                   const std::unordered_set<std::string> &local_names) {
  if (!node) return;

  if (node->type == AST_FUNCTION_CALL && node->name &&
      local_names.count(node->name)) {
    rename_field(node->name, mangle(alias, node->name));
  }

  // Generic recursion: every pointer the AST node tracks. Some of these
  // are unused for a given node type — that's fine, they'll be null.
  rewrite_calls(node->left, alias, local_names);
  rewrite_calls(node->right, alias, local_names);
  rewrite_calls(node->body, alias, local_names);
  rewrite_calls(node->condition, alias, local_names);
  rewrite_calls(node->then_branch, alias, local_names);
  rewrite_calls(node->else_branch, alias, local_names);
  rewrite_calls(node->init, alias, local_names);
  rewrite_calls(node->increment, alias, local_names);
  rewrite_calls(node->iterable, alias, local_names);
  rewrite_calls(node->return_value, alias, local_names);
  rewrite_calls(node->index, alias, local_names);
  rewrite_calls(node->try_block, alias, local_names);
  rewrite_calls(node->catch_block, alias, local_names);
  rewrite_calls(node->finally_block, alias, local_names);
  rewrite_calls(node->throw_expr, alias, local_names);
  rewrite_calls(node->receiver, alias, local_names);

  for (int i = 0; i < node->statement_count && node->statements; ++i) {
    rewrite_calls(node->statements[i], alias, local_names);
  }
  for (int i = 0; i < node->argument_count && node->arguments; ++i) {
    rewrite_calls(node->arguments[i], alias, local_names);
  }
  for (int i = 0; i < node->element_count && node->elements; ++i) {
    rewrite_calls(node->elements[i], alias, local_names);
  }
  for (int i = 0; i < node->param_count && node->parameters; ++i) {
    rewrite_calls(node->parameters[i], alias, local_names);
  }
  for (int i = 0; i < node->object_count && node->object_values; ++i) {
    rewrite_calls(node->object_values[i], alias, local_names);
  }
  for (int i = 0; i < node->field_count && node->field_defaults; ++i) {
    rewrite_calls(node->field_defaults[i], alias, local_names);
  }
}

}  // namespace

int resolve_qualified_call(ASTNode_C *node,
                           tulpar_function_exists_fn function_exists,
                           void *ctx) {
  if (!node || node->type != AST_FUNCTION_CALL) return 0;
  if (!node->receiver || node->receiver->type != AST_IDENTIFIER) return 0;
  if (!node->receiver->name || !node->name) return 0;
  if (!function_exists) return 0;

  std::string mangled;
  mangled.reserve(std::strlen(node->receiver->name) + 2 +
                  std::strlen(node->name));
  mangled.append(node->receiver->name);
  mangled.append("__");
  mangled.append(node->name);

  // Alias path: <recv>__<name> already registered (apply_import_alias
  // path). Rewrite node->name and free the now-redundant receiver.
  if (function_exists(mangled.c_str(), ctx)) {
    std::free(node->name);
    node->name = static_cast<char *>(std::malloc(mangled.size() + 1));
    std::memcpy(node->name, mangled.c_str(), mangled.size() + 1);
    ast_node_free(node->receiver);
    node->receiver = nullptr;
    return 1;
  }

  // Method path: prepend receiver as the first positional argument. We
  // do not gate this branch on function_exists() — leaving the resolved
  // shape `<name>(receiver, args...)` to fall through into the existing
  // FUNCTION_CALL codegen lets the standard "function not found"
  // diagnostic fire with the user-meaningful method name (`<name>`)
  // rather than the synthetic mangled form.
  int new_count = node->argument_count + 1;
  ASTNode_C **new_args = static_cast<ASTNode_C **>(
      std::malloc(sizeof(ASTNode_C *) * static_cast<size_t>(new_count)));
  new_args[0] = node->receiver;
  for (int i = 0; i < node->argument_count; ++i) {
    new_args[i + 1] = node->arguments[i];
  }
  if (node->arguments) std::free(node->arguments);
  node->arguments = new_args;
  node->argument_count = new_count;
  // argument_names: extend with a null slot for the new receiver-arg if
  // the caller had named-argument metadata. Most call sites leave this
  // null entirely (named args aren't widely used), in which case we
  // skip touching it.
  if (node->argument_names) {
    char **new_names = static_cast<char **>(
        std::malloc(sizeof(char *) * static_cast<size_t>(new_count)));
    new_names[0] = nullptr;
    for (int i = 0; i < node->argument_count - 1; ++i) {
      new_names[i + 1] = node->argument_names[i];
    }
    std::free(node->argument_names);
    node->argument_names = new_names;
  }
  node->receiver = nullptr;
  return 2;
}

void apply_import_alias(ASTNode_C *program, const char *alias) {
  if (!program || !alias || !*alias) return;
  if (program->type != AST_PROGRAM) return;

  std::string alias_str(alias);

  // Pass 1: collect names of top-level FunctionDecls. Only these get
  // mangled; nested function-style declarations (closures, etc.) and
  // top-level variables/imports stay as-is so they do not collide with
  // the importer's identifiers.
  std::unordered_set<std::string> local_names;
  for (int i = 0; i < program->statement_count; ++i) {
    ASTNode_C *stmt = program->statements[i];
    if (stmt && stmt->type == AST_FUNCTION_DECL && stmt->name) {
      local_names.insert(stmt->name);
    }
  }

  if (local_names.empty()) return;

  // Pass 2: rename the decls themselves.
  for (int i = 0; i < program->statement_count; ++i) {
    ASTNode_C *stmt = program->statements[i];
    if (stmt && stmt->type == AST_FUNCTION_DECL && stmt->name &&
        local_names.count(stmt->name)) {
      rename_field(stmt->name, mangle(alias_str, stmt->name));
    }
  }

  // Pass 3: rewrite every internal FunctionCall whose target is one of
  // the (now-mangled) locals. We use the pre-mangle name set so the
  // rewrite matches the original spellings inside this module.
  rewrite_calls(program, alias_str, local_names);
}

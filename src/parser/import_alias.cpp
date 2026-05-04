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

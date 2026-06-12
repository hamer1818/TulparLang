// Tulpar Type Inference Module
// Full static type inference for compile-time type checking

#ifndef TULPAR_TYPEINFER_H
#define TULPAR_TYPEINFER_H

#include "../parser/ast_nodes.hpp"
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct InferredTypeInfo {
  DataType type;
  std::optional<std::string> custom_type_name;
  bool is_inferred;
};

// Symbol entry for type tracking
struct TypeSymbol {
  DataType type;
  std::optional<std::string> custom_type_name;
  bool is_mutable; // const değilse true
  bool is_moved;   // move edilmişse true
};

struct FunctionSignature {
  DataType return_type;
  std::optional<std::string> return_custom_type;
  std::vector<DataType> param_types;
};

// User-declared struct (`struct Point { int x; int y; }`). Field
// types are tracked so a future PR can validate `<TypeName> ident;`
// declarations and (Plan 04 PR3+) drive unboxed LLVM struct codegen.
// Order is preserved — codegen emits fields in declaration order.
struct StructTypeInfo {
  std::vector<std::string> field_names;
  std::vector<DataType> field_types;
  std::vector<std::optional<std::string>> field_custom_types;
};

// Type checker context
struct TypeInferContext {
  std::unordered_map<std::string, TypeSymbol> symbols;
  std::unordered_map<std::string, FunctionSignature> functions;
  std::unordered_map<std::string, StructTypeInfo> struct_types;

  // Current function context (for return type checking)
  DataType current_return_type;
  std::string current_function_name;

  // Error tracking
  int error_count;
  std::string last_error;

  // When true, diagnostics are formatted as `[typecheck]` warnings
  // (informational, non-fatal) instead of `Type Error:` lines, and the
  // optional `source_path` is folded in so editors can jump to the file.
  // Used by the build/run pre-pass (`typeinfer_emit_warnings`); the
  // standalone `tulpar typecheck` subcommand keeps the default error mode.
  bool warning_mode;
  std::string source_path;
};

// ============================================================================
// API Functions
// ============================================================================

// Create/destroy context
TypeInferContext *typeinfer_create(void);
void typeinfer_destroy(TypeInferContext *ctx);

// Main inference functions
DataType typeinfer_expression(TypeInferContext *ctx, const ASTNode *expr);
void typeinfer_statement(TypeInferContext *ctx, const ASTNode *stmt);
void typeinfer_program(TypeInferContext *ctx, const ASTNode *program);

// Symbol table management
void typeinfer_add_symbol(TypeInferContext *ctx, const char *name, DataType type);
DataType typeinfer_lookup_symbol(TypeInferContext *ctx, const char *name);
void typeinfer_mark_moved(TypeInferContext *ctx, const char *name);
int typeinfer_is_moved(TypeInferContext *ctx, const char *name);

// Function registration
void typeinfer_register_function(TypeInferContext *ctx, const char *name, 
                                  DataType return_type, DataType *param_types, int param_count);
DataType typeinfer_get_function_return_type(TypeInferContext *ctx, const char *name);

// Error handling
int typeinfer_has_errors(TypeInferContext *ctx);
const char *typeinfer_get_last_error(TypeInferContext *ctx);

// Utility: Convert DataType to string for error messages
const char *datatype_to_string(DataType type);

// Type compatibility checking
int types_compatible(DataType a, DataType b);
DataType promote_types(DataType a, DataType b);  // For binary ops

#endif // TULPAR_TYPEINFER_H

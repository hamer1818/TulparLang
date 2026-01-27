// Tulpar Type Inference Module
// Full static type inference for compile-time type checking

#ifndef TULPAR_TYPEINFER_H
#define TULPAR_TYPEINFER_H

#include "../parser/parser.h"

// Type inference result
typedef struct {
  DataType type;
  char *custom_type_name;  // TYPE_CUSTOM için tip adı
  int is_inferred;         // 1 if type was inferred, 0 if explicit
} InferredTypeInfo;

// Symbol entry for type tracking
typedef struct {
  char *name;
  DataType type;
  char *custom_type_name;
  int is_mutable;          // const değilse 1
  int is_moved;            // move edilmişse 1
} TypeSymbol;

// Type checker context
typedef struct {
  TypeSymbol symbols[1024];
  int symbol_count;
  
  // Function return types for call expression inference
  struct {
    char *name;
    DataType return_type;
    char *return_custom_type;
    DataType *param_types;
    int param_count;
  } functions[256];
  int function_count;
  
  // Current function context (for return type checking)
  DataType current_return_type;
  char *current_function_name;
  
  // Error tracking
  int error_count;
  char *last_error;
} TypeInferContext;

// ============================================================================
// API Functions
// ============================================================================

// Create/destroy context
TypeInferContext *typeinfer_create(void);
void typeinfer_destroy(TypeInferContext *ctx);

// Main inference functions
DataType typeinfer_expression(TypeInferContext *ctx, ASTNode *expr);
void typeinfer_statement(TypeInferContext *ctx, ASTNode *stmt);
void typeinfer_program(TypeInferContext *ctx, ASTNode *program);

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

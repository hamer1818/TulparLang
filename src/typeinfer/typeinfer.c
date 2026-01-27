// Tulpar Type Inference Module - Implementation
// Full static type inference for compile-time type checking

#include "typeinfer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Utility Functions
// ============================================================================

const char *datatype_to_string(DataType type) {
  switch (type) {
    case TYPE_INT:         return "int";
    case TYPE_FLOAT:       return "float";
    case TYPE_STRING:      return "str";
    case TYPE_BOOL:        return "bool";
    case TYPE_VOID:        return "void";
    case TYPE_ARRAY:       return "array";
    case TYPE_ARRAY_INT:   return "arrayInt";
    case TYPE_ARRAY_FLOAT: return "arrayFloat";
    case TYPE_ARRAY_STR:   return "arrayStr";
    case TYPE_ARRAY_BOOL:  return "arrayBool";
    case TYPE_ARRAY_JSON:  return "arrayJson";
    case TYPE_CUSTOM:      return "custom";
    default:               return "unknown";
  }
}

int types_compatible(DataType a, DataType b) {
  if (a == b) return 1;
  
  // int ve float birbirine atanabilir (implicit conversion)
  if ((a == TYPE_INT && b == TYPE_FLOAT) || 
      (a == TYPE_FLOAT && b == TYPE_INT)) {
    return 1;
  }
  
  // array tipleri kendi içinde uyumlu
  if ((a == TYPE_ARRAY || a == TYPE_ARRAY_INT || a == TYPE_ARRAY_FLOAT || 
       a == TYPE_ARRAY_STR || a == TYPE_ARRAY_BOOL || a == TYPE_ARRAY_JSON) &&
      (b == TYPE_ARRAY || b == TYPE_ARRAY_INT || b == TYPE_ARRAY_FLOAT || 
       b == TYPE_ARRAY_STR || b == TYPE_ARRAY_BOOL || b == TYPE_ARRAY_JSON)) {
    // Specific array to generic array is ok
    if (a == TYPE_ARRAY || b == TYPE_ARRAY) return 1;
  }
  
  return 0;
}

DataType promote_types(DataType a, DataType b) {
  // Aynı tip
  if (a == b) return a;
  
  // int + float = float
  if ((a == TYPE_INT && b == TYPE_FLOAT) || 
      (a == TYPE_FLOAT && b == TYPE_INT)) {
    return TYPE_FLOAT;
  }
  
  // string + anything = string (concatenation)
  if (a == TYPE_STRING || b == TYPE_STRING) {
    return TYPE_STRING;
  }
  
  return a; // Default to first type
}

// ============================================================================
// Context Management
// ============================================================================

TypeInferContext *typeinfer_create(void) {
  TypeInferContext *ctx = (TypeInferContext *)calloc(1, sizeof(TypeInferContext));
  ctx->symbol_count = 0;
  ctx->function_count = 0;
  ctx->error_count = 0;
  ctx->last_error = NULL;
  ctx->current_return_type = TYPE_VOID;
  ctx->current_function_name = NULL;
  return ctx;
}

void typeinfer_destroy(TypeInferContext *ctx) {
  if (!ctx) return;
  
  // Free symbol names
  for (int i = 0; i < ctx->symbol_count; i++) {
    if (ctx->symbols[i].name) free(ctx->symbols[i].name);
    if (ctx->symbols[i].custom_type_name) free(ctx->symbols[i].custom_type_name);
  }
  
  // Free function entries
  for (int i = 0; i < ctx->function_count; i++) {
    if (ctx->functions[i].name) free(ctx->functions[i].name);
    if (ctx->functions[i].return_custom_type) free(ctx->functions[i].return_custom_type);
    if (ctx->functions[i].param_types) free(ctx->functions[i].param_types);
  }
  
  if (ctx->last_error) free(ctx->last_error);
  if (ctx->current_function_name) free(ctx->current_function_name);
  
  free(ctx);
}

// ============================================================================
// Symbol Table
// ============================================================================

void typeinfer_add_symbol(TypeInferContext *ctx, const char *name, DataType type) {
  if (ctx->symbol_count >= 1024) {
    fprintf(stderr, "Type Error: Symbol table overflow\n");
    return;
  }
  
  // Check if already exists (update type)
  for (int i = 0; i < ctx->symbol_count; i++) {
    if (strcmp(ctx->symbols[i].name, name) == 0) {
      ctx->symbols[i].type = type;
      return;
    }
  }
  
  ctx->symbols[ctx->symbol_count].name = strdup(name);
  ctx->symbols[ctx->symbol_count].type = type;
  ctx->symbols[ctx->symbol_count].custom_type_name = NULL;
  ctx->symbols[ctx->symbol_count].is_mutable = 1;
  ctx->symbols[ctx->symbol_count].is_moved = 0;
  ctx->symbol_count++;
}

DataType typeinfer_lookup_symbol(TypeInferContext *ctx, const char *name) {
  for (int i = ctx->symbol_count - 1; i >= 0; i--) {
    if (strcmp(ctx->symbols[i].name, name) == 0) {
      return ctx->symbols[i].type;
    }
  }
  return TYPE_VOID; // Not found
}

void typeinfer_mark_moved(TypeInferContext *ctx, const char *name) {
  for (int i = ctx->symbol_count - 1; i >= 0; i--) {
    if (strcmp(ctx->symbols[i].name, name) == 0) {
      ctx->symbols[i].is_moved = 1;
      return;
    }
  }
}

int typeinfer_is_moved(TypeInferContext *ctx, const char *name) {
  for (int i = ctx->symbol_count - 1; i >= 0; i--) {
    if (strcmp(ctx->symbols[i].name, name) == 0) {
      return ctx->symbols[i].is_moved;
    }
  }
  return 0;
}

// ============================================================================
// Function Registry
// ============================================================================

void typeinfer_register_function(TypeInferContext *ctx, const char *name, 
                                  DataType return_type, DataType *param_types, int param_count) {
  if (ctx->function_count >= 256) {
    fprintf(stderr, "Type Error: Function table overflow\n");
    return;
  }
  
  int idx = ctx->function_count++;
  ctx->functions[idx].name = strdup(name);
  ctx->functions[idx].return_type = return_type;
  ctx->functions[idx].return_custom_type = NULL;
  ctx->functions[idx].param_count = param_count;
  
  if (param_count > 0 && param_types) {
    ctx->functions[idx].param_types = (DataType *)malloc(sizeof(DataType) * param_count);
    memcpy(ctx->functions[idx].param_types, param_types, sizeof(DataType) * param_count);
  } else {
    ctx->functions[idx].param_types = NULL;
  }
}

DataType typeinfer_get_function_return_type(TypeInferContext *ctx, const char *name) {
  for (int i = 0; i < ctx->function_count; i++) {
    if (strcmp(ctx->functions[i].name, name) == 0) {
      return ctx->functions[i].return_type;
    }
  }
  return TYPE_VOID; // Unknown function
}

// ============================================================================
// Error Handling
// ============================================================================

int typeinfer_has_errors(TypeInferContext *ctx) {
  return ctx->error_count > 0;
}

const char *typeinfer_get_last_error(TypeInferContext *ctx) {
  return ctx->last_error;
}

static void report_error(TypeInferContext *ctx, const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  if (ctx->last_error) free(ctx->last_error);
  ctx->last_error = strdup(buffer);
  ctx->error_count++;
  
  fprintf(stderr, "Type Error: %s\n", buffer);
}

// ============================================================================
// Expression Type Inference
// ============================================================================

DataType typeinfer_expression(TypeInferContext *ctx, ASTNode *expr) {
  if (!expr) return TYPE_VOID;
  
  switch (expr->type) {
    // Literals - doğrudan tip
    case AST_INT_LITERAL:
      return TYPE_INT;
      
    case AST_FLOAT_LITERAL:
      return TYPE_FLOAT;
      
    case AST_STRING_LITERAL:
      return TYPE_STRING;
      
    case AST_BOOL_LITERAL:
      return TYPE_BOOL;
      
    case AST_ARRAY_LITERAL: {
      if (expr->element_count == 0) {
        return TYPE_ARRAY; // Empty array - generic
      }
      // First element determines array type
      DataType elem_type = typeinfer_expression(ctx, expr->elements[0]);
      
      // Check all elements have same type
      for (int i = 1; i < expr->element_count; i++) {
        DataType t = typeinfer_expression(ctx, expr->elements[i]);
        if (!types_compatible(elem_type, t)) {
          // Mixed types - use generic array
          return TYPE_ARRAY;
        }
        elem_type = promote_types(elem_type, t);
      }
      
      // Return specific array type
      switch (elem_type) {
        case TYPE_INT:    return TYPE_ARRAY_INT;
        case TYPE_FLOAT:  return TYPE_ARRAY_FLOAT;
        case TYPE_STRING: return TYPE_ARRAY_STR;
        case TYPE_BOOL:   return TYPE_ARRAY_BOOL;
        default:          return TYPE_ARRAY;
      }
    }
    
    case AST_OBJECT_LITERAL:
      return TYPE_ARRAY_JSON; // JSON object
      
    case AST_IDENTIFIER: {
      // Check if moved
      if (typeinfer_is_moved(ctx, expr->name)) {
        report_error(ctx, "Use of moved variable '%s' at line %d", 
                     expr->name, expr->line);
      }
      return typeinfer_lookup_symbol(ctx, expr->name);
    }
    
    case AST_BINARY_OP: {
      DataType left_type = typeinfer_expression(ctx, expr->left);
      DataType right_type = typeinfer_expression(ctx, expr->right);
      
      // Comparison operators -> bool
      switch (expr->op) {
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_AND:
        case TOKEN_OR:
          return TYPE_BOOL;
        default:
          break;
      }
      
      // String concatenation
      if (expr->op == TOKEN_PLUS && 
          (left_type == TYPE_STRING || right_type == TYPE_STRING)) {
        return TYPE_STRING;
      }
      
      // Arithmetic -> promote types
      return promote_types(left_type, right_type);
    }
    
    case AST_UNARY_OP: {
      DataType operand_type = typeinfer_expression(ctx, expr->right);
      if (expr->op == TOKEN_BANG) {
        return TYPE_BOOL;
      }
      return operand_type; // - operator preserves type
    }
    
    case AST_FUNCTION_CALL: {
      // Look up function return type
      DataType ret = typeinfer_get_function_return_type(ctx, expr->name);
      if (ret == TYPE_VOID && strcmp(expr->name, "print") != 0 &&
          strcmp(expr->name, "println") != 0) {
        // Might be builtin - check common ones
        if (strcmp(expr->name, "len") == 0) return TYPE_INT;
        if (strcmp(expr->name, "to_string") == 0) return TYPE_STRING;
        if (strcmp(expr->name, "to_int") == 0) return TYPE_INT;
        if (strcmp(expr->name, "to_float") == 0) return TYPE_FLOAT;
        if (strcmp(expr->name, "input") == 0) return TYPE_STRING;
        if (strcmp(expr->name, "clock_ms") == 0) return TYPE_FLOAT;
        // Math functions
        if (strcmp(expr->name, "abs") == 0 || strcmp(expr->name, "sqrt") == 0 ||
            strcmp(expr->name, "floor") == 0 || strcmp(expr->name, "ceil") == 0) {
          return TYPE_FLOAT;
        }
      }
      return ret;
    }
    
    case AST_ARRAY_ACCESS: {
      // Get array type
      DataType arr_type;
      if (expr->name) {
        arr_type = typeinfer_lookup_symbol(ctx, expr->name);
      } else if (expr->left) {
        arr_type = typeinfer_expression(ctx, expr->left);
      } else {
        return TYPE_VOID;
      }
      
      // Return element type
      switch (arr_type) {
        case TYPE_ARRAY_INT:   return TYPE_INT;
        case TYPE_ARRAY_FLOAT: return TYPE_FLOAT;
        case TYPE_ARRAY_STR:   return TYPE_STRING;
        case TYPE_ARRAY_BOOL:  return TYPE_BOOL;
        case TYPE_STRING:      return TYPE_STRING; // char access
        default:               return TYPE_VOID; // Unknown
      }
    }
    
    default:
      return TYPE_VOID;
  }
}

// ============================================================================
// Statement Type Checking
// ============================================================================

void typeinfer_statement(TypeInferContext *ctx, ASTNode *stmt) {
  if (!stmt) return;
  
  switch (stmt->type) {
    case AST_VARIABLE_DECL: {
      DataType declared_type = stmt->data_type;
      
      // Type inference for 'var'
      if (declared_type == TYPE_VOID && stmt->right) {
        declared_type = typeinfer_expression(ctx, stmt->right);
        stmt->data_type = declared_type; // Update AST with inferred type
      }
      
      // Type check initialization
      if (stmt->right) {
        DataType init_type = typeinfer_expression(ctx, stmt->right);
        if (declared_type != TYPE_VOID && !types_compatible(declared_type, init_type)) {
          report_error(ctx, "Type mismatch in declaration of '%s': expected %s, got %s at line %d",
                       stmt->name, datatype_to_string(declared_type), 
                       datatype_to_string(init_type), stmt->line);
        }
      }
      
      typeinfer_add_symbol(ctx, stmt->name, declared_type);
      break;
    }
    
    case AST_ASSIGNMENT: {
      DataType var_type = typeinfer_lookup_symbol(ctx, stmt->name);
      DataType expr_type = typeinfer_expression(ctx, stmt->right);
      
      if (var_type != TYPE_VOID && !types_compatible(var_type, expr_type)) {
        report_error(ctx, "Type mismatch in assignment to '%s': expected %s, got %s at line %d",
                     stmt->name, datatype_to_string(var_type), 
                     datatype_to_string(expr_type), stmt->line);
      }
      break;
    }
    
    case AST_RETURN: {
      if (stmt->return_value) {
        DataType ret_type = typeinfer_expression(ctx, stmt->return_value);
        if (ctx->current_return_type != TYPE_VOID && 
            !types_compatible(ctx->current_return_type, ret_type)) {
          report_error(ctx, "Return type mismatch in function '%s': expected %s, got %s at line %d",
                       ctx->current_function_name ? ctx->current_function_name : "?",
                       datatype_to_string(ctx->current_return_type),
                       datatype_to_string(ret_type), stmt->line);
        }
      }
      break;
    }
    
    case AST_IF: {
      DataType cond_type = typeinfer_expression(ctx, stmt->condition);
      if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
        report_error(ctx, "Condition must be boolean or integer at line %d", stmt->line);
      }
      typeinfer_statement(ctx, stmt->then_branch);
      if (stmt->else_branch) {
        typeinfer_statement(ctx, stmt->else_branch);
      }
      break;
    }
    
    case AST_WHILE: {
      DataType cond_type = typeinfer_expression(ctx, stmt->condition);
      if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
        report_error(ctx, "While condition must be boolean or integer at line %d", stmt->line);
      }
      typeinfer_statement(ctx, stmt->body);
      break;
    }
    
    case AST_FOR: {
      if (stmt->init) typeinfer_statement(ctx, stmt->init);
      if (stmt->condition) {
        DataType cond_type = typeinfer_expression(ctx, stmt->condition);
        if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
          report_error(ctx, "For condition must be boolean or integer at line %d", stmt->line);
        }
      }
      if (stmt->increment) typeinfer_statement(ctx, stmt->increment);
      typeinfer_statement(ctx, stmt->body);
      break;
    }
    
    case AST_BLOCK: {
      for (int i = 0; i < stmt->statement_count; i++) {
        typeinfer_statement(ctx, stmt->statements[i]);
      }
      break;
    }
    
    case AST_FUNCTION_DECL: {
      // Register function
      DataType *param_types = NULL;
      if (stmt->param_count > 0) {
        param_types = (DataType *)malloc(sizeof(DataType) * stmt->param_count);
        for (int i = 0; i < stmt->param_count; i++) {
          param_types[i] = stmt->parameters[i]->data_type;
        }
      }
      typeinfer_register_function(ctx, stmt->name, stmt->return_type, 
                                   param_types, stmt->param_count);
      if (param_types) free(param_types);
      
      // Set context for return type checking
      DataType prev_return = ctx->current_return_type;
      char *prev_func = ctx->current_function_name;
      ctx->current_return_type = stmt->return_type;
      ctx->current_function_name = stmt->name;
      
      // Add parameters as symbols
      for (int i = 0; i < stmt->param_count; i++) {
        typeinfer_add_symbol(ctx, stmt->parameters[i]->name, 
                              stmt->parameters[i]->data_type);
      }
      
      // Type check body
      typeinfer_statement(ctx, stmt->body);
      
      // Restore context
      ctx->current_return_type = prev_return;
      ctx->current_function_name = prev_func;
      break;
    }
    
    default:
      // Other statements - recurse into expressions
      if (stmt->right) typeinfer_expression(ctx, stmt->right);
      if (stmt->left) typeinfer_expression(ctx, stmt->left);
      break;
  }
}

// ============================================================================
// Program Type Checking
// ============================================================================

void typeinfer_program(TypeInferContext *ctx, ASTNode *program) {
  if (!program || program->type != AST_PROGRAM) return;
  
  // First pass: register all functions
  for (int i = 0; i < program->statement_count; i++) {
    ASTNode *stmt = program->statements[i];
    if (stmt->type == AST_FUNCTION_DECL) {
      DataType *param_types = NULL;
      if (stmt->param_count > 0) {
        param_types = (DataType *)malloc(sizeof(DataType) * stmt->param_count);
        for (int j = 0; j < stmt->param_count; j++) {
          param_types[j] = stmt->parameters[j]->data_type;
        }
      }
      typeinfer_register_function(ctx, stmt->name, stmt->return_type, 
                                   param_types, stmt->param_count);
      if (param_types) free(param_types);
    }
  }
  
  // Second pass: type check all statements
  for (int i = 0; i < program->statement_count; i++) {
    typeinfer_statement(ctx, program->statements[i]);
  }
}

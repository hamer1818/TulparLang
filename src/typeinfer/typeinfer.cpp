// Tulpar Type Inference Module - Implementation
// Full static type inference for compile-time type checking

#include "typeinfer.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

template <typename T> const T *as_node(const ASTNode *node) {
  return node ? std::get_if<T>(&node->value) : nullptr;
}

DataType infer_expr(TypeInferContext *ctx, const ASTNode *expr);
void infer_stmt(TypeInferContext *ctx, const ASTNode *stmt);

static void report_error(TypeInferContext *ctx, const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  ctx->last_error = buffer;
  ctx->error_count++;
  fprintf(stderr, "Type Error: %s\n", buffer);
}

static DataType lookup_symbol_type(TypeInferContext *ctx, const std::string &name) {
  auto it = ctx->symbols.find(name);
  if (it == ctx->symbols.end()) {
    return TYPE_VOID;
  }
  return it->second.type;
}

static bool symbol_is_moved(TypeInferContext *ctx, const std::string &name) {
  auto it = ctx->symbols.find(name);
  if (it == ctx->symbols.end()) {
    return false;
  }
  return it->second.is_moved;
}

static DataType function_return_type(TypeInferContext *ctx, const std::string &name) {
  auto it = ctx->functions.find(name);
  if (it == ctx->functions.end()) {
    return TYPE_VOID;
  }
  return it->second.return_type;
}

DataType infer_expr(TypeInferContext *ctx, const ASTNode *expr) {
  if (!expr) {
    return TYPE_VOID;
  }

  if (as_node<IntLiteral>(expr)) {
    return TYPE_INT;
  }
  if (as_node<FloatLiteral>(expr)) {
    return TYPE_FLOAT;
  }
  if (as_node<StringLiteral>(expr)) {
    return TYPE_STRING;
  }
  if (as_node<BoolLiteral>(expr)) {
    return TYPE_BOOL;
  }

  if (const auto *arr = as_node<ArrayLiteral>(expr)) {
    if (arr->elements.empty()) {
      return TYPE_ARRAY;
    }

    DataType elem_type = infer_expr(ctx, arr->elements[0].get());
    for (size_t i = 1; i < arr->elements.size(); ++i) {
      DataType t = infer_expr(ctx, arr->elements[i].get());
      if (!types_compatible(elem_type, t)) {
        return TYPE_ARRAY;
      }
      elem_type = promote_types(elem_type, t);
    }

    switch (elem_type) {
    case TYPE_INT:
      return TYPE_ARRAY_INT;
    case TYPE_FLOAT:
      return TYPE_ARRAY_FLOAT;
    case TYPE_STRING:
      return TYPE_ARRAY_STR;
    case TYPE_BOOL:
      return TYPE_ARRAY_BOOL;
    default:
      return TYPE_ARRAY;
    }
  }

  if (as_node<ObjectLiteral>(expr)) {
    return TYPE_ARRAY_JSON;
  }

  if (const auto *id = as_node<Identifier>(expr)) {
    if (symbol_is_moved(ctx, id->name)) {
      report_error(ctx, "Use of moved variable '%s' at line %d", id->name.c_str(),
                   id->loc.line);
    }
    return lookup_symbol_type(ctx, id->name);
  }

  if (const auto *bin = as_node<BinaryOp>(expr)) {
    DataType left_type = infer_expr(ctx, bin->left.get());
    DataType right_type = infer_expr(ctx, bin->right.get());

    switch (bin->op) {
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

    if (bin->op == TOKEN_PLUS &&
        (left_type == TYPE_STRING || right_type == TYPE_STRING)) {
      return TYPE_STRING;
    }

    return promote_types(left_type, right_type);
  }

  if (const auto *un = as_node<UnaryOp>(expr)) {
    DataType operand_type = infer_expr(ctx, un->operand.get());
    if (un->op == TOKEN_BANG) {
      return TYPE_BOOL;
    }
    return operand_type;
  }

  if (const auto *call = as_node<FunctionCall>(expr)) {
    DataType ret = function_return_type(ctx, call->name);
    if (ret == TYPE_VOID && call->name != "print" && call->name != "println") {
      if (call->name == "len")
        return TYPE_INT;
      if (call->name == "to_string")
        return TYPE_STRING;
      if (call->name == "to_int")
        return TYPE_INT;
      if (call->name == "to_float")
        return TYPE_FLOAT;
      if (call->name == "input")
        return TYPE_STRING;
      if (call->name == "clock_ms")
        return TYPE_FLOAT;
      if (call->name == "abs" || call->name == "sqrt" || call->name == "floor" ||
          call->name == "ceil") {
        return TYPE_FLOAT;
      }
    }
    return ret;
  }

  if (const auto *access = as_node<ArrayAccess>(expr)) {
    DataType arr_type = infer_expr(ctx, access->object.get());
    switch (arr_type) {
    case TYPE_ARRAY_INT:
      return TYPE_INT;
    case TYPE_ARRAY_FLOAT:
      return TYPE_FLOAT;
    case TYPE_ARRAY_STR:
      return TYPE_STRING;
    case TYPE_ARRAY_BOOL:
      return TYPE_BOOL;
    case TYPE_STRING:
      return TYPE_STRING;
    default:
      return TYPE_VOID;
    }
  }

  return TYPE_VOID;
}

void infer_stmt(TypeInferContext *ctx, const ASTNode *stmt) {
  if (!stmt) {
    return;
  }

  if (const auto *decl = as_node<VariableDecl>(stmt)) {
    DataType declared_type = decl->data_type;
    if (declared_type == TYPE_VOID && decl->initializer) {
      declared_type = infer_expr(ctx, decl->initializer.get());
    }
    if (decl->initializer) {
      DataType init_type = infer_expr(ctx, decl->initializer.get());
      if (declared_type != TYPE_VOID && !types_compatible(declared_type, init_type)) {
        report_error(ctx, "Type mismatch in declaration of '%s': expected %s, got %s at line %d",
                     decl->name.c_str(), datatype_to_string(declared_type),
                     datatype_to_string(init_type), decl->loc.line);
      }
    }
    typeinfer_add_symbol(ctx, decl->name.c_str(), declared_type);
    return;
  }

  if (const auto *assign = as_node<Assignment>(stmt)) {
    DataType var_type = lookup_symbol_type(ctx, assign->name);
    DataType expr_type = infer_expr(ctx, assign->value.get());
    if (var_type != TYPE_VOID && !types_compatible(var_type, expr_type)) {
      report_error(ctx, "Type mismatch in assignment to '%s': expected %s, got %s at line %d",
                   assign->name.c_str(), datatype_to_string(var_type),
                   datatype_to_string(expr_type), assign->loc.line);
    }
    return;
  }

  if (const auto *ret = as_node<ReturnStatement>(stmt)) {
    if (ret->value) {
      DataType ret_type = infer_expr(ctx, ret->value.get());
      if (ctx->current_return_type != TYPE_VOID &&
          !types_compatible(ctx->current_return_type, ret_type)) {
        report_error(ctx,
                     "Return type mismatch in function '%s': expected %s, got %s at line %d",
                     ctx->current_function_name.c_str(),
                     datatype_to_string(ctx->current_return_type),
                     datatype_to_string(ret_type), ret->loc.line);
      }
    }
    return;
  }

  if (const auto *if_stmt = as_node<IfStatement>(stmt)) {
    DataType cond_type = infer_expr(ctx, if_stmt->condition.get());
    if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
      report_error(ctx, "Condition must be boolean or integer at line %d",
                   if_stmt->loc.line);
    }
    infer_stmt(ctx, if_stmt->then_branch.get());
    if (if_stmt->else_branch) {
      infer_stmt(ctx, if_stmt->else_branch.get());
    }
    return;
  }

  if (const auto *while_stmt = as_node<WhileLoop>(stmt)) {
    DataType cond_type = infer_expr(ctx, while_stmt->condition.get());
    if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
      report_error(ctx, "While condition must be boolean or integer at line %d",
                   while_stmt->loc.line);
    }
    infer_stmt(ctx, while_stmt->body.get());
    return;
  }

  if (const auto *for_stmt = as_node<ForLoop>(stmt)) {
    infer_stmt(ctx, for_stmt->init.get());
    if (for_stmt->condition) {
      DataType cond_type = infer_expr(ctx, for_stmt->condition.get());
      if (cond_type != TYPE_BOOL && cond_type != TYPE_INT) {
        report_error(ctx, "For condition must be boolean or integer at line %d",
                     for_stmt->loc.line);
      }
    }
    infer_stmt(ctx, for_stmt->increment.get());
    infer_stmt(ctx, for_stmt->body.get());
    return;
  }

  if (const auto *block = as_node<Block>(stmt)) {
    for (const auto &child : block->statements) {
      infer_stmt(ctx, child.get());
    }
    return;
  }

  if (const auto *func = as_node<FunctionDecl>(stmt)) {
    std::vector<DataType> param_types;
    param_types.reserve(func->parameters.size());
    for (const auto &param : func->parameters) {
      param_types.push_back(param.type);
    }
    typeinfer_register_function(ctx, func->name.c_str(), func->return_type,
                                param_types.empty() ? nullptr : param_types.data(),
                                static_cast<int>(param_types.size()));

    const DataType prev_return = ctx->current_return_type;
    const std::string prev_func = ctx->current_function_name;
    ctx->current_return_type = func->return_type;
    ctx->current_function_name = func->name;

    for (const auto &param : func->parameters) {
      typeinfer_add_symbol(ctx, param.name.c_str(), param.type);
    }
    infer_stmt(ctx, func->body.get());

    ctx->current_return_type = prev_return;
    ctx->current_function_name = prev_func;
    return;
  }
}

} // namespace

const char *datatype_to_string(DataType type) {
  switch (type) {
  case TYPE_INT:
    return "int";
  case TYPE_FLOAT:
    return "float";
  case TYPE_STRING:
    return "str";
  case TYPE_BOOL:
    return "bool";
  case TYPE_VOID:
    return "void";
  case TYPE_ARRAY:
    return "array";
  case TYPE_ARRAY_INT:
    return "arrayInt";
  case TYPE_ARRAY_FLOAT:
    return "arrayFloat";
  case TYPE_ARRAY_STR:
    return "arrayStr";
  case TYPE_ARRAY_BOOL:
    return "arrayBool";
  case TYPE_ARRAY_JSON:
    return "arrayJson";
  case TYPE_CUSTOM:
    return "custom";
  default:
    return "unknown";
  }
}

int types_compatible(DataType a, DataType b) {
  if (a == b) {
    return 1;
  }
  if ((a == TYPE_INT && b == TYPE_FLOAT) || (a == TYPE_FLOAT && b == TYPE_INT)) {
    return 1;
  }
  if ((a == TYPE_ARRAY || a == TYPE_ARRAY_INT || a == TYPE_ARRAY_FLOAT ||
       a == TYPE_ARRAY_STR || a == TYPE_ARRAY_BOOL || a == TYPE_ARRAY_JSON) &&
      (b == TYPE_ARRAY || b == TYPE_ARRAY_INT || b == TYPE_ARRAY_FLOAT ||
       b == TYPE_ARRAY_STR || b == TYPE_ARRAY_BOOL || b == TYPE_ARRAY_JSON)) {
    return (a == TYPE_ARRAY || b == TYPE_ARRAY) ? 1 : 0;
  }
  return 0;
}

DataType promote_types(DataType a, DataType b) {
  if (a == b) {
    return a;
  }
  if ((a == TYPE_INT && b == TYPE_FLOAT) || (a == TYPE_FLOAT && b == TYPE_INT)) {
    return TYPE_FLOAT;
  }
  if (a == TYPE_STRING || b == TYPE_STRING) {
    return TYPE_STRING;
  }
  return a;
}

TypeInferContext *typeinfer_create(void) {
  auto *ctx = new TypeInferContext();
  ctx->current_return_type = TYPE_VOID;
  ctx->current_function_name.clear();
  ctx->error_count = 0;
  ctx->last_error.clear();
  return ctx;
}

void typeinfer_destroy(TypeInferContext *ctx) { delete ctx; }

void typeinfer_add_symbol(TypeInferContext *ctx, const char *name, DataType type) {
  if (!name) {
    return;
  }
  TypeSymbol symbol;
  symbol.type = type;
  symbol.custom_type_name = std::nullopt;
  symbol.is_mutable = true;
  symbol.is_moved = false;
  ctx->symbols[std::string(name)] = std::move(symbol);
}

DataType typeinfer_lookup_symbol(TypeInferContext *ctx, const char *name) {
  if (!name) {
    return TYPE_VOID;
  }
  return lookup_symbol_type(ctx, name);
}

void typeinfer_mark_moved(TypeInferContext *ctx, const char *name) {
  if (!name) {
    return;
  }
  auto it = ctx->symbols.find(name);
  if (it != ctx->symbols.end()) {
    it->second.is_moved = true;
  }
}

int typeinfer_is_moved(TypeInferContext *ctx, const char *name) {
  if (!name) {
    return 0;
  }
  return symbol_is_moved(ctx, name) ? 1 : 0;
}

void typeinfer_register_function(TypeInferContext *ctx, const char *name,
                                 DataType return_type, DataType *param_types,
                                 int param_count) {
  if (!name) {
    return;
  }
  FunctionSignature signature;
  signature.return_type = return_type;
  signature.return_custom_type = std::nullopt;
  if (param_count > 0 && param_types) {
    signature.param_types.assign(param_types, param_types + param_count);
  }
  ctx->functions[std::string(name)] = std::move(signature);
}

DataType typeinfer_get_function_return_type(TypeInferContext *ctx, const char *name) {
  if (!name) {
    return TYPE_VOID;
  }
  return function_return_type(ctx, name);
}

int typeinfer_has_errors(TypeInferContext *ctx) { return ctx->error_count > 0; }

const char *typeinfer_get_last_error(TypeInferContext *ctx) {
  return ctx->last_error.empty() ? nullptr : ctx->last_error.c_str();
}

DataType typeinfer_expression(TypeInferContext *ctx, const ASTNode *expr) {
  return infer_expr(ctx, expr);
}

void typeinfer_statement(TypeInferContext *ctx, const ASTNode *stmt) {
  infer_stmt(ctx, stmt);
}

void typeinfer_program(TypeInferContext *ctx, const ASTNode *program) {
  const auto *prog = as_node<Program>(program);
  if (!prog) {
    return;
  }

  for (const auto &stmt : prog->statements) {
    if (const auto *func = as_node<FunctionDecl>(stmt.get())) {
      std::vector<DataType> param_types;
      param_types.reserve(func->parameters.size());
      for (const auto &param : func->parameters) {
        param_types.push_back(param.type);
      }
      typeinfer_register_function(ctx, func->name.c_str(), func->return_type,
                                  param_types.empty() ? nullptr : param_types.data(),
                                  static_cast<int>(param_types.size()));
    }
  }

  for (const auto &stmt : prog->statements) {
    infer_stmt(ctx, stmt.get());
  }
}

// Tulpar Type Inference Module - Implementation
// Full static type inference for compile-time type checking

#include "typeinfer.hpp"
#include "../common/localization.hpp"
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
  if (ctx->warning_mode) {
    // Pre-pass mode: tag as informational so users / editors know the build
    // is still continuing. Include source path when known so jump-to works.
    if (!ctx->source_path.empty()) {
      fprintf(stderr, "[typecheck] %s: %s\n", ctx->source_path.c_str(), buffer);
    } else {
      fprintf(stderr, "[typecheck] %s\n", buffer);
    }
  } else {
    fprintf(stderr, tulpar::i18n::tr_for_en("Type Error: %s\n"), buffer);
  }
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
    return TYPE_JSON;
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
    // Always recurse into arguments so nested calls / expressions get
    // checked even if THIS call is a built-in we don't have a signature
    // for (e.g., `print(add(1))` — print has no signature, but `add(1)`
    // inside it still needs its arg-count checked).
    std::vector<DataType> arg_types;
    arg_types.reserve(call->arguments.size() + 1);
    for (const auto &arg : call->arguments) {
      arg_types.push_back(infer_expr(ctx, arg.get()));
    }

    // Method-call dispatch awareness: when the parser saw
    // `<recv>.<name>(args)` it stored `<recv>` in call->receiver and
    // left call->name unmangled. We mirror the codegen-side resolver
    // (`resolve_qualified_call`) so the arg-count / arg-type checks run
    // against the function shape the AOT/VM will actually emit.
    std::string effective_name = call->name;
    if (call->receiver) {
      DataType receiver_type = infer_expr(ctx, call->receiver.get());
      bool resolved_as_alias = false;
      if (const auto *recv_id = as_node<Identifier>(call->receiver.get())) {
        std::string mangled = recv_id->name + "__" + call->name;
        if (ctx->functions.count(mangled)) {
          effective_name = mangled;
          resolved_as_alias = true;
        }
      }
      if (!resolved_as_alias) {
        // Method path: receiver counts as first positional arg.
        arg_types.insert(arg_types.begin(), receiver_type);
      }
    }

    // User-defined function call: check arg count + arg types against the
    // signature we registered during the pre-pass. Built-ins are not in
    // ctx->functions and are skipped — their argument contracts are too
    // varied to model here without a dedicated catalogue.
    auto sig_it = ctx->functions.find(effective_name);
    if (sig_it != ctx->functions.end()) {
      const FunctionSignature &sig = sig_it->second;
      const int expected = static_cast<int>(sig.param_types.size());
      const int got = static_cast<int>(arg_types.size());
      if (expected != got) {
        report_error(ctx,
                     "Function '%s' expects %d argument(s), got %d at line %d",
                     call->name.c_str(), expected, got, call->loc.line);
      } else {
        // Same unknown-aware tolerance as VarDecl/Assignment: if either side
        // is unknown (TYPE_VOID/UNKNOWN/CUSTOM) we don't flag — better a
        // false negative than a false positive while the catalogue grows.
        auto is_unknown = [](DataType t) {
          return t == TYPE_VOID || t == TYPE_UNKNOWN || t == TYPE_CUSTOM;
        };
        // Polymorphism categories for select built-ins. The catalog
        // registers these with TYPE_UNKNOWN to keep the storage shape
        // simple, so the call-site check folds the category in here
        // rather than extending FunctionSignature with overload sets.
        // Only fires when the registered param IS the wildcard — user
        // functions named `len`/`abs` (rare but legal) keep their own
        // declared types.
        auto is_collection = [](DataType t) {
          return t == TYPE_STRING || t == TYPE_ARRAY ||
                 t == TYPE_ARRAY_INT || t == TYPE_ARRAY_FLOAT ||
                 t == TYPE_ARRAY_STR || t == TYPE_ARRAY_BOOL ||
                 t == TYPE_ARRAY_JSON;
        };
        auto is_numeric = [](DataType t) {
          return t == TYPE_INT || t == TYPE_FLOAT;
        };
        const bool poly_collection =
            (call->name == "len" || call->name == "length");
        const bool poly_numeric = (call->name == "abs");

        for (int i = 0; i < expected; ++i) {
          DataType param_type = sig.param_types[i];
          DataType arg_type = arg_types[i];

          // Polymorphic position with concrete arg → use category check
          // instead of the wildcard-skip default.
          if (param_type == TYPE_UNKNOWN && i == 0 && !is_unknown(arg_type)) {
            if (poly_collection && !is_collection(arg_type)) {
              report_error(ctx,
                           "Argument %d of '%s': expected string or array, got %s at line %d",
                           i + 1, call->name.c_str(),
                           datatype_to_string(arg_type), call->loc.line);
              continue;
            }
            if (poly_numeric && !is_numeric(arg_type)) {
              report_error(ctx,
                           "Argument %d of '%s': expected int or float, got %s at line %d",
                           i + 1, call->name.c_str(),
                           datatype_to_string(arg_type), call->loc.line);
              continue;
            }
          }

          if (!is_unknown(param_type) && !is_unknown(arg_type) &&
              !types_compatible(param_type, arg_type)) {
            report_error(ctx,
                         "Argument %d of '%s': expected %s, got %s at line %d",
                         i + 1, call->name.c_str(),
                         datatype_to_string(param_type),
                         datatype_to_string(arg_type), call->loc.line);
          }
        }
      }
    }

    DataType ret = function_return_type(ctx, effective_name);
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
    // Validate custom-typed declarations: `Point p;` / `Point p = ...;`
    // referencing a user-defined struct must point at a registered
    // type. Catches typos (`Pont p;`) at typecheck time before codegen
    // emits opaque "field not found" diagnostics. Skipped silently if
    // the type system runs in warning mode and the lookup fails — the
    // strict mode (Plan 03) is what turns this into an exit-blocking
    // error.
    if (decl->data_type == TYPE_CUSTOM && decl->custom_type.has_value() &&
        !ctx->struct_types.count(decl->custom_type.value())) {
      report_error(ctx, "Unknown type '%s' in declaration of '%s' at line %d",
                   decl->custom_type.value().c_str(), decl->name.c_str(),
                   decl->loc.line);
    }
    if (declared_type == TYPE_VOID && decl->initializer) {
      declared_type = infer_expr(ctx, decl->initializer.get());
    }
    if (decl->initializer) {
      DataType init_type = infer_expr(ctx, decl->initializer.get());
      // Skip the check when either side is unknown: TYPE_VOID often means
      // "expression returns from a built-in we haven't catalogued";
      // TYPE_CUSTOM means a user-declared struct whose field set typeinfer
      // doesn't track here, and TYPE_UNKNOWN is the explicit `var x = …`
      // / `degisken x = …` form. Conservative on purpose — a future PR
      // can tighten once the builtin catalogue and custom-type tracking
      // are richer.
      auto is_unknown = [](DataType t) {
        return t == TYPE_VOID || t == TYPE_UNKNOWN || t == TYPE_CUSTOM ||
               t == TYPE_JSON;
      };
      if (!is_unknown(declared_type) && !is_unknown(init_type) &&
          !types_compatible(declared_type, init_type)) {
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
    // See VariableDecl note above: don't flag against unknown-typed sides.
    auto is_unknown = [](DataType t) {
      return t == TYPE_VOID || t == TYPE_UNKNOWN || t == TYPE_CUSTOM;
    };
    if (!is_unknown(var_type) && !is_unknown(expr_type) &&
        !types_compatible(var_type, expr_type)) {
      report_error(ctx, "Type mismatch in assignment to '%s': expected %s, got %s at line %d",
                   assign->name.c_str(), datatype_to_string(var_type),
                   datatype_to_string(expr_type), assign->loc.line);
    }
    return;
  }

  if (const auto *ret = as_node<ReturnStatement>(stmt)) {
    if (ret->value) {
      DataType ret_type = infer_expr(ctx, ret->value.get());
      // Don't flag against unknown-typed return expressions.
      if (ctx->current_return_type != TYPE_VOID && ret_type != TYPE_VOID &&
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

  // For if/while/for-condition checks, treat TYPE_VOID as "unknown — we
  // don't have a return-type entry for whatever expression produced it"
  // and skip the check rather than emit a false positive. Conservative on
  // purpose; we'd rather miss a real bug than annoy users until typeinfer's
  // builtin catalogue is exhaustive.
  auto cond_acceptable = [](DataType t) {
    return t == TYPE_BOOL || t == TYPE_INT || t == TYPE_VOID;
  };

  if (const auto *if_stmt = as_node<IfStatement>(stmt)) {
    DataType cond_type = infer_expr(ctx, if_stmt->condition.get());
    if (!cond_acceptable(cond_type)) {
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
    if (!cond_acceptable(cond_type)) {
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
      if (!cond_acceptable(cond_type)) {
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

  // Expression-statement fallback: Tulpar doesn't have a dedicated AST node
  // for "expression used as a statement"; the parser just returns the bare
  // expression (FunctionCall, etc.). Without this fallback, top-level
  // function calls are never visited by infer_stmt and the new arg-count /
  // arg-type checks never run on them.
  infer_expr(ctx, stmt);
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
  case TYPE_JSON:
    return "json";
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
  ctx->warning_mode = false;
  ctx->source_path.clear();
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

// Pre-populate the function table with the language's built-in / runtime
// functions so calls like `len(42)` get the same arg-count + arg-type
// scrutiny as user-defined functions. Polymorphic positions (e.g. the
// argument to `len` — string OR array) use TYPE_UNKNOWN, which the
// FunctionCall checker treats as a wildcard so the count check still
// fires but the type check skips. Variadic / special-cased builtins
// (`print`, `println`, `call`) are intentionally absent — registering
// them with a fixed arity would false-positive valid programs. Keep
// this list ordered roughly by category for ease of audit.
static void register_builtin_signatures(TypeInferContext *ctx) {
  struct BuiltinSig {
    const char *name;
    DataType return_type;
    std::vector<DataType> params;
  };
  // NB: the AOT path's `lower_for_in_in_place` synthesises `length()` calls
  // on the C-bridge AST — those happen after our pre-pass on the std::variant
  // AST, so the synthetic calls never reach typeinfer.
  const BuiltinSig sigs[] = {
      // Time
      {"clock", TYPE_FLOAT, {}},
      {"clock_ms", TYPE_FLOAT, {}},
      // Collection / string size
      {"length", TYPE_INT, {TYPE_UNKNOWN}},
      {"len", TYPE_INT, {TYPE_UNKNOWN}},
      // Range / iteration
      {"range", TYPE_ARRAY_INT, {TYPE_INT}},
      // Object/json keys — returns a string array of field names.
      {"keys", TYPE_ARRAY_STR, {TYPE_UNKNOWN}},
      {"values", TYPE_ARRAY, {TYPE_UNKNOWN}},
      // Array mutation
      {"push", TYPE_VOID, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"pop", TYPE_UNKNOWN, {TYPE_UNKNOWN}},
      // env() — process env var lookup, "" when missing
      {"env", TYPE_STRING, {TYPE_STRING}},
      // call(name, ...) — handler dispatch by string. Args are variadic;
      // we still register it so length/arity isn't flagged.
      {"call", TYPE_UNKNOWN, {TYPE_STRING}},
      // Math (single float arg → float result)
      {"sqrt", TYPE_FLOAT, {TYPE_FLOAT}},
      {"sin", TYPE_FLOAT, {TYPE_FLOAT}},
      {"cos", TYPE_FLOAT, {TYPE_FLOAT}},
      {"tan", TYPE_FLOAT, {TYPE_FLOAT}},
      {"log", TYPE_FLOAT, {TYPE_FLOAT}},
      {"log10", TYPE_FLOAT, {TYPE_FLOAT}},
      {"exp", TYPE_FLOAT, {TYPE_FLOAT}},
      {"floor", TYPE_FLOAT, {TYPE_FLOAT}},
      {"ceil", TYPE_FLOAT, {TYPE_FLOAT}},
      // abs is int-or-float polymorphic — leave the arg open to avoid
      // false-positives on `abs(-3)`.
      {"abs", TYPE_UNKNOWN, {TYPE_UNKNOWN}},
      // Conversions (polymorphic input)
      {"toString", TYPE_STRING, {TYPE_UNKNOWN}},
      {"toJson", TYPE_STRING, {TYPE_UNKNOWN}},
      {"fromJson", TYPE_UNKNOWN, {TYPE_STRING}},
      {"toInt", TYPE_INT, {TYPE_UNKNOWN}},
      {"toFloat", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"toBool", TYPE_BOOL, {TYPE_UNKNOWN}},
      // I/O
      {"input", TYPE_STRING, {}},
      {"exit", TYPE_VOID, {TYPE_INT}},
      {"sleep", TYPE_VOID, {TYPE_INT}},
      // String utils
      {"split", TYPE_ARRAY_STR, {TYPE_STRING, TYPE_STRING}},
      {"replace", TYPE_STRING, {TYPE_STRING, TYPE_STRING, TYPE_STRING}},
      {"substring", TYPE_STRING, {TYPE_STRING, TYPE_INT, TYPE_INT}},
      {"indexOf", TYPE_INT, {TYPE_STRING, TYPE_STRING}},
      {"contains", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"startsWith", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"endsWith", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"trim", TYPE_STRING, {TYPE_STRING}},
      {"upper", TYPE_STRING, {TYPE_STRING}},
      {"toUpper", TYPE_STRING, {TYPE_STRING}},
      {"lower", TYPE_STRING, {TYPE_STRING}},
      {"toLower", TYPE_STRING, {TYPE_STRING}},
      // File I/O
      {"write_file", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"read_file", TYPE_STRING, {TYPE_STRING}},
      {"append_file", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"file_exists", TYPE_BOOL, {TYPE_STRING}},
      // Sockets — handles + buffers are opaque to typeinfer; we still
      // catch arg-count typos via the wildcard params.
      {"socket_server", TYPE_UNKNOWN, {TYPE_STRING, TYPE_INT}},
      {"socket_client", TYPE_UNKNOWN, {TYPE_STRING, TYPE_INT}},
      {"socket_accept", TYPE_UNKNOWN, {TYPE_UNKNOWN}},
      {"socket_send", TYPE_INT, {TYPE_UNKNOWN, TYPE_STRING}},
      {"socket_receive", TYPE_STRING, {TYPE_UNKNOWN, TYPE_INT}},
      {"socket_recv", TYPE_STRING, {TYPE_UNKNOWN, TYPE_INT}},
      {"socket_close", TYPE_VOID, {TYPE_UNKNOWN}},
      {"socket_select", TYPE_UNKNOWN, {TYPE_UNKNOWN, TYPE_INT}},
      // Threads
      {"thread_create", TYPE_UNKNOWN, {TYPE_STRING, TYPE_UNKNOWN}},
      // Database (SQLite)
      {"db_open", TYPE_UNKNOWN, {TYPE_STRING}},
      {"db_close", TYPE_VOID, {TYPE_UNKNOWN}},
      {"db_query", TYPE_UNKNOWN, {TYPE_UNKNOWN, TYPE_STRING}},
      // Array mutation — `push(arr, val)` accepts any value type.
      {"push", TYPE_VOID, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
  };
  for (const auto &s : sigs) {
    std::vector<DataType> ps = s.params;
    typeinfer_register_function(ctx, s.name, s.return_type,
                                ps.empty() ? nullptr : ps.data(),
                                static_cast<int>(ps.size()));
  }
}

void typeinfer_program(TypeInferContext *ctx, const ASTNode *program) {
  const auto *prog = as_node<Program>(program);
  if (!prog) {
    return;
  }

  // Builtins go in BEFORE walking the AST so user code that calls a
  // builtin from a top-level statement (no enclosing function) is still
  // scrutinised. User-defined functions then layer on top — if a user
  // happens to define `func len(...)`, their signature wins (last write).
  register_builtin_signatures(ctx);

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
    // Pre-scan struct declarations so `<TypeName> ident;` decls
    // anywhere in the program (even before the type's definition
    // textually) can validate against ctx->struct_types.
    if (const auto *type_decl = as_node<TypeDecl>(stmt.get())) {
      if (ctx->struct_types.count(type_decl->name)) {
        report_error(ctx, "Duplicate struct/type declaration '%s' at line %d",
                     type_decl->name.c_str(), type_decl->loc.line);
      } else {
        StructTypeInfo info;
        info.field_names = type_decl->field_names;
        info.field_types = type_decl->field_types;
        info.field_custom_types = type_decl->field_custom_types;
        ctx->struct_types[type_decl->name] = std::move(info);
      }
    }
  }

  for (const auto &stmt : prog->statements) {
    infer_stmt(ctx, stmt.get());
  }
}

// Tulpar Type Inference Module - Implementation
// Full static type inference for compile-time type checking

#include "typeinfer.hpp"
#include "../common/localization.hpp"
#include "../embedded_libs.h"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

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

// `==` / `!=` between two different scalar types can never be true: the
// runtime compares the value's type tag first, so `true == 1` is false and —
// the nastier half — `false != 0` is *true*. Nothing about that is visible at
// the call site, which is how `assert(x < y, ...)` silently never failed for
// the entire life of lib/test.tpr: the parameter was declared `int`, the
// caller passed a bool, and `cond == 0` could not hold. Flagging the constant
// comparison is what turns that whole bug family into a compile-time error.
//
// Deliberately conservative, in the same spirit as `cond_acceptable` below:
// only the four scalars are judged, int/float stay mutually comparable
// (numeric promotion makes `1 == 1.0` genuinely true), and anything dynamic
// or unknown (json, structs, arrays, VOID = "no signature entry") is skipped
// rather than guessed at.
static bool is_comparable_scalar(DataType t) {
  return t == TYPE_INT || t == TYPE_FLOAT || t == TYPE_STRING || t == TYPE_BOOL;
}

// `int x = <bool>` and `x = <bool>` are a supported, tested coercion: the AOT
// backend rewrites the VMValue tag from VM_VAL_BOOL to VM_VAL_INT at the local
// store (llvm_backend.cpp's AST_VARIABLE_DECL boxed-local fallback, guarded by
// tests/bool_to_int_coerce.test.tpr — slot 2 already holds 1/0, so it is
// lossless). No such rewrite happens at a CALL boundary, which is exactly why
// `assert(x < y, msg)` against an `int cond` parameter silently never fired.
// So this allowance is deliberately scoped to stores; the argument check keeps
// rejecting the same pair, because there the value really does stay a bool.
static bool store_coercible(DataType declared, DataType initializer) {
  return declared == TYPE_INT && initializer == TYPE_BOOL;
}

static bool comparison_is_constant(DataType a, DataType b) {
  if (!is_comparable_scalar(a) || !is_comparable_scalar(b)) {
    return false;
  }
  if (a == b) {
    return false;
  }
  bool a_num = (a == TYPE_INT || a == TYPE_FLOAT);
  bool b_num = (b == TYPE_INT || b == TYPE_FLOAT);
  return !(a_num && b_num);
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
  if (as_node<NullLiteral>(expr)) {
    return TYPE_VOID;
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

  if (const auto *lam = as_node<LambdaExpr>(expr)) {
    auto old_symbols = ctx->symbols;
    auto old_ret = ctx->current_return_type;
    ctx->current_return_type = TYPE_UNKNOWN;
    for (const auto &param : lam->parameters) {
      TypeSymbol sym = { param.type, param.custom_type, true, false };
      ctx->symbols[param.name] = sym;
    }
    if (lam->body) {
      if (as_node<Block>(lam->body.get())) {
        infer_stmt(ctx, lam->body.get());
      } else {
        infer_expr(ctx, lam->body.get());
      }
    }
    ctx->symbols = old_symbols;
    ctx->current_return_type = old_ret;
    return TYPE_UNKNOWN;
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
      if (comparison_is_constant(left_type, right_type)) {
        report_error(ctx,
                     tulpar::i18n::tr_en(
                         "'%s' ile '%s' karsilastirmasi her zaman %s "
                         "(farkli tipler asla esit olmaz) - satir %d",
                         "comparison between '%s' and '%s' is always %s "
                         "(different types are never equal) at line %d"),
                     datatype_to_string(left_type),
                     datatype_to_string(right_type),
                     bin->op == TOKEN_EQUAL ? "false" : "true", bin->loc.line);
      }
      return TYPE_BOOL;
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

  if (const auto *tern = as_node<TernaryOp>(expr)) {
    // Walk the condition for coverage, then unify the two branch types. When
    // both branches agree, that's the ternary's type; otherwise fall back to
    // numeric promotion (int/float) and, failing that, the then-branch type.
    infer_expr(ctx, tern->condition.get());
    DataType then_type = infer_expr(ctx, tern->then_branch.get());
    DataType else_type = infer_expr(ctx, tern->else_branch.get());
    if (then_type == else_type) {
      return then_type;
    }
    return promote_types(then_type, else_type);
  }

  if (const auto *un = as_node<UnaryOp>(expr)) {
    DataType operand_type = infer_expr(ctx, un->operand.get());
    if (un->op == TOKEN_BANG) {
      return TYPE_BOOL;
    }
    return operand_type;
  }

  if (const auto *call = as_node<FunctionCall>(expr)) {
    if (call->callee) {
      infer_expr(ctx, call->callee.get());
      for (const auto &arg : call->arguments) {
        infer_expr(ctx, arg.get());
      }
      return TYPE_UNKNOWN;
    }

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
      // Too MANY args is an error; too FEW is allowed — the codegen pads the
      // missing trailing slots with a default (0), which is what lets
      // `serve()` stand in for `serve(<default port>)`.
      // `call(name, ...)` is genuinely variadic (it forwards a callee's args by
      // name), so registering it with one param must not flag the extra args.
      const bool is_variadic_builtin = (effective_name == "call");
      if (got > expected && !is_variadic_builtin) {
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
          // `length(json)` returns the key count (matches Python's
          // `len({})`, JS's `Object.keys(o).length`). Originally `json`
          // was rejected here because the runtime returned 0; now both
          // VM and AOT count keys, so accept json as a collection too.
          return t == TYPE_STRING || t == TYPE_ARRAY ||
                 t == TYPE_ARRAY_INT || t == TYPE_ARRAY_FLOAT ||
                 t == TYPE_ARRAY_STR || t == TYPE_ARRAY_BOOL ||
                 t == TYPE_ARRAY_JSON || t == TYPE_JSON;
        };
        auto is_numeric = [](DataType t) {
          return t == TYPE_INT || t == TYPE_FLOAT;
        };
        const bool poly_collection =
            (call->name == "len" || call->name == "length");
        const bool poly_numeric = (call->name == "abs");

        // Bounded by `got`: with default-arg padding `got` may be < expected,
        // and arg_types only holds the supplied args.
        for (int i = 0; i < got && i < expected; ++i) {
          DataType param_type = sig.param_types[i];
          DataType arg_type = arg_types[i];

          // Polymorphic position with concrete arg → use category check
          // instead of the wildcard-skip default.
          if (param_type == TYPE_UNKNOWN && i == 0 && !is_unknown(arg_type)) {
            if (poly_collection && !is_collection(arg_type)) {
              report_error(ctx,
                           "Argument %d of '%s': expected string, array or json, got %s at line %d",
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
        !ctx->struct_types.count(decl->custom_type.value()) &&
        !ctx->has_imports) {
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
          !store_coercible(declared_type, init_type) &&
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
        !store_coercible(var_type, expr_type) &&
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
      if (ctx->current_return_type != TYPE_VOID && ctx->current_return_type != TYPE_UNKNOWN &&
          ret_type != TYPE_VOID && ret_type != TYPE_UNKNOWN &&
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
  // TYPE_UNKNOWN is an untyped parameter (`func f(on) { if (on) ... }`, the
  // house style for every on/off toggle in scene3d/arcade/wings) and TYPE_JSON
  // is the dynamic type whose truthiness is only knowable at runtime. Both are
  // "no information", same as VOID, so warning on them is a false positive by
  // construction — `if (on)` is the correct way to read a flag, and the whole
  // point of the assert fix was that `on == 1` is the broken one.
  auto cond_acceptable = [](DataType t) {
    return t == TYPE_BOOL || t == TYPE_INT || t == TYPE_VOID ||
           t == TYPE_UNKNOWN || t == TYPE_JSON;
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
  // `json` is Tulpar's dynamic type: a json value holds any runtime value, and
  // any value flows into a json slot. Declaring it compatible in both
  // directions is what makes signatures like `assert_eq_str(json, json)` — the
  // deliberate "accept anything, stringify it" idiom in lib/test.tpr — mean
  // what they say. Nothing exercised this until imported signatures started
  // being checked; before that, json parameters were essentially all in
  // modules typeinfer never opened.
  if (a == TYPE_JSON || b == TYPE_JSON) {
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
      // --- Matematik: codegen'de VARDI ama tabloda YOKTU ---
      // Tabloda olmayan bir builtin HİÇ denetlenmiyor: `str s = pow("a","b")`
      // sessizce geçiyordu (dönüş VOID sayıldığı için sonraki denetimler de
      // atlanıyordu). Dönüş tipleri runtime_bindings.cpp'den OKUNDU, tahmin
      // edilmedi — hepsi VM_FLOAT döndürüyor.
      {"acos", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"asin", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"atan", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"cbrt", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"cosh", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"sinh", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"tanh", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"log2", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"round", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"trunc", TYPE_FLOAT, {TYPE_UNKNOWN}},
      {"atan2", TYPE_FLOAT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"hypot", TYPE_FLOAT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"fmod", TYPE_FLOAT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"pow", TYPE_FLOAT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"clock", TYPE_FLOAT, {}},
      {"clock_ms", TYPE_FLOAT, {}},
      // Collection / string size
      {"length", TYPE_INT, {TYPE_UNKNOWN}},
      {"len", TYPE_INT, {TYPE_UNKNOWN}},
      // Range / iteration
      {"range", TYPE_ARRAY_INT, {TYPE_INT}},
      // Object/json keys — returns a string array of field names.
      {"keys", TYPE_ARRAY_STR, {TYPE_UNKNOWN}},
      {"args", TYPE_ARRAY_STR, {}},
      {"values", TYPE_ARRAY, {TYPE_UNKNOWN}},
      // clone(obj) — shallow copy. Underlies the VM's typed-struct
      // pass-by-value prologue; surfaced as a user-callable builtin.
      {"clone", TYPE_UNKNOWN, {TYPE_UNKNOWN}},
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
      {"read_key", TYPE_STRING, {}},
      {"sys_lang", TYPE_STRING, {}},
      {"read_key_timeout", TYPE_STRING, {TYPE_INT}},
      {"term_width", TYPE_INT, {}},
      {"term_height", TYPE_INT, {}},
      {"display_width", TYPE_INT, {TYPE_STRING}},
      {"fit_width", TYPE_STRING, {TYPE_STRING, TYPE_INT}},
      {"screen_open", TYPE_VOID, {}},
      {"screen_close", TYPE_VOID, {}},
      {"screen_render", TYPE_VOID, {TYPE_STRING}},
      {"style", TYPE_STRING, {TYPE_STRING, TYPE_STRING}},
      {"exit", TYPE_VOID, {TYPE_INT}},
      {"sleep", TYPE_VOID, {TYPE_INT}},
      {"sleep_async", TYPE_JSON, {TYPE_INT}},
      // String utils
      {"split", TYPE_ARRAY_STR, {TYPE_STRING, TYPE_STRING}},
      {"replace", TYPE_STRING, {TYPE_STRING, TYPE_STRING, TYPE_STRING}},
      {"substring", TYPE_STRING, {TYPE_STRING, TYPE_INT, TYPE_INT}},
      {"ord", TYPE_INT, {TYPE_STRING, TYPE_INT}},
      {"chr", TYPE_STRING, {TYPE_INT}},
      // tame (2D oyun) builtin ailesi — import "tame" sarmalayıcılarının
      // altındaki tm_* native katmanı. Koordinat pozisyonları int VEYA float
      // kabul eder (oyunlar `x + dx` float'larıyla literal int'leri serbestçe
      // karıştırır) → TYPE_UNKNOWN; renk her zaman paketlenmiş int
      // (0xRRGGBBAA, bkz. lib/tame.tpr rgb()/rgba()).
      {"tm_window", TYPE_BOOL, {TYPE_INT, TYPE_INT, TYPE_STRING}},
      {"tm_running", TYPE_BOOL, {}},
      {"tm_close", TYPE_VOID, {}},
      {"tm_set_fps", TYPE_VOID, {TYPE_INT}},
      {"tm_begin", TYPE_VOID, {}},
      {"tm_end", TYPE_VOID, {}},
      {"tm_fps", TYPE_INT, {}},
      {"tm_frame_time", TYPE_FLOAT, {}},
      {"tm_view_left", TYPE_FLOAT, {}},
      {"tm_view_right", TYPE_FLOAT, {}},
      {"tm_view_top", TYPE_FLOAT, {}},
      {"tm_view_bottom", TYPE_FLOAT, {}},
      {"tm_accel_x", TYPE_FLOAT, {}},
      {"tm_accel_y", TYPE_FLOAT, {}},
      {"tm_accel_z", TYPE_FLOAT, {}},
      {"tm_accel_available", TYPE_BOOL, {}},
      {"tm_active", TYPE_BOOL, {}},
      {"tm_beep", TYPE_VOID, {TYPE_FLOAT, TYPE_INT}},
      {"tm_tone", TYPE_VOID, {TYPE_FLOAT, TYPE_INT, TYPE_FLOAT}},
      {"tm_time", TYPE_FLOAT, {}},
      {"tm_width", TYPE_INT, {}},
      {"tm_height", TYPE_INT, {}},
      {"tm_clear", TYPE_VOID, {TYPE_INT}},
      {"tm_rect", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm_rect_lines", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm_circle", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm_line", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm_pixel", TYPE_VOID, {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm_text", TYPE_VOID,
       {TYPE_STRING, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT, TYPE_INT}},
      // Tuş argümanı ad ("W") VEYA ham raylib kodu (87) olabilir — bkz.
      // aot_tm_key_down_ptr; tek tip dayatmak kod veren kütüphaneleri
      // (scene3d'nin K_* sabitleri) yanlış uyarır.
      {"tm_key_down", TYPE_BOOL, {TYPE_UNKNOWN}},
      {"tm_key_pressed", TYPE_BOOL, {TYPE_UNKNOWN}},
      {"tm_text_width", TYPE_INT, {TYPE_STRING, TYPE_INT}},
      {"tm_font_width", TYPE_INT, {TYPE_INT, TYPE_STRING, TYPE_INT}},
      {"tm_scissor", TYPE_VOID, {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT}},
      {"tm_scissor_end", TYPE_VOID, {}},
      {"tm_rt_new", TYPE_INT, {TYPE_INT, TYPE_INT}},
      {"tm_rt_free", TYPE_VOID, {TYPE_INT}},
      {"tm_rt_w", TYPE_INT, {TYPE_INT}},
      {"tm_rt_h", TYPE_INT, {TYPE_INT}},
      {"tm_rt_begin", TYPE_VOID, {TYPE_INT}},
      {"tm_rt_end", TYPE_VOID, {}},
      {"tm_rt_draw", TYPE_VOID, {TYPE_INT, TYPE_INT, TYPE_INT}},
      {"tm_char_pressed", TYPE_INT, {}},
      {"tm_key_released", TYPE_BOOL, {TYPE_UNKNOWN}},
      {"tm_mouse_x", TYPE_INT, {}},
      {"tm_mouse_y", TYPE_INT, {}},
      {"tm_mouse_down", TYPE_BOOL, {TYPE_INT}},
      {"tm_mouse_pressed", TYPE_BOOL, {TYPE_INT}},
      {"tm_mouse_wheel", TYPE_FLOAT, {}},
      {"tm_mouse_dx", TYPE_FLOAT, {}},
      {"tm_mouse_dy", TYPE_FLOAT, {}},
      {"tm_cursor_lock", TYPE_VOID, {TYPE_INT}},
      {"tm_exit_key", TYPE_VOID, {TYPE_INT}},
      {"tm_cursor_locked", TYPE_BOOL, {}},
      {"tm_touch_count", TYPE_INT, {}},
      {"tm_touch_x", TYPE_INT, {TYPE_INT}},
      {"tm_touch_y", TYPE_INT, {TYPE_INT}},
      // tame Faz 3-4: kaynak handle'ları int'tir (-1 = yükleme başarısız).
      {"tm_load_texture", TYPE_INT, {TYPE_STRING}},
      {"tm_draw_texture", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm_draw_texture_ex", TYPE_VOID,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm_texture_width", TYPE_INT, {TYPE_INT}},
      {"tm_texture_height", TYPE_INT, {TYPE_INT}},
      {"tm_unload_texture", TYPE_VOID, {TYPE_INT}},
      {"tm_load_font", TYPE_INT, {TYPE_STRING, TYPE_INT}},
      {"tm_text_font", TYPE_VOID,
       {TYPE_INT, TYPE_STRING, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT,
        TYPE_INT}},
      {"tm_measure_text", TYPE_INT, {TYPE_STRING, TYPE_INT}},
      {"tm_load_sound", TYPE_INT, {TYPE_STRING}},
      {"tm_play_sound", TYPE_VOID, {TYPE_INT}},
      {"tm_stop_sound", TYPE_VOID, {TYPE_INT}},
      {"tm_sound_volume", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN}},
      {"tm_load_music", TYPE_INT, {TYPE_STRING}},
      {"tm_play_music", TYPE_VOID, {TYPE_INT}},
      {"tm_stop_music", TYPE_VOID, {TYPE_INT}},
      {"tm_music_volume", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN}},
      {"tm_triangle", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm_screenshot", TYPE_VOID, {TYPE_STRING}},
      {"tm3_camera", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_begin", TYPE_VOID, {}},
      {"tm3_end", TYPE_VOID, {}},
      {"tm3_cube", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_cube_wires", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_grid", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN}},
      {"tm3_sphere", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_sphere_wires", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT,
        TYPE_INT}},
      {"tm3_cylinder", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_INT}},
      {"tm3_plane", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_INT}},
      {"tm3_line", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_pick_box", TYPE_FLOAT,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_pick_sphere", TYPE_FLOAT,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN}},
      {"tm3_load_model", TYPE_INT, {TYPE_STRING}},
      {"tm3_gen", TYPE_INT,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_draw_model", TYPE_VOID,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_INT}},
      {"tm3_draw_model_rot", TYPE_VOID,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_model_texture", TYPE_VOID, {TYPE_INT, TYPE_INT}},
      {"tm3_anim_count", TYPE_INT, {TYPE_INT}},
      {"tm3_anim_frames", TYPE_INT, {TYPE_INT, TYPE_INT}},
      {"tm3_anim", TYPE_VOID, {TYPE_INT, TYPE_INT, TYPE_INT}},
      {"tm3_anim_blend", TYPE_VOID, {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_FLOAT}},
      {"tm3_unload_model", TYPE_VOID, {TYPE_INT}},
      {"tm3_lights", TYPE_BOOL, {TYPE_INT}},
      {"tm3_light_set", TYPE_VOID,
       {TYPE_INT, TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_INT}},
      {"tm3_light_off", TYPE_VOID, {TYPE_INT}},
      {"tm3_ambient", TYPE_VOID, {TYPE_INT}},
      {"tm3_shadows", TYPE_BOOL, {TYPE_INT}},
      {"tm3_shadow_area", TYPE_VOID, {TYPE_UNKNOWN}},
      {"tm3_shadows_active", TYPE_BOOL, {}},
      {"tm3_texture", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_billboard", TYPE_VOID,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_INT}},
      {"tm3_screen_x", TYPE_FLOAT,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_screen_y", TYPE_FLOAT,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_terrain_gen", TYPE_INT,
       {TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_terrain_load", TYPE_INT,
       {TYPE_STRING, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_terrain_height", TYPE_FLOAT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_terrain_off", TYPE_VOID, {}},
      {"tm3_cube_rot", TYPE_VOID,
       {TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN, TYPE_UNKNOWN, TYPE_INT}},
      {"tm3_sky_stars", TYPE_VOID, {TYPE_UNKNOWN}},
      {"tm3_sky_clouds", TYPE_VOID, {TYPE_FLOAT}},
      {"tm3_terrain_layer", TYPE_INT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_terrain_layers", TYPE_VOID,
       {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_UNKNOWN, TYPE_UNKNOWN,
        TYPE_UNKNOWN}},
      {"tm3_terrain_layers_off", TYPE_VOID, {}},
      {"tm3_material", TYPE_VOID, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"tm3_sky", TYPE_BOOL, {TYPE_INT, TYPE_INT}},
      {"tm3_sky_off", TYPE_VOID, {}},
      {"tm3_fog", TYPE_VOID, {TYPE_INT, TYPE_UNKNOWN}},
      {"tm_checker", TYPE_INT, {TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT, TYPE_INT}},
      {"tm_gamepad_available", TYPE_BOOL, {TYPE_INT}},
      {"tm_gamepad_name", TYPE_STRING, {TYPE_INT}},
      {"tm_gamepad_down", TYPE_BOOL, {TYPE_INT, TYPE_STRING}},
      {"tm_gamepad_pressed", TYPE_BOOL, {TYPE_INT, TYPE_STRING}},
      {"tm_gamepad_axis", TYPE_FLOAT, {TYPE_INT, TYPE_STRING}},
      // Kalıcı kayıt (Android: internal storage; masaüstü: CWD) + titreşim.
      {"tm_save_data", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"tm_load_data", TYPE_STRING, {TYPE_STRING}},
      {"tm_download", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"tm_is_web", TYPE_BOOL, {}},
      {"tm_vibrate", TYPE_VOID, {TYPE_INT}},
      // Polymorphic: string haystack (substring) OR array haystack (membership).
      {"indexOf", TYPE_INT, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
      {"contains", TYPE_BOOL, {TYPE_UNKNOWN, TYPE_UNKNOWN}},
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
      // System / process
      {"sys_run", TYPE_INT, {TYPE_STRING}},
      // Regex (std::regex ECMAScript; match = full-string, search = substring)
      {"regex_match", TYPE_INT, {TYPE_STRING, TYPE_STRING}},
      {"regex_search", TYPE_INT, {TYPE_STRING, TYPE_STRING}},
      {"regex_capture", TYPE_ARRAY, {TYPE_STRING, TYPE_STRING}},
      {"regex_replace", TYPE_STRING, {TYPE_STRING, TYPE_STRING, TYPE_STRING}},
      // Crypto / content integrity
      {"sha256", TYPE_STRING, {TYPE_STRING}},
      // Password KDF (PBKDF2-HMAC-SHA256)
      {"password_hash", TYPE_STRING, {TYPE_STRING}},
      {"password_verify", TYPE_BOOL, {TYPE_STRING, TYPE_STRING}},
      {"hmac_sha256", TYPE_STRING, {TYPE_STRING, TYPE_STRING}},
      {"secure_token", TYPE_STRING, {TYPE_INT}},
      {"gzip_compress", TYPE_STRING, {TYPE_STRING}},
      // Sockets — handles + buffers are opaque to typeinfer; we still
      // catch arg-count typos via the wildcard params.
      {"socket_server", TYPE_UNKNOWN, {TYPE_STRING, TYPE_INT}},
      {"socket_client", TYPE_UNKNOWN, {TYPE_STRING, TYPE_INT}},
      {"socket_accept", TYPE_UNKNOWN, {TYPE_UNKNOWN}},
      {"socket_send", TYPE_INT, {TYPE_UNKNOWN, TYPE_STRING}},
      {"socket_receive", TYPE_STRING, {TYPE_UNKNOWN, TYPE_INT}},
      {"socket_recv", TYPE_STRING, {TYPE_UNKNOWN, TYPE_INT}},
      {"socket_close", TYPE_VOID, {TYPE_UNKNOWN}},
      {"socket_peer_ip", TYPE_STRING, {TYPE_UNKNOWN}},
      {"socket_select", TYPE_UNKNOWN, {TYPE_UNKNOWN, TYPE_INT}},
      // Threads
      {"thread_create", TYPE_UNKNOWN, {TYPE_STRING, TYPE_UNKNOWN}},
      // HTTP helpers
      {"parse_query", TYPE_UNKNOWN, {TYPE_STRING}},
      {"parse_cookies", TYPE_UNKNOWN, {TYPE_STRING}},
      // Database (SQLite)
      {"db_open", TYPE_UNKNOWN, {TYPE_STRING}},
      {"db_close", TYPE_VOID, {TYPE_UNKNOWN}},
      // 3rd param (optional bound-params array) is wildcard; too-few-args is
      // allowed, so both db_query(db,sql) and db_query(db,sql,params) pass.
      {"db_query", TYPE_UNKNOWN, {TYPE_UNKNOWN, TYPE_STRING, TYPE_UNKNOWN}},
      {"db_execute", TYPE_BOOL, {TYPE_UNKNOWN, TYPE_STRING, TYPE_UNKNOWN}},
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

// ---------------------------------------------------------------------------
// Import resolution
//
// Until now typeinfer never opened a module's source, so every call into the
// stdlib was unchecked. That is precisely how `assert(x < y, msg)` survived:
// the argument checker below would have rejected `bool` against `assert`'s
// declared `int cond` on sight, but it never saw the signature. Pulling the
// exported signatures in closes the hole for `test`, `wings`, `router`, `orm`,
// `scene3d`, `arcade` — every module — at once.
//
// Only SIGNATURES are imported (functions + struct layouts), never the module
// body: type-checking stdlib internals on every user build would be both slow
// and noisy, and the module's own diagnostics belong to whoever edits it.
// ---------------------------------------------------------------------------

// Same resolution order as the AOT backend (`src/aot/llvm_backend.cpp`):
// embedded stdlib → literal path → `<name>.tpr` → the two `tulpar_modules/`
// slots that `tulpar pkg install` populates. Returns false when nothing
// resolves; that stays silent here on purpose, because the AOT path owns the
// "Could not import file" error and we must not double-report it.
static bool load_import_source(const std::string &name, std::string &out) {
  if (const char *embedded = get_embedded_lib(name.c_str())) {
    out = embedded;
    return true;
  }
  const std::string candidates[] = {
      name,
      name + ".tpr",
      "tulpar_modules/" + name + "/" + name + ".tpr",
      "tulpar_modules/" + name + ".tpr",
  };
  for (const auto &path : candidates) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      continue;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
  }
  return false;
}

static std::unique_ptr<ASTNode> parse_module_source(const std::string &source) {
  try {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
      Token tok = lexer.next_token();
      bool eof = tok.type() == TOKEN_EOF;
      tokens.push_back(std::move(tok));
      if (eof) {
        break;
      }
    }
    Parser parser(std::move(tokens));
    return parser.parse();
  } catch (...) {
    // A module that won't parse is not this pass's problem to report.
    return nullptr;
  }
}

// Register everything `import "<name>"` brings into scope. Existing entries are
// never overwritten, which gives the precedence we want without any extra
// bookkeeping: builtins beat modules (their hand-written signatures are more
// precise than a wrapper's), and the importing file's own declarations were
// registered first, so local definitions beat both.
static void register_module_exports(TypeInferContext *ctx,
                                    const std::string &module_name,
                                    const std::string &alias,
                                    std::set<std::string> &visited, int depth) {
  // Depth cap and visited set together make an import cycle terminate; the
  // AOT path guards this separately, so hitting either here is silent.
  if (depth > 8 || !visited.insert(module_name).second) {
    return;
  }
  std::string source;
  if (!load_import_source(module_name, source)) {
    return;
  }
  std::unique_ptr<ASTNode> module_ast = parse_module_source(source);
  if (!module_ast) {
    return;
  }
  const auto *module_prog = as_node<Program>(module_ast.get());
  if (!module_prog) {
    return;
  }

  for (const auto &stmt : module_prog->statements) {
    if (const auto *func = as_node<FunctionDecl>(stmt.get())) {
      // `import "m" as a` mangles the module's top-level functions to
      // `a__<name>` (src/parser/import_alias.cpp); mirror that here or the
      // aliased call sites would look undefined.
      std::string name = alias.empty() ? func->name : alias + "__" + func->name;
      if (ctx->functions.count(name)) {
        continue;
      }
      std::vector<DataType> param_types;
      param_types.reserve(func->parameters.size());
      for (const auto &param : func->parameters) {
        param_types.push_back(param.type);
      }
      typeinfer_register_function(
          ctx, name.c_str(), func->return_type,
          param_types.empty() ? nullptr : param_types.data(),
          static_cast<int>(param_types.size()));
      continue;
    }
    if (const auto *type_decl = as_node<TypeDecl>(stmt.get())) {
      if (ctx->struct_types.count(type_decl->name)) {
        continue;
      }
      StructTypeInfo info;
      info.field_names = type_decl->field_names;
      info.field_types = type_decl->field_types;
      info.field_custom_types = type_decl->field_custom_types;
      ctx->struct_types[type_decl->name] = std::move(info);
      continue;
    }
    // A module's own imports are transitive for the plain (unaliased) form —
    // `import "wings"` puts http_utils' helpers in scope too, matching how the
    // AOT path flattens them into one namespace.
    if (const auto *nested = as_node<ImportStatement>(stmt.get())) {
      register_module_exports(ctx, nested->path, nested->alias, visited,
                              depth + 1);
    }
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
    // Programda import varsa, yerel olmayan custom-type'lar için "Unknown type"
    // uyarısını bastır (tip import edilen modülden gelmiş olabilir; typeinfer
    // modül kaynağını parse etmediğinden struct'ını göremez).
    if (as_node<ImportStatement>(stmt.get())) {
      ctx->has_imports = true;
    }
  }

  // Imported signatures come in AFTER the local pre-scan so this file's own
  // declarations always win a name clash, and the gap-filling in
  // register_module_exports needs no special case for it.
  if (ctx->has_imports) {
    std::set<std::string> visited;
    for (const auto &stmt : prog->statements) {
      if (const auto *imp = as_node<ImportStatement>(stmt.get())) {
        register_module_exports(ctx, imp->path, imp->alias, visited, 0);
      }
    }
  }

  for (const auto &stmt : prog->statements) {
    infer_stmt(ctx, stmt.get());
  }
}

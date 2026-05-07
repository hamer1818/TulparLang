// See typeinfer_warn.hpp for the rationale.

#include "typeinfer_warn.hpp"

#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "typeinfer.hpp"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <utility>
#include <vector>

namespace tulpar {

static bool typecheck_disabled_via_env() {
  const char *v = std::getenv("TULPAR_NO_TYPECHECK");
  return v && *v && std::strcmp(v, "0") != 0;
}

int typeinfer_emit_warnings(const char *source, const char *source_filename) {
  if (!source) return 0;
  if (typecheck_disabled_via_env()) return 0;

  // We deliberately use the C++ Lexer/Parser here (not the C-bridge
  // Parser_C the AOT/VM pipeline uses) because typeinfer is written
  // against the std::variant ASTNode produced by Parser. Parsing twice
  // costs microseconds on typical user files and keeps this module a
  // pure additive overlay — no AST refactor required.
  Lexer lexer(source);
  std::vector<Token> tokens;
  while (true) {
    Token tok = lexer.next_token();
    bool eof = tok.type() == TOKEN_EOF;
    tokens.push_back(std::move(tok));
    if (eof) break;
  }

  std::unique_ptr<ASTNode> ast;
  parser_set_quiet(1);  // Suppress pretty-render — the AOT path re-parses
                        // and emits the same diagnostics with proper
                        // filename context.
  try {
    Parser parser(std::move(tokens));
    ast = parser.parse();
  } catch (const std::exception &) {
    // The real compile path will surface the parse error with proper
    // diagnostics — stay silent here so we don't double-report.
    parser_set_quiet(0);
    return 0;
  }
  parser_set_quiet(0);
  if (!ast) return 0;
  // If the parser recovered through one or more errors, the AST has
  // gaps (skipped statements) and typeinfer running on it would
  // produce noisy `[typecheck]` warnings about phantom undefined
  // names. The AOT pipeline re-parses and prints the parse errors
  // properly anyway; bail silently here.
  if (parser_get_error_count() > 0) return 0;

  TypeInferContext *ctx = typeinfer_create();
  ctx->warning_mode = true;
  if (source_filename) ctx->source_path = source_filename;
  typeinfer_program(ctx, ast.get());
  int count = ctx->error_count;
  typeinfer_destroy(ctx);
  return count;
}

}  // namespace tulpar

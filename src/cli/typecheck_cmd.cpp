// `tulpar typecheck <file.tpr>` — surfaces the (otherwise unused) typeinfer
// module as a stand-alone tool. Build/run pipelines do NOT call typeinfer
// today; this subcommand lets authors and CI run the checker explicitly,
// review the noise level, and shape the rules before we wire it into the
// default path.

#include "typecheck_cmd.hpp"

#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../typeinfer/typeinfer.hpp"

#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace tulpar {

int typecheck_cmd_main(int argc, char **argv) {
  // argv layout: argv[0]="tulpar", argv[1]="typecheck", argv[2..]=args.
  const char *path = nullptr;
  for (int i = 2; i < argc; i++) {
    if (argv[i][0] == '-') {
      std::fprintf(stderr, "tulpar typecheck: unknown flag '%s'\n", argv[i]);
      return 2;
    }
    if (path) {
      std::fprintf(stderr,
                   "tulpar typecheck: only one file at a time (got '%s' and '%s')\n",
                   path, argv[i]);
      return 2;
    }
    path = argv[i];
  }
  if (!path) {
    std::fprintf(stderr, "Usage: tulpar typecheck <file.tpr>\n");
    return 2;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "tulpar typecheck: cannot open '%s'\n", path);
    return 2;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string source = ss.str();
  in.close();

  // Lex into a token vector. We use the C++ Lexer/Parser path directly so
  // we get the modern AST (std::variant) — the same input typeinfer expects.
  Lexer lexer(source);
  std::vector<Token> tokens;
  while (true) {
    Token tok = lexer.next_token();
    bool eof = tok.type() == TOKEN_EOF;
    tokens.push_back(std::move(tok));
    if (eof) break;
  }

  std::unique_ptr<ASTNode> ast;
  try {
    Parser parser(std::move(tokens));
    ast = parser.parse();
  } catch (const std::exception &e) {
    // Parser::error already printed a Rust-style diagnostic to stderr.
    return 2;
  }
  if (!ast) {
    std::fprintf(stderr, "tulpar typecheck: parse failed\n");
    return 2;
  }

  TypeInferContext *ctx = typeinfer_create();
  typeinfer_program(ctx, ast.get());
  bool had_errors = typeinfer_has_errors(ctx) != 0;
  int err_count = ctx->error_count;
  typeinfer_destroy(ctx);

  if (had_errors) {
    std::fprintf(stderr, "tulpar typecheck: %d type issue(s) in %s\n",
                 err_count, path);
    return 1;
  }
  std::fprintf(stderr, "tulpar typecheck: ok (%s)\n", path);
  return 0;
}

}  // namespace tulpar

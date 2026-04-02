#ifndef TULPAR_PARSER_HPP
#define TULPAR_PARSER_HPP

#include "../lexer/lexer.hpp"
#include "ast_nodes.hpp"
#include <vector>
#include <memory>
#include <stdexcept>

// ============================================================================
// Modern C++ Parser Class
// ============================================================================

class Parser {
private:
    std::vector<Token> tokens_;
    size_t position_;
    
    // Helper methods
    const Token& current() const;
    const Token& peek(int offset = 1) const;
    void advance();
    bool match(TulparTokenType type);
    bool check(TulparTokenType type) const;
    Token expect(TulparTokenType type, const std::string& error_msg);
    bool is_at_end() const;
    
    // Precedence-based parsing
    int get_precedence(TulparTokenType op) const;
    
    // Expression parsing (recursive descent)
    std::unique_ptr<ASTNode> parse_expression(int precedence = 0);
    std::unique_ptr<ASTNode> parse_primary();
    std::unique_ptr<ASTNode> parse_binary_op(std::unique_ptr<ASTNode> left, int precedence);
    std::unique_ptr<ASTNode> parse_unary();
    std::unique_ptr<ASTNode> parse_postfix(std::unique_ptr<ASTNode> expr);
    std::unique_ptr<ASTNode> parse_call(std::unique_ptr<ASTNode> callee);
    std::unique_ptr<ASTNode> parse_array_access(std::unique_ptr<ASTNode> object);
    std::unique_ptr<ASTNode> parse_array_literal();
    std::unique_ptr<ASTNode> parse_object_literal();
    
    // Statement parsing
    std::unique_ptr<ASTNode> parse_statement();
    std::unique_ptr<ASTNode> parse_variable_decl();
    std::unique_ptr<ASTNode> parse_function_decl();
    std::unique_ptr<ASTNode> parse_type_decl();
    std::unique_ptr<ASTNode> parse_if_statement();
    std::unique_ptr<ASTNode> parse_while_loop();
    std::unique_ptr<ASTNode> parse_for_loop();
    std::unique_ptr<ASTNode> parse_for_in_loop();
    std::unique_ptr<ASTNode> parse_return_statement();
    std::unique_ptr<ASTNode> parse_break_statement();
    std::unique_ptr<ASTNode> parse_continue_statement();
    std::unique_ptr<ASTNode> parse_try_catch();
    std::unique_ptr<ASTNode> parse_throw_statement();
    std::unique_ptr<ASTNode> parse_import_statement();
    std::unique_ptr<ASTNode> parse_block();
    std::unique_ptr<ASTNode> parse_expression_statement();
    
    // Type parsing
    DataType parse_type();
    std::string parse_custom_type_name();
    
    // Error handling
    void error(const std::string& message);

public:
    // Constructor
    explicit Parser(std::vector<Token> tokens);
    
    // Main parsing method - returns Program node
    std::unique_ptr<ASTNode> parse();
};

// ============================================================================
// Legacy C API (for backward compatibility during transition)
// ============================================================================

// Old C-style AST node (kept for compatibility)
typedef enum {
  AST_INT_LITERAL,
  AST_FLOAT_LITERAL,
  AST_STRING_LITERAL,
  AST_BOOL_LITERAL,
  AST_ARRAY_LITERAL,
  AST_OBJECT_LITERAL,
  AST_IDENTIFIER,
  AST_BINARY_OP,
  AST_UNARY_OP,
  AST_FUNCTION_CALL,
  AST_ARRAY_ACCESS,
  AST_TYPE_DECL,
  AST_VARIABLE_DECL,
  AST_ASSIGNMENT,
  AST_COMPOUND_ASSIGN,
  AST_INCREMENT,
  AST_DECREMENT,
  AST_FUNCTION_DECL,
  AST_RETURN,
  AST_IF,
  AST_WHILE,
  AST_FOR,
  AST_FOR_IN,
  AST_BREAK,
  AST_CONTINUE,
  AST_TRY_CATCH,
  AST_THROW,
  AST_IMPORT,
  AST_BLOCK,
  AST_PROGRAM
} ASTNodeType;

// Old C-style AST node structure (kept for legacy code compatibility)
typedef struct ASTNode_C {
  ASTNodeType type;
  int line;
  int column;

  union {
    long long int_value;
    float float_value;
    char *string_value;
    int bool_value;
  } value;

  struct ASTNode_C *left;
  struct ASTNode_C *right;
  TulparTokenType op;

  char *name;
  DataType data_type;
  struct ASTNode_C **field_types_nodes;
  char **field_names;
  DataType *field_types;
  char **field_custom_types;
  int field_count;
  struct ASTNode_C **field_defaults;

  struct ASTNode_C **parameters;
  int param_count;
  struct ASTNode_C *body;
  DataType return_type;
  char *return_custom_type;
  char *receiver_type_name;
  struct ASTNode_C *receiver;
  
  uint8_t is_moved;

  struct ASTNode_C *condition;
  struct ASTNode_C *then_branch;
  struct ASTNode_C *else_branch;

  struct ASTNode_C *init;
  struct ASTNode_C *increment;
  struct ASTNode_C *iterable;

  struct ASTNode_C **statements;
  int statement_count;

  struct ASTNode_C *return_value;

  struct ASTNode_C **arguments;
  int argument_count;
  char **argument_names;

  struct ASTNode_C **elements;
  int element_count;
  struct ASTNode_C *index;

  char **object_keys;
  struct ASTNode_C **object_values;
  int object_count;

  struct ASTNode_C *try_block;
  struct ASTNode_C *catch_block;
  struct ASTNode_C *finally_block;
  char *catch_var;
  struct ASTNode_C *throw_expr;

} ASTNode_C;

// Old C-style Parser structure
typedef struct {
  Token **tokens;
  int position;
  Token *current_token;
  int token_count;
} Parser_C;

extern "C" {
    Parser_C *parser_create(Token **tokens, int token_count);
    void parser_free(Parser_C *parser);
    ASTNode_C *parser_parse(Parser_C *parser);
    ASTNode_C *ast_node_create(ASTNodeType type);
    void ast_node_free(ASTNode_C *node);
    void ast_print(ASTNode_C *node, int indent);
}

#endif // TULPAR_PARSER_HPP

#include "parser.hpp"
#include "ast_visitor.hpp"
#include "../common/localization.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>

// ============================================================================
// Parser Constructor and Helper Methods
// ============================================================================

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), position_(0) {
    if (tokens_.empty()) {
        throw std::runtime_error("Parser: Empty token list");
    }
}

const Token& Parser::current() const {
    if (position_ >= tokens_.size()) {
        return tokens_.back(); // Return EOF token
    }
    return tokens_[position_];
}

const Token& Parser::peek(int offset) const {
    size_t peek_pos = position_ + offset;
    if (peek_pos >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[peek_pos];
}

void Parser::advance() {
    if (position_ < tokens_.size() - 1) {
        position_++;
    }
}

bool Parser::match(TulparTokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::check(TulparTokenType type) const {
    if (is_at_end()) return false;
    return current().type() == type;
}

Token Parser::expect(TulparTokenType type, const std::string& error_msg) {
    if (!check(type)) {
        error(error_msg + " at line " + std::to_string(current().line()));
    }
    Token tok = current();
    advance();
    return tok;
}

bool Parser::is_at_end() const {
    return current().type() == TOKEN_EOF;
}

void Parser::error(const std::string& message) {
    fprintf(stderr, tulpar::i18n::tr_for_en("Parser Error: %s\n"), message.c_str());
    throw std::runtime_error(message);
}

int Parser::get_precedence(TulparTokenType op) const {
    switch (op) {
        case TOKEN_OR: return 1;
        case TOKEN_AND: return 2;
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL: return 3;
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL: return 4;
        case TOKEN_PLUS:
        case TOKEN_MINUS: return 5;
        case TOKEN_MULTIPLY:
        case TOKEN_DIVIDE: return 6;
        default: return 0;
    }
}

// ============================================================================
// Main Parse Method
// ============================================================================

std::unique_ptr<ASTNode> Parser::parse() {
    std::vector<std::unique_ptr<ASTNode>> statements;
    
    while (!is_at_end()) {
        try {
            statements.push_back(parse_statement());
        } catch (const std::exception& e) {
            fprintf(stderr, tulpar::i18n::tr_for_en("Parse error: %s\n"), e.what());
            // Skip to next statement
            while (!is_at_end() && current().type() != TOKEN_SEMICOLON &&
                   current().type() != TOKEN_RBRACE) {
                advance();
            }
            if (current().type() == TOKEN_SEMICOLON) {
                advance();
            } else if (current().type() == TOKEN_RBRACE) {
                // Move forward to avoid getting stuck on the same block terminator.
                advance();
            } else if (!is_at_end()) {
                // Ensure forward progress even when recovery sentinel is not found.
                advance();
            }
        }
    }
    
    auto program = std::make_unique<ASTNode>(Program(std::move(statements)));
    return program;
}

// ============================================================================
// Statement Parsing
// ============================================================================

std::unique_ptr<ASTNode> Parser::parse_statement() {
    // Variable declaration
    if (check(TOKEN_INT_TYPE) || check(TOKEN_FLOAT_TYPE) ||
        check(TOKEN_STR_TYPE) || check(TOKEN_BOOL_TYPE) ||
        check(TOKEN_ARRAY_TYPE) || check(TOKEN_ARRAY_INT) ||
        check(TOKEN_ARRAY_FLOAT) || check(TOKEN_ARRAY_STR) ||
        check(TOKEN_ARRAY_BOOL) || check(TOKEN_ARRAY_JSON) ||
        check(TOKEN_VAR)) {
        return parse_variable_decl();
    }

    // Custom type variable declaration: <TypeName> <varName> ...
    if (check(TOKEN_IDENTIFIER) && peek().type() == TOKEN_IDENTIFIER) {
        return parse_variable_decl();
    }
    
    // Function declaration
    if (check(TOKEN_FUNC)) {
        return parse_function_decl();
    }
    
    // Type declaration
    if (check(TOKEN_TYPE_KW)) {
        return parse_type_decl();
    }
    
    // Control flow
    if (check(TOKEN_IF)) {
        return parse_if_statement();
    }
    if (check(TOKEN_WHILE)) {
        return parse_while_loop();
    }
    if (check(TOKEN_FOR)) {
        // Need to distinguish for vs for-in
        return parse_for_loop();
    }
    if (check(TOKEN_RETURN)) {
        return parse_return_statement();
    }
    if (check(TOKEN_BREAK)) {
        return parse_break_statement();
    }
    if (check(TOKEN_CONTINUE)) {
        return parse_continue_statement();
    }
    if (check(TOKEN_TRY)) {
        return parse_try_catch();
    }
    if (check(TOKEN_THROW)) {
        return parse_throw_statement();
    }
    if (check(TOKEN_IMPORT)) {
        return parse_import_statement();
    }
    
    // Block
    if (check(TOKEN_LBRACE)) {
        return parse_block();
    }
    
    // Expression statement
    return parse_expression_statement();
}

std::unique_ptr<ASTNode> Parser::parse_variable_decl() {
    SourceLocation loc(current().line(), current().column());
    
    // Parse type
    DataType type = parse_type();
    
    // Parse variable name
    Token name_tok = expect(TOKEN_IDENTIFIER, "Expected variable name");
    std::string name = name_tok.value();
    
    // Optional initializer
    std::unique_ptr<ASTNode> initializer = nullptr;
    if (match(TOKEN_ASSIGN)) {
        initializer = parse_expression();
    }
    
    expect(TOKEN_SEMICOLON, "Expected ';' after variable declaration");
    
    return std::make_unique<ASTNode>(
        VariableDecl(name, type, std::move(initializer), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_function_decl() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'func'
    
    // Function name
    Token name_tok = expect(TOKEN_IDENTIFIER, "Expected function name");
    std::string name = name_tok.value();
    
    // Parameters
    expect(TOKEN_LPAREN, "Expected '(' after function name");
    std::vector<Parameter> parameters;
    
    if (!check(TOKEN_RPAREN)) {
        do {
            DataType param_type = parse_type();
            Token param_name = expect(TOKEN_IDENTIFIER, "Expected parameter name");
            parameters.emplace_back(param_name.value(), param_type);
        } while (match(TOKEN_COMMA));
    }
    
    expect(TOKEN_RPAREN, "Expected ')' after parameters");
    
    // Return type (optional, defaults to void)
    DataType return_type = TYPE_VOID;
    if (!check(TOKEN_LBRACE)) {
        return_type = parse_type();
    }
    
    // Function body
    auto body = parse_block();
    
    return std::make_unique<ASTNode>(
        FunctionDecl(name, std::move(parameters), return_type, std::move(body), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_type_decl() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'type'
    
    Token name_tok = expect(TOKEN_IDENTIFIER, "Expected type name");
    std::string name = name_tok.value();
    
    TypeDecl type_decl(name, loc);
    
    expect(TOKEN_LBRACE, "Expected '{' after type name");
    
    while (!check(TOKEN_RBRACE) && !is_at_end()) {
        DataType field_type = parse_type();
        Token field_name = expect(TOKEN_IDENTIFIER, "Expected field name");
        
        type_decl.field_types.push_back(field_type);
        type_decl.field_names.push_back(field_name.value());
        type_decl.field_custom_types.push_back(std::nullopt);
        type_decl.field_defaults.push_back(nullptr);
        
        expect(TOKEN_SEMICOLON, "Expected ';' after field");
    }
    
    expect(TOKEN_RBRACE, "Expected '}' after type body");
    
    return std::make_unique<ASTNode>(std::move(type_decl));
}

std::unique_ptr<ASTNode> Parser::parse_if_statement() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'if'
    
    expect(TOKEN_LPAREN, "Expected '(' after 'if'");
    auto condition = parse_expression();
    expect(TOKEN_RPAREN, "Expected ')' after condition");
    
    auto then_branch = parse_statement();
    
    std::unique_ptr<ASTNode> else_branch = nullptr;
    if (match(TOKEN_ELSE)) {
        else_branch = parse_statement();
    }
    
    return std::make_unique<ASTNode>(
        IfStatement(std::move(condition), std::move(then_branch),
                    std::move(else_branch), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_while_loop() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'while'
    
    expect(TOKEN_LPAREN, "Expected '(' after 'while'");
    auto condition = parse_expression();
    expect(TOKEN_RPAREN, "Expected ')' after condition");
    
    auto body = parse_statement();
    
    return std::make_unique<ASTNode>(
        WhileLoop(std::move(condition), std::move(body), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_for_loop() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'for'
    
    expect(TOKEN_LPAREN, "Expected '(' after 'for'");
    
    // Check for for-in loop
    if (check(TOKEN_IDENTIFIER)) {
        size_t saved_pos = position_;
        Token id = current();
        advance();
        
        if (check(TOKEN_IN)) {
            // for-in loop
            advance();
            auto iterable = parse_expression();
            expect(TOKEN_RPAREN, "Expected ')' after for-in");
            auto body = parse_statement();
            
            return std::make_unique<ASTNode>(
                ForInLoop(id.value(), std::move(iterable), std::move(body), loc)
            );
        }
        
        // Not for-in, restore position
        position_ = saved_pos;
    }
    
    // Regular for loop
    auto init = parse_statement();
    auto condition = parse_expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after condition");
    auto increment = parse_expression();
    expect(TOKEN_RPAREN, "Expected ')' after for clauses");
    
    auto body = parse_statement();
    
    return std::make_unique<ASTNode>(
        ForLoop(std::move(init), std::move(condition),
                std::move(increment), std::move(body), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_return_statement() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'return'
    
    std::unique_ptr<ASTNode> value = nullptr;
    if (!check(TOKEN_SEMICOLON)) {
        value = parse_expression();
    }
    
    expect(TOKEN_SEMICOLON, "Expected ';' after return");
    
    return std::make_unique<ASTNode>(
        ReturnStatement(std::move(value), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_break_statement() {
    SourceLocation loc(current().line(), current().column());
    advance();
    expect(TOKEN_SEMICOLON, "Expected ';' after break");
    return std::make_unique<ASTNode>(BreakStatement(loc));
}

std::unique_ptr<ASTNode> Parser::parse_continue_statement() {
    SourceLocation loc(current().line(), current().column());
    advance();
    expect(TOKEN_SEMICOLON, "Expected ';' after continue");
    return std::make_unique<ASTNode>(ContinueStatement(loc));
}

std::unique_ptr<ASTNode> Parser::parse_try_catch() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'try'
    
    auto try_block = parse_block();
    
    expect(TOKEN_CATCH, "Expected 'catch' after try block");
    expect(TOKEN_LPAREN, "Expected '(' after 'catch'");
    Token catch_var = expect(TOKEN_IDENTIFIER, "Expected exception variable name");
    expect(TOKEN_RPAREN, "Expected ')' after exception variable");
    
    auto catch_block = parse_block();
    
    std::unique_ptr<ASTNode> finally_block = nullptr;
    if (match(TOKEN_FINALLY)) {
        finally_block = parse_block();
    }
    
    return std::make_unique<ASTNode>(
        TryCatch(std::move(try_block), catch_var.value(),
                 std::move(catch_block), std::move(finally_block), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_throw_statement() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'throw'
    
    auto expression = parse_expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after throw");
    
    return std::make_unique<ASTNode>(
        ThrowStatement(std::move(expression), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_import_statement() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume 'import'
    
    Token path_tok = expect(TOKEN_STRING_LITERAL, "Expected string literal after import");
    expect(TOKEN_SEMICOLON, "Expected ';' after import");
    
    return std::make_unique<ASTNode>(
        ImportStatement(path_tok.value(), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_block() {
    SourceLocation loc(current().line(), current().column());
    expect(TOKEN_LBRACE, "Expected '{'");
    
    std::vector<std::unique_ptr<ASTNode>> statements;
    
    while (!check(TOKEN_RBRACE) && !is_at_end()) {
        statements.push_back(parse_statement());
    }
    
    expect(TOKEN_RBRACE, "Expected '}'");
    
    return std::make_unique<ASTNode>(Block(std::move(statements), loc));
}

std::unique_ptr<ASTNode> Parser::parse_expression_statement() {
    // Assignment / compound assignment statement fast path
    if (check(TOKEN_IDENTIFIER)) {
        const Token name_tok = current();
        const TulparTokenType next_type = peek().type();
        if (next_type == TOKEN_ASSIGN ||
            next_type == TOKEN_PLUS_EQUAL ||
            next_type == TOKEN_MINUS_EQUAL ||
            next_type == TOKEN_MULTIPLY_EQUAL ||
            next_type == TOKEN_DIVIDE_EQUAL) {
            SourceLocation loc(name_tok.line(), name_tok.column());
            advance(); // identifier
            advance(); // assignment operator
            auto value = parse_expression();
            expect(TOKEN_SEMICOLON, "Expected ';' after expression");

            if (next_type == TOKEN_ASSIGN) {
                return std::make_unique<ASTNode>(
                    Assignment(name_tok.value(), std::move(value), loc)
                );
            }
            return std::make_unique<ASTNode>(
                CompoundAssign(name_tok.value(), next_type, std::move(value), loc)
            );
        }
    }

    auto expr = parse_expression();
    expect(TOKEN_SEMICOLON, "Expected ';' after expression");
    return expr;
}

// ============================================================================
// Expression Parsing
// ============================================================================

std::unique_ptr<ASTNode> Parser::parse_expression(int precedence) {
    auto left = parse_unary();
    
    while (!is_at_end()) {
        TulparTokenType op = current().type();
        int op_prec = get_precedence(op);
        
        if (op_prec <= precedence) {
            break;
        }
        
        advance(); // consume operator
        auto right = parse_expression(op_prec);
        
        SourceLocation loc(current().line(), current().column());
        left = std::make_unique<ASTNode>(
            BinaryOp(std::move(left), std::move(right), op, loc)
        );
    }
    
    return parse_postfix(std::move(left));
}

std::unique_ptr<ASTNode> Parser::parse_unary() {
    if (match(TOKEN_MINUS) || match(TOKEN_BANG)) {
        SourceLocation loc(current().line(), current().column());
        TulparTokenType op = peek(-1).type();
        auto operand = parse_unary();
        return std::make_unique<ASTNode>(UnaryOp(std::move(operand), op, loc));
    }
    
    return parse_primary();
}

std::unique_ptr<ASTNode> Parser::parse_primary() {
    SourceLocation loc(current().line(), current().column());
    
    // Literals
    if (check(TOKEN_INT_LITERAL)) {
        long long value = std::stoll(current().value());
        advance();
        return std::make_unique<ASTNode>(IntLiteral(value, loc));
    }
    
    if (check(TOKEN_FLOAT_LITERAL)) {
        double value = std::stod(current().value());
        advance();
        return std::make_unique<ASTNode>(FloatLiteral(value, loc));
    }
    
    if (check(TOKEN_STRING_LITERAL)) {
        std::string value = current().value();
        advance();
        return std::make_unique<ASTNode>(StringLiteral(value, loc));
    }
    
    if (check(TOKEN_TRUE)) {
        advance();
        return std::make_unique<ASTNode>(BoolLiteral(true, loc));
    }
    
    if (check(TOKEN_FALSE)) {
        advance();
        return std::make_unique<ASTNode>(BoolLiteral(false, loc));
    }
    
    // Array literal
    if (check(TOKEN_LBRACKET)) {
        return parse_array_literal();
    }
    
    // Object literal
    if (check(TOKEN_LBRACE)) {
        return parse_object_literal();
    }
    
    // Identifier
    if (check(TOKEN_IDENTIFIER)) {
        std::string name = current().value();
        advance();
        return std::make_unique<ASTNode>(Identifier(name, loc));
    }
    
    // Parenthesized expression
    if (match(TOKEN_LPAREN)) {
        auto expr = parse_expression();
        expect(TOKEN_RPAREN, "Expected ')' after expression");
        return expr;
    }
    
    error("Unexpected token in expression");
    return nullptr;
}

std::unique_ptr<ASTNode> Parser::parse_postfix(std::unique_ptr<ASTNode> expr) {
    while (true) {
        if (match(TOKEN_LPAREN)) {
            expr = parse_call(std::move(expr));
        } else if (match(TOKEN_LBRACKET)) {
            expr = parse_array_access(std::move(expr));
        } else if (match(TOKEN_PLUS_PLUS)) {
            // x++ becomes increment
            if (auto* id = std::get_if<Identifier>(&expr->value)) {
                SourceLocation loc(current().line(), current().column());
                return std::make_unique<ASTNode>(IncrementOp(id->name, loc));
            }
        } else if (match(TOKEN_MINUS_MINUS)) {
            // x-- becomes decrement
            if (auto* id = std::get_if<Identifier>(&expr->value)) {
                SourceLocation loc(current().line(), current().column());
                return std::make_unique<ASTNode>(DecrementOp(id->name, loc));
            }
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ASTNode> Parser::parse_call(std::unique_ptr<ASTNode> callee) {
    SourceLocation loc(current().line(), current().column());
    
    // Extract function name
    std::string func_name;
    if (auto* id = std::get_if<Identifier>(&callee->value)) {
        func_name = id->name;
    } else {
        error("Can only call identifiers");
    }
    
    // Parse arguments
    std::vector<std::unique_ptr<ASTNode>> arguments;
    
    if (!check(TOKEN_RPAREN)) {
        do {
            arguments.push_back(parse_expression());
        } while (match(TOKEN_COMMA));
    }
    
    expect(TOKEN_RPAREN, "Expected ')' after arguments");
    
    return std::make_unique<ASTNode>(
        FunctionCall(func_name, std::move(arguments), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_array_access(std::unique_ptr<ASTNode> object) {
    SourceLocation loc(current().line(), current().column());
    auto index = parse_expression();
    expect(TOKEN_RBRACKET, "Expected ']' after array index");
    
    return std::make_unique<ASTNode>(
        ArrayAccess(std::move(object), std::move(index), loc)
    );
}

std::unique_ptr<ASTNode> Parser::parse_array_literal() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume '['
    
    std::vector<std::unique_ptr<ASTNode>> elements;
    
    if (!check(TOKEN_RBRACKET)) {
        do {
            elements.push_back(parse_expression());
        } while (match(TOKEN_COMMA));
    }
    
    expect(TOKEN_RBRACKET, "Expected ']' after array elements");
    
    return std::make_unique<ASTNode>(ArrayLiteral(std::move(elements), loc));
}

std::unique_ptr<ASTNode> Parser::parse_object_literal() {
    SourceLocation loc(current().line(), current().column());
    advance(); // consume '{'
    
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> fields;
    
    if (!check(TOKEN_RBRACE)) {
        do {
            Token key = expect(TOKEN_STRING_LITERAL, "Expected string key in object");
            expect(TOKEN_COLON, "Expected ':' after object key");
            auto value = parse_expression();
            
            fields.emplace_back(key.value(), std::move(value));
        } while (match(TOKEN_COMMA));
    }
    
    expect(TOKEN_RBRACE, "Expected '}' after object fields");
    
    return std::make_unique<ASTNode>(ObjectLiteral(std::move(fields), loc));
}

// ============================================================================
// Type Parsing
// ============================================================================

DataType Parser::parse_type() {
    if (match(TOKEN_INT_TYPE)) return TYPE_INT;
    if (match(TOKEN_FLOAT_TYPE)) return TYPE_FLOAT;
    if (match(TOKEN_STR_TYPE)) return TYPE_STRING;
    if (match(TOKEN_BOOL_TYPE)) return TYPE_BOOL;
    if (match(TOKEN_ARRAY_TYPE)) return TYPE_ARRAY;
    if (match(TOKEN_ARRAY_INT)) return TYPE_ARRAY_INT;
    if (match(TOKEN_ARRAY_FLOAT)) return TYPE_ARRAY_FLOAT;
    if (match(TOKEN_ARRAY_STR)) return TYPE_ARRAY_STR;
    if (match(TOKEN_ARRAY_BOOL)) return TYPE_ARRAY_BOOL;
    if (match(TOKEN_ARRAY_JSON)) return TYPE_ARRAY_JSON;
    if (match(TOKEN_VAR)) return TYPE_UNKNOWN;
    
    // Custom type
    if (check(TOKEN_IDENTIFIER)) {
        advance();
        return TYPE_CUSTOM;
    }
    
    error("Expected type name");
    return TYPE_UNKNOWN;
}

// ============================================================================
// Legacy C API Implementation (compatibility bridge)
// ============================================================================

namespace {

static char* dup_cstr(const std::string& value) {
    char* out = static_cast<char*>(malloc(value.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

static ASTNode_C* convert_ast_node(const ASTNode& node);

static void set_loc(ASTNode_C* out, const SourceLocation& loc) {
    out->line = loc.line;
    out->column = loc.column;
}

static ASTNode_C* convert_ast_node_ptr(const std::unique_ptr<ASTNode>& node) {
    return node ? convert_ast_node(*node) : nullptr;
}

static ASTNode_C* convert_ast_node(const ASTNode& node) {
    ASTNode_C* out = static_cast<ASTNode_C*>(std::calloc(1, sizeof(ASTNode_C)));
    if (!out) return nullptr;

    std::visit([&](const auto& n) {
        using T = std::decay_t<decltype(n)>;

        if constexpr (std::is_same_v<T, IntLiteral>) {
            out->type = AST_INT_LITERAL;
            out->value.int_value = n.value;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, FloatLiteral>) {
            out->type = AST_FLOAT_LITERAL;
            out->value.float_value = static_cast<float>(n.value);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, StringLiteral>) {
            out->type = AST_STRING_LITERAL;
            out->value.string_value = dup_cstr(n.value);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, BoolLiteral>) {
            out->type = AST_BOOL_LITERAL;
            out->value.bool_value = n.value ? 1 : 0;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, Identifier>) {
            out->type = AST_IDENTIFIER;
            out->name = dup_cstr(n.name);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, BinaryOp>) {
            out->type = AST_BINARY_OP;
            out->left = convert_ast_node_ptr(n.left);
            out->right = convert_ast_node_ptr(n.right);
            out->op = n.op;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, UnaryOp>) {
            out->type = AST_UNARY_OP;
            out->left = convert_ast_node_ptr(n.operand);
            out->op = n.op;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ArrayLiteral>) {
            out->type = AST_ARRAY_LITERAL;
            out->element_count = static_cast<int>(n.elements.size());
            if (out->element_count > 0) {
                out->elements = static_cast<ASTNode_C**>(std::calloc(out->element_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->element_count; ++i) {
                    out->elements[i] = convert_ast_node_ptr(n.elements[i]);
                }
            }
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ObjectLiteral>) {
            out->type = AST_OBJECT_LITERAL;
            out->object_count = static_cast<int>(n.fields.size());
            if (out->object_count > 0) {
                out->object_keys = static_cast<char**>(std::calloc(out->object_count, sizeof(char*)));
                out->object_values = static_cast<ASTNode_C**>(std::calloc(out->object_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->object_count; ++i) {
                    out->object_keys[i] = dup_cstr(n.fields[i].first);
                    out->object_values[i] = convert_ast_node_ptr(n.fields[i].second);
                }
            }
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ArrayAccess>) {
            out->type = AST_ARRAY_ACCESS;
            out->left = convert_ast_node_ptr(n.object);
            out->index = convert_ast_node_ptr(n.index);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, FunctionCall>) {
            out->type = AST_FUNCTION_CALL;
            out->name = dup_cstr(n.name);
            out->argument_count = static_cast<int>(n.arguments.size());
            if (out->argument_count > 0) {
                out->arguments = static_cast<ASTNode_C**>(std::calloc(out->argument_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->argument_count; ++i) {
                    out->arguments[i] = convert_ast_node_ptr(n.arguments[i]);
                }
            }
            out->receiver = convert_ast_node_ptr(n.receiver);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, VariableDecl>) {
            out->type = AST_VARIABLE_DECL;
            out->name = dup_cstr(n.name);
            out->data_type = n.data_type;
            out->right = convert_ast_node_ptr(n.initializer);
            out->is_moved = n.is_moved ? 1 : 0;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, Assignment>) {
            out->type = AST_ASSIGNMENT;
            out->name = dup_cstr(n.name);
            out->right = convert_ast_node_ptr(n.value);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, CompoundAssign>) {
            out->type = AST_COMPOUND_ASSIGN;
            out->name = dup_cstr(n.name);
            out->right = convert_ast_node_ptr(n.value);
            out->op = n.op;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, IncrementOp>) {
            out->type = AST_INCREMENT;
            out->name = dup_cstr(n.name);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, DecrementOp>) {
            out->type = AST_DECREMENT;
            out->name = dup_cstr(n.name);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, FunctionDecl>) {
            out->type = AST_FUNCTION_DECL;
            out->name = dup_cstr(n.name);
            out->param_count = static_cast<int>(n.parameters.size());
            out->return_type = n.return_type;
            if (n.return_custom_type.has_value()) {
                out->return_custom_type = dup_cstr(n.return_custom_type.value());
            }
            if (out->param_count > 0) {
                out->parameters = static_cast<ASTNode_C**>(std::calloc(out->param_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->param_count; ++i) {
                    ASTNode_C* param = static_cast<ASTNode_C*>(std::calloc(1, sizeof(ASTNode_C)));
                    param->type = AST_VARIABLE_DECL;
                    param->name = dup_cstr(n.parameters[i].name);
                    param->data_type = n.parameters[i].type;
                    if (n.parameters[i].custom_type.has_value()) {
                        param->return_custom_type = dup_cstr(n.parameters[i].custom_type.value());
                    }
                    out->parameters[i] = param;
                }
            }
            out->body = convert_ast_node_ptr(n.body);
            if (n.receiver_type.has_value()) {
                out->receiver_type_name = dup_cstr(n.receiver_type.value());
            }
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, IfStatement>) {
            out->type = AST_IF;
            out->condition = convert_ast_node_ptr(n.condition);
            out->then_branch = convert_ast_node_ptr(n.then_branch);
            out->else_branch = convert_ast_node_ptr(n.else_branch);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, WhileLoop>) {
            out->type = AST_WHILE;
            out->condition = convert_ast_node_ptr(n.condition);
            out->body = convert_ast_node_ptr(n.body);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ForLoop>) {
            out->type = AST_FOR;
            out->init = convert_ast_node_ptr(n.init);
            out->condition = convert_ast_node_ptr(n.condition);
            out->increment = convert_ast_node_ptr(n.increment);
            out->body = convert_ast_node_ptr(n.body);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ForInLoop>) {
            out->type = AST_FOR_IN;
            out->name = dup_cstr(n.variable);
            out->iterable = convert_ast_node_ptr(n.iterable);
            out->body = convert_ast_node_ptr(n.body);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ReturnStatement>) {
            out->type = AST_RETURN;
            out->return_value = convert_ast_node_ptr(n.value);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, BreakStatement>) {
            out->type = AST_BREAK;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ContinueStatement>) {
            out->type = AST_CONTINUE;
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, Block>) {
            out->type = AST_BLOCK;
            out->statement_count = static_cast<int>(n.statements.size());
            if (out->statement_count > 0) {
                out->statements = static_cast<ASTNode_C**>(std::calloc(out->statement_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->statement_count; ++i) {
                    out->statements[i] = convert_ast_node_ptr(n.statements[i]);
                }
            }
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, Program>) {
            out->type = AST_PROGRAM;
            out->statement_count = static_cast<int>(n.statements.size());
            if (out->statement_count > 0) {
                out->statements = static_cast<ASTNode_C**>(std::calloc(out->statement_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->statement_count; ++i) {
                    out->statements[i] = convert_ast_node_ptr(n.statements[i]);
                }
            }
            out->line = 1;
            out->column = 1;
        } else if constexpr (std::is_same_v<T, TryCatch>) {
            out->type = AST_TRY_CATCH;
            out->try_block = convert_ast_node_ptr(n.try_block);
            out->catch_var = dup_cstr(n.catch_var);
            out->catch_block = convert_ast_node_ptr(n.catch_block);
            out->finally_block = convert_ast_node_ptr(n.finally_block);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ThrowStatement>) {
            out->type = AST_THROW;
            out->throw_expr = convert_ast_node_ptr(n.expression);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, ImportStatement>) {
            out->type = AST_IMPORT;
            // Legacy AOT path reads import path from value.string_value.
            out->value.string_value = dup_cstr(n.path);
            // Keep name populated as a compatibility alias for other backends.
            out->name = dup_cstr(n.path);
            set_loc(out, n.loc);
        } else if constexpr (std::is_same_v<T, TypeDecl>) {
            out->type = AST_TYPE_DECL;
            out->name = dup_cstr(n.name);
            out->field_count = static_cast<int>(n.field_names.size());
            if (out->field_count > 0) {
                out->field_names = static_cast<char**>(std::calloc(out->field_count, sizeof(char*)));
                out->field_types = static_cast<DataType*>(std::calloc(out->field_count, sizeof(DataType)));
                out->field_custom_types = static_cast<char**>(std::calloc(out->field_count, sizeof(char*)));
                out->field_defaults = static_cast<ASTNode_C**>(std::calloc(out->field_count, sizeof(ASTNode_C*)));
                for (int i = 0; i < out->field_count; ++i) {
                    out->field_names[i] = dup_cstr(n.field_names[i]);
                    out->field_types[i] = n.field_types[i];
                    if (n.field_custom_types[i].has_value()) {
                        out->field_custom_types[i] = dup_cstr(n.field_custom_types[i].value());
                    }
                    out->field_defaults[i] = convert_ast_node_ptr(n.field_defaults[i]);
                }
            }
            set_loc(out, n.loc);
        }
    }, node.value);

    return out;
}

static void ast_node_free_recursive(ASTNode_C* node) {
    if (!node) return;

    ast_node_free_recursive(node->left);
    ast_node_free_recursive(node->right);
    ast_node_free_recursive(node->body);
    ast_node_free_recursive(node->condition);
    ast_node_free_recursive(node->then_branch);
    ast_node_free_recursive(node->else_branch);
    ast_node_free_recursive(node->init);
    ast_node_free_recursive(node->increment);
    ast_node_free_recursive(node->iterable);
    ast_node_free_recursive(node->return_value);
    ast_node_free_recursive(node->index);
    ast_node_free_recursive(node->receiver);
    ast_node_free_recursive(node->try_block);
    ast_node_free_recursive(node->catch_block);
    ast_node_free_recursive(node->finally_block);
    ast_node_free_recursive(node->throw_expr);

    for (int i = 0; i < node->statement_count; ++i) ast_node_free_recursive(node->statements[i]);
    for (int i = 0; i < node->argument_count; ++i) ast_node_free_recursive(node->arguments[i]);
    for (int i = 0; i < node->element_count; ++i) ast_node_free_recursive(node->elements[i]);
    for (int i = 0; i < node->object_count; ++i) ast_node_free_recursive(node->object_values[i]);
    for (int i = 0; i < node->param_count; ++i) ast_node_free_recursive(node->parameters[i]);
    for (int i = 0; i < node->field_count; ++i) ast_node_free_recursive(node->field_defaults[i]);

    if (node->type == AST_STRING_LITERAL) {
        free(node->value.string_value);
    }

    switch (node->type) {
        case AST_IDENTIFIER:
        case AST_VARIABLE_DECL:
        case AST_ASSIGNMENT:
        case AST_COMPOUND_ASSIGN:
        case AST_INCREMENT:
        case AST_DECREMENT:
        case AST_FUNCTION_DECL:
        case AST_IMPORT:
        case AST_TYPE_DECL:
        case AST_FOR_IN:
        case AST_FUNCTION_CALL:
            free(node->name);
            break;
        default:
            break;
    }

    if (node->type == AST_FUNCTION_DECL) {
        free(node->return_custom_type);
        free(node->receiver_type_name);
    }

    if (node->type == AST_TRY_CATCH) {
        free(node->catch_var);
    }

    if (node->field_names) {
        for (int i = 0; i < node->field_count; ++i) free(node->field_names[i]);
        free(node->field_names);
    }
    if (node->field_custom_types) {
        for (int i = 0; i < node->field_count; ++i) free(node->field_custom_types[i]);
        free(node->field_custom_types);
    }
    if (node->object_keys) {
        for (int i = 0; i < node->object_count; ++i) free(node->object_keys[i]);
        free(node->object_keys);
    }
    if (node->argument_names) {
        for (int i = 0; i < node->argument_count; ++i) free(node->argument_names[i]);
        free(node->argument_names);
    }

    free(node->field_types);
    free(node->field_types_nodes);
    free(node->field_defaults);
    free(node->statements);
    free(node->arguments);
    free(node->elements);
    free(node->object_values);
    free(node->parameters);
    free(node);
}

} // namespace

extern "C" {

Parser_C *parser_create(Token **tokens, int token_count) {
    if (!tokens || token_count <= 0) return nullptr;
    Parser_C* parser = static_cast<Parser_C*>(std::calloc(1, sizeof(Parser_C)));
    if (!parser) return nullptr;
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->position = 0;
    parser->current_token = tokens[0];
    return parser;
}

void parser_free(Parser_C *parser) {
    free(parser);
}

ASTNode_C *parser_parse(Parser_C *parser) {
    if (!parser || !parser->tokens || parser->token_count <= 0) return nullptr;

    std::vector<Token> token_vec;
    token_vec.reserve(parser->token_count);
    for (int i = 0; i < parser->token_count; ++i) {
        if (parser->tokens[i]) {
            token_vec.push_back(*parser->tokens[i]);
        }
    }
    if (token_vec.empty() || token_vec.back().type() != TOKEN_EOF) {
        token_vec.emplace_back(TOKEN_EOF, "", 0, 0);
    }

    try {
        Parser modern_parser(std::move(token_vec));
        std::unique_ptr<ASTNode> modern_ast = modern_parser.parse();
        if (!modern_ast) return nullptr;
        return convert_ast_node(*modern_ast);
    } catch (const std::exception& e) {
        std::fprintf(stderr, tulpar::i18n::tr_for_en("Parser Error: %s\n"), e.what());
        return nullptr;
    }
}

ASTNode_C *ast_node_create(ASTNodeType type) {
    ASTNode_C* node = static_cast<ASTNode_C*>(std::calloc(1, sizeof(ASTNode_C)));
    if (!node) return nullptr;
    node->type = type;
    return node;
}

void ast_node_free(ASTNode_C *node) {
    ast_node_free_recursive(node);
}

void ast_print(ASTNode_C *node, int indent) {
    if (!node) return;
    for (int i = 0; i < indent; ++i) std::printf("  ");
    std::printf("ASTNode(type=%d)\n", static_cast<int>(node->type));
    for (int i = 0; i < node->statement_count; ++i) {
        ast_print(node->statements[i], indent + 1);
    }
}

} // extern "C"

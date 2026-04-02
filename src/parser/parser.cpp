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
// Legacy C API Implementation (stub for compatibility)
// ============================================================================

extern "C" {

Parser_C *parser_create(Token **tokens, int token_count) {
    // TODO: Implement C API wrapper
    return nullptr;
}

void parser_free(Parser_C *parser) {
    // TODO: Implement
}

ASTNode_C *parser_parse(Parser_C *parser) {
    // TODO: Implement
    return nullptr;
}

ASTNode_C *ast_node_create(ASTNodeType type) {
    // TODO: Implement
    return nullptr;
}

void ast_node_free(ASTNode_C *node) {
    // TODO: Implement
}

void ast_print(ASTNode_C *node, int indent) {
    // TODO: Implement
}

} // extern "C"

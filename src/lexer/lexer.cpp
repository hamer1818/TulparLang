#include "lexer.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

// ============================================================================
// Token Class Implementation
// ============================================================================

void Token::print() const {
    const char* type_names[] = {
        "INT_TYPE", "FLOAT_TYPE", "STR_TYPE", "BOOL_TYPE",
        "ARRAY_TYPE", "ARRAY_INT", "ARRAY_FLOAT", "ARRAY_STR", "ARRAY_BOOL", "ARRAY_JSON",
        "FUNC", "TYPE_KW", "RETURN", "IF", "ELSE", "WHILE", "FOR", "IN",
        "BREAK", "CONTINUE", "TRY", "CATCH", "FINALLY", "THROW", "IMPORT",
        "TRUE", "FALSE", "MOVE", "VAR",
        "IDENTIFIER", "INT_LITERAL", "FLOAT_LITERAL", "STRING_LITERAL",
        "PLUS", "MINUS", "MULTIPLY", "DIVIDE", "ASSIGN",
        "EQUAL", "NOT_EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL",
        "AND", "OR", "BANG", "PLUS_PLUS", "MINUS_MINUS",
        "PLUS_EQUAL", "MINUS_EQUAL", "MULTIPLY_EQUAL", "DIVIDE_EQUAL",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET",
        "SEMICOLON", "COMMA", "COLON", "DOT",
        "EOF", "ERROR"
    };
    
    printf("Token(%s, \"%s\", line: %d, col: %d)\n",
           type_names[type_],
           value_.c_str(),
           line_,
           column_);
}

// ============================================================================
// Lexer Class Implementation
// ============================================================================

Lexer::Lexer(const std::string& source)
    : source_(source),
      position_(0),
      line_(1),
      column_(0),
      current_char_(source.empty() ? '\0' : source[0]) {
}

void Lexer::advance() {
    position_++;
    column_++;
    
    if (position_ < source_.length()) {
        current_char_ = source_[position_];
    } else {
        current_char_ = '\0';
    }
}

char Lexer::peek() const {
    size_t peek_pos = position_ + 1;
    if (peek_pos < source_.length()) {
        return source_[peek_pos];
    }
    return '\0';
}

void Lexer::skip_whitespace() {
    while (current_char_ != '\0' && std::isspace(current_char_)) {
        if (current_char_ == '\n') {
            line_++;
            column_ = 0;
        }
        advance();
    }
}

void Lexer::skip_comment() {
    if (current_char_ == '/' && peek() == '/') {
        // Single line comment
        while (current_char_ != '\0' && current_char_ != '\n') {
            advance();
        }
    }
}

void Lexer::skip_block_comment() {
    if (current_char_ == '/' && peek() == '*') {
        // Block comment: /* ... */
        int start_line = line_;
        int start_col = column_;
        advance(); // '/'
        advance(); // '*'
        
        while (current_char_ != '\0') {
            if (current_char_ == '*' && peek() == '/') {
                advance(); // '*'
                advance(); // '/'
                return;
            }
            if (current_char_ == '\n') {
                line_++;
                column_ = 0;
            }
            advance();
        }
        
        // Not terminated
        fprintf(stderr,
                "Lexer Error: Block comment not terminated (started at line %d, col %d)\n",
                start_line, start_col);
    }
}

Token Lexer::read_number() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    bool is_float = false;
    
    while (current_char_ != '\0' &&
           (std::isdigit(current_char_) || current_char_ == '.')) {
        if (current_char_ == '.') {
            if (is_float) break; // Second dot - error
            is_float = true;
        }
        buffer += current_char_;
        advance();
    }
    
    return Token(is_float ? TOKEN_FLOAT_LITERAL : TOKEN_INT_LITERAL,
                 buffer, start_line, start_column);
}

Token Lexer::read_string() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    
    advance(); // Skip opening "
    
    while (current_char_ != '\0' && current_char_ != '"') {
        if (current_char_ == '\\') {
            advance();
            // Escape sequences
            switch (current_char_) {
                case 'n': buffer += '\n'; break;
                case 't': buffer += '\t'; break;
                case 'r': buffer += '\r'; break;
                case '\\': buffer += '\\'; break;
                case '"': buffer += '"'; break;
                default: buffer += current_char_; break;
            }
        } else {
            buffer += current_char_;
        }
        advance();
    }
    
    if (current_char_ == '"') {
        advance(); // Skip closing "
    }
    
    return Token(TOKEN_STRING_LITERAL, buffer, start_line, start_column);
}

Token Lexer::read_identifier() {
    int start_line = line_;
    int start_column = column_;
    std::string buffer;
    
    while (current_char_ != '\0' &&
           (std::isalnum(current_char_) || current_char_ == '_' ||
            static_cast<unsigned char>(current_char_) > 127)) {
        buffer += current_char_;
        advance();
    }
    
    // Check for keywords
    TulparTokenType type = TOKEN_IDENTIFIER;
    
    // Data types
    if (buffer == "int") type = TOKEN_INT_TYPE;
    else if (buffer == "float") type = TOKEN_FLOAT_TYPE;
    else if (buffer == "str") type = TOKEN_STR_TYPE;
    else if (buffer == "bool") type = TOKEN_BOOL_TYPE;
    else if (buffer == "array") type = TOKEN_ARRAY_TYPE;
    else if (buffer == "arrayInt") type = TOKEN_ARRAY_INT;
    else if (buffer == "arrayFloat") type = TOKEN_ARRAY_FLOAT;
    else if (buffer == "arrayStr") type = TOKEN_ARRAY_STR;
    else if (buffer == "arrayBool") type = TOKEN_ARRAY_BOOL;
    else if (buffer == "arrayJson") type = TOKEN_ARRAY_JSON;
    
    // Keywords
    else if (buffer == "func") type = TOKEN_FUNC;
    else if (buffer == "type") type = TOKEN_TYPE_KW;
    else if (buffer == "return") type = TOKEN_RETURN;
    else if (buffer == "if") type = TOKEN_IF;
    else if (buffer == "else") type = TOKEN_ELSE;
    else if (buffer == "while") type = TOKEN_WHILE;
    else if (buffer == "for") type = TOKEN_FOR;
    else if (buffer == "in") type = TOKEN_IN;
    else if (buffer == "break") type = TOKEN_BREAK;
    else if (buffer == "continue") type = TOKEN_CONTINUE;
    else if (buffer == "try") type = TOKEN_TRY;
    else if (buffer == "catch") type = TOKEN_CATCH;
    else if (buffer == "finally") type = TOKEN_FINALLY;
    else if (buffer == "throw") type = TOKEN_THROW;
    else if (buffer == "import") type = TOKEN_IMPORT;
    else if (buffer == "true") type = TOKEN_TRUE;
    else if (buffer == "false") type = TOKEN_FALSE;
    else if (buffer == "move") type = TOKEN_MOVE;
    else if (buffer == "var") type = TOKEN_VAR;
    
    // Turkish keywords (UTF-8 encoded)
    else if (buffer == "iken") type = TOKEN_WHILE;
    else if (buffer == "eğer") type = TOKEN_IF;
    else if (buffer == "yoksa") type = TOKEN_ELSE;
    else if (buffer == "değilse") type = TOKEN_ELSE;
    else if (buffer == "için") type = TOKEN_FOR;
    else if (buffer == "dur") type = TOKEN_BREAK;
    else if (buffer == "devam") type = TOKEN_CONTINUE;
    else if (buffer == "işlev") type = TOKEN_FUNC;
    else if (buffer == "fonk") type = TOKEN_FUNC;
    else if (buffer == "döndür") type = TOKEN_RETURN;
    else if (buffer == "içe_aktar") type = TOKEN_IMPORT;
    else if (buffer == "doğru") type = TOKEN_TRUE;
    else if (buffer == "yanlış") type = TOKEN_FALSE;
    else if (buffer == "dene") type = TOKEN_TRY;
    else if (buffer == "yakala") type = TOKEN_CATCH;
    else if (buffer == "sonunda") type = TOKEN_FINALLY;
    else if (buffer == "fırlat") type = TOKEN_THROW;
    else if (buffer == "tip") type = TOKEN_TYPE_KW;
    
    return Token(type, buffer, start_line, start_column);
}

Token Lexer::next_token() {
    while (current_char_ != '\0') {
        // Skip whitespace
        if (std::isspace(current_char_)) {
            skip_whitespace();
            continue;
        }
        
        // Skip comments
        if (current_char_ == '/') {
            if (peek() == '/') {
                skip_comment();
                continue;
            } else if (peek() == '*') {
                skip_block_comment();
                continue;
            }
        }
        
        // Numbers
        if (std::isdigit(current_char_)) {
            return read_number();
        }
        
        // Strings
        if (current_char_ == '"') {
            return read_string();
        }
        
        // Identifiers and keywords
        if (std::isalpha(current_char_) || current_char_ == '_' ||
            static_cast<unsigned char>(current_char_) > 127) {
            return read_identifier();
        }
        
        // Operators and symbols
        int start_line = line_;
        int start_column = column_;
        char ch = current_char_;
        char next_ch = peek();
        
        // Two-character operators
        if (ch == '+' && next_ch == '+') {
            advance(); advance();
            return Token(TOKEN_PLUS_PLUS, "++", start_line, start_column);
        }
        if (ch == '-' && next_ch == '-') {
            advance(); advance();
            return Token(TOKEN_MINUS_MINUS, "--", start_line, start_column);
        }
        if (ch == '+' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_PLUS_EQUAL, "+=", start_line, start_column);
        }
        if (ch == '-' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_MINUS_EQUAL, "-=", start_line, start_column);
        }
        if (ch == '*' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_MULTIPLY_EQUAL, "*=", start_line, start_column);
        }
        if (ch == '/' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_DIVIDE_EQUAL, "/=", start_line, start_column);
        }
        if (ch == '=' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_EQUAL, "==", start_line, start_column);
        }
        if (ch == '!' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_NOT_EQUAL, "!=", start_line, start_column);
        }
        if (ch == '<' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_LESS_EQUAL, "<=", start_line, start_column);
        }
        if (ch == '>' && next_ch == '=') {
            advance(); advance();
            return Token(TOKEN_GREATER_EQUAL, ">=", start_line, start_column);
        }
        if (ch == '&' && next_ch == '&') {
            advance(); advance();
            return Token(TOKEN_AND, "&&", start_line, start_column);
        }
        if (ch == '|' && next_ch == '|') {
            advance(); advance();
            return Token(TOKEN_OR, "||", start_line, start_column);
        }
        
        // Single-character operators
        advance();
        std::string value(1, ch);
        
        switch (ch) {
            case '+': return Token(TOKEN_PLUS, value, start_line, start_column);
            case '-': return Token(TOKEN_MINUS, value, start_line, start_column);
            case '*': return Token(TOKEN_MULTIPLY, value, start_line, start_column);
            case '/': return Token(TOKEN_DIVIDE, value, start_line, start_column);
            case '=': return Token(TOKEN_ASSIGN, value, start_line, start_column);
            case '<': return Token(TOKEN_LESS, value, start_line, start_column);
            case '>': return Token(TOKEN_GREATER, value, start_line, start_column);
            case '!': return Token(TOKEN_BANG, value, start_line, start_column);
            case '(': return Token(TOKEN_LPAREN, value, start_line, start_column);
            case ')': return Token(TOKEN_RPAREN, value, start_line, start_column);
            case '{': return Token(TOKEN_LBRACE, value, start_line, start_column);
            case '}': return Token(TOKEN_RBRACE, value, start_line, start_column);
            case '[': return Token(TOKEN_LBRACKET, value, start_line, start_column);
            case ']': return Token(TOKEN_RBRACKET, value, start_line, start_column);
            case ';': return Token(TOKEN_SEMICOLON, value, start_line, start_column);
            case ',': return Token(TOKEN_COMMA, value, start_line, start_column);
            case ':': return Token(TOKEN_COLON, value, start_line, start_column);
            case '.': return Token(TOKEN_DOT, value, start_line, start_column);
            default:
                fprintf(stderr, "Lexer Error: Unknown character '%c' at line %d, col %d\n",
                        ch, start_line, start_column);
                return Token(TOKEN_ERROR, value, start_line, start_column);
        }
    }
    
    return Token(TOKEN_EOF, "", line_, column_);
}

// ============================================================================
// Legacy C API (for backward compatibility)
// ============================================================================

extern "C" {

Lexer* lexer_create(const char* source) {
    return new Lexer(std::string(source));
}

void lexer_free(Lexer* lexer) {
    delete lexer;
}

Token* lexer_next_token(Lexer* lexer) {
    Token tok = lexer->next_token();
    // Copy to heap for C API compatibility
    Token* heap_token = new Token(tok);
    return heap_token;
}

void token_free(Token* token) {
    delete token;
}

void token_print(Token* token) {
    token->print();
}

} // extern "C"

#ifndef TULPAR_LEXER_HPP
#define TULPAR_LEXER_HPP

#include <string>
#include <memory>

// Token türleri (Windows winnt.h ile çakışmayı önlemek için Tulpar prefix)
enum TulparTokenType {
    // Veri tipleri
    TOKEN_INT_TYPE,      // "int"
    TOKEN_FLOAT_TYPE,    // "float"
    TOKEN_STR_TYPE,      // "str"
    TOKEN_BOOL_TYPE,     // "bool"
    TOKEN_ARRAY_TYPE,    // "array" (mixed)
    TOKEN_ARRAY_INT,     // "arrayInt"
    TOKEN_ARRAY_FLOAT,   // "arrayFloat"
    TOKEN_ARRAY_STR,     // "arrayStr"
    TOKEN_ARRAY_BOOL,    // "arrayBool"
    TOKEN_ARRAY_JSON,    // "arrayJson" (JSON-like mixed)
    
    // Anahtar kelimeler
    TOKEN_FUNC,          // "func"
    TOKEN_TYPE_KW,       // "type" (struct benzeri)
    TOKEN_RETURN,        // "return"
    TOKEN_IF,            // "if"
    TOKEN_ELSE,          // "else"
    TOKEN_WHILE,         // "while"
    TOKEN_FOR,           // "for"
    TOKEN_IN,            // "in"
    TOKEN_BREAK,         // "break"
    TOKEN_CONTINUE,      // "continue"
    TOKEN_TRY,           // "try"
    TOKEN_CATCH,         // "catch"
    TOKEN_FINALLY,       // "finally"
    TOKEN_THROW,         // "throw"
    TOKEN_IMPORT,        // "import"
    TOKEN_TRUE,          // "true"
    TOKEN_FALSE,         // "false"
    TOKEN_MOVE,          // "move" - ownership transfer
    TOKEN_VAR,           // "var" - type inference
    
    // Değerler
    TOKEN_IDENTIFIER,    // değişken/fonksiyon isimleri
    TOKEN_INT_LITERAL,   // 123
    TOKEN_FLOAT_LITERAL, // 3.14
    TOKEN_STRING_LITERAL,// "merhaba"
    
    // Operatörler
    TOKEN_PLUS,          // +
    TOKEN_MINUS,         // -
    TOKEN_MULTIPLY,      // *
    TOKEN_DIVIDE,        // /
    TOKEN_ASSIGN,        // =
    TOKEN_EQUAL,         // ==
    TOKEN_NOT_EQUAL,     // !=
    TOKEN_LESS,          // <
    TOKEN_GREATER,       // >
    TOKEN_LESS_EQUAL,    // <=
    TOKEN_GREATER_EQUAL, // >=
    TOKEN_AND,           // &&
    TOKEN_OR,            // ||
    TOKEN_BANG,          // !
    TOKEN_PLUS_PLUS,     // ++
    TOKEN_MINUS_MINUS,   // --
    TOKEN_PLUS_EQUAL,    // +=
    TOKEN_MINUS_EQUAL,   // -=
    TOKEN_MULTIPLY_EQUAL,// *=
    TOKEN_DIVIDE_EQUAL,  // /=
    
    // Semboller
    TOKEN_LPAREN,        // (
    TOKEN_RPAREN,        // )
    TOKEN_LBRACE,        // {
    TOKEN_RBRACE,        // }
    TOKEN_LBRACKET,      // [
    TOKEN_RBRACKET,      // ]
    TOKEN_SEMICOLON,     // ;
    TOKEN_COMMA,         // ,
    TOKEN_COLON,         // : (for JSON objects)
    TOKEN_DOT,           // . (member access)
    
    TOKEN_EOF,           // End of file
    TOKEN_ERROR          // Error
};

// Modern C++ Token class
class Token {
private:
    TulparTokenType type_;
    std::string value_;
    int line_;
    int column_;

public:
    // Constructor
    Token(TulparTokenType type, const std::string& value, int line, int column)
        : type_(type), value_(value), line_(line), column_(column) {}
    
    // Getters
    TulparTokenType type() const { return type_; }
    const std::string& value() const { return value_; }
    int line() const { return line_; }
    int column() const { return column_; }
    
    // Utility
    void print() const;
    
    // Rule of zero - compiler handles copy/move/destroy automatically
};

// Modern C++ Lexer class
class Lexer {
private:
    std::string source_;
    size_t position_;
    int line_;
    int column_;
    char current_char_;
    
    // Private helper methods
    void advance();
    char peek() const;
    void skip_whitespace();
    void skip_comment();
    void skip_block_comment();
    
    Token read_number();
    Token read_string();
    Token read_identifier();

public:
    // Constructor
    explicit Lexer(const std::string& source);
    
    // Main tokenization method
    Token next_token();
    
    // Rule of zero - std::string handles cleanup automatically
};

// Legacy C API (for backward compatibility during transition)
// These will be removed in later phases
extern "C" {
    Lexer* lexer_create(const char* source);
    void lexer_free(Lexer* lexer);
    Token* lexer_next_token(Lexer* lexer);
    void token_free(Token* token);
    void token_print(Token* token);
}

#endif // TULPAR_LEXER_HPP
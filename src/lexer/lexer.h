#ifndef LEXER_H
#define LEXER_H

// Token türleri (Windows winnt.h ile çakışmayı önlemek için Tulpar prefix)
typedef enum {
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
    TOKEN_RETURN,        // "return"
    TOKEN_IF,            // "if"
    TOKEN_ELSE,          // "else"
    TOKEN_WHILE,         // "while"
    TOKEN_FOR,           // "for"
    TOKEN_IN,            // "in"
    TOKEN_BREAK,         // "break"
    TOKEN_CONTINUE,      // "continue"
    TOKEN_TRUE,          // "true"
    TOKEN_FALSE,         // "false"
    
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
    
    TOKEN_EOF,           // Dosya sonu
    TOKEN_ERROR          // Hata
} TulparTokenType;

// Token yapısı
typedef struct {
    TulparTokenType type;
    char* value;         // Token'ın değeri (string olarak)
    int line;            // Hangi satırda olduğu
    int column;          // Hangi sütunda olduğu
} Token;

// Lexer yapısı
typedef struct {
    char* source;        // Kaynak kod
    int position;        // Şu anki pozisyon
    int line;            // Şu anki satır
    int column;          // Şu anki sütun
    char current_char;   // Şu anki karakter
} Lexer;

// Fonksiyon prototipleri
Lexer* lexer_create(char* source);
void lexer_free(Lexer* lexer);
Token* lexer_next_token(Lexer* lexer);
void token_free(Token* token);
void token_print(Token* token);

#endif
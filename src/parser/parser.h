#ifndef PARSER_H
#define PARSER_H

#include "../lexer/lexer.h"

// AST düğüm türleri
typedef enum {
    // İfadeler (Expressions)
    AST_INT_LITERAL,
    AST_FLOAT_LITERAL,
    AST_STRING_LITERAL,
    AST_BOOL_LITERAL,
    AST_ARRAY_LITERAL,    // [1, 2, 3]
    AST_IDENTIFIER,
    AST_BINARY_OP,        // +, -, *, /, ==, !=, <, >, <=, >=
    AST_UNARY_OP,         // -, !
    AST_FUNCTION_CALL,    // fonksiyon çağrısı
    AST_ARRAY_ACCESS,     // arr[0]
    
    // İfadeler (Statements)
    AST_VARIABLE_DECL,    // int x = 5;
    AST_ASSIGNMENT,       // x = 10;
    AST_COMPOUND_ASSIGN,  // x += 5; x -= 3; etc
    AST_INCREMENT,        // x++
    AST_DECREMENT,        // x--
    AST_FUNCTION_DECL,    // func topla(int a, int b) { ... }
    AST_RETURN,           // return x;
    AST_IF,               // if (x > 5) { ... }
    AST_WHILE,            // while (x < 10) { ... }
    AST_FOR,              // for (int i = 0; i < 10; i = i + 1) { ... }
    AST_FOR_IN,           // for (x in range(10)) { ... }
    AST_BREAK,            // break;
    AST_CONTINUE,         // continue;
    AST_BLOCK,            // { ... }
    AST_PROGRAM           // Tüm program
} ASTNodeType;

// Veri tipi
typedef enum {
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_STRING,
    TYPE_BOOL,
    TYPE_ARRAY,         // Mixed type array
    TYPE_ARRAY_INT,     // Int-only array
    TYPE_ARRAY_FLOAT,   // Float-only array
    TYPE_ARRAY_STR,     // String-only array
    TYPE_ARRAY_BOOL,    // Bool-only array
    TYPE_ARRAY_JSON,    // JSON-like mixed array
    TYPE_VOID
} DataType;

// AST düğümü
typedef struct ASTNode {
    ASTNodeType type;
    
    // Değerler (literaller için)
    union {
        int int_value;
        float float_value;
        char* string_value;
        int bool_value;
    } value;
    
    // Binary operatör için
    struct ASTNode* left;
    struct ASTNode* right;
    OLangTokenType op;  // Operatör türü
    
    // Değişken/fonksiyon için
    char* name;
    DataType data_type;
    
    // Fonksiyon için
    struct ASTNode** parameters;  // Parametre listesi
    int param_count;
    struct ASTNode* body;
    
    // If/While/For için
    struct ASTNode* condition;
    struct ASTNode* then_branch;
    struct ASTNode* else_branch;
    
    // For döngüsü için
    struct ASTNode* init;          // for init statement
    struct ASTNode* increment;     // for increment statement
    struct ASTNode* iterable;      // for..in iterable expression
    
    // Block/Program için
    struct ASTNode** statements;
    int statement_count;
    
    // Return için
    struct ASTNode* return_value;
    
    // Fonksiyon çağrısı için
    struct ASTNode** arguments;
    int argument_count;
    
    // Dizi (Array) için
    struct ASTNode** elements;    // Dizi elemanları
    int element_count;            // Eleman sayısı
    struct ASTNode* index;        // Array access için index (arr[index])
    
} ASTNode;

// Parser yapısı
typedef struct {
    Token** tokens;
    int position;
    Token* current_token;
    int token_count;
} Parser;

// Fonksiyon prototipleri
Parser* parser_create(Token** tokens, int token_count);
void parser_free(Parser* parser);
ASTNode* parser_parse(Parser* parser);
ASTNode* ast_node_create(ASTNodeType type);
void ast_node_free(ASTNode* node);
void ast_print(ASTNode* node, int indent);

#endif
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
    AST_OBJECT_LITERAL,   // { "key": value }
    AST_IDENTIFIER,
    AST_BINARY_OP,        // +, -, *, /, ==, !=, <, >, <=, >=
    AST_UNARY_OP,         // -, !
    AST_FUNCTION_CALL,    // fonksiyon çağrısı
    AST_ARRAY_ACCESS,     // arr[0] or obj["key"]
    AST_TYPE_DECL,        // type Person { str name; int age; }
    
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
    TYPE_CUSTOM,
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
    int line;
    int column;
    
    // Değerler (literaller için)
    union {
        long long int_value;
        float float_value;
        char* string_value;
        int bool_value;
    } value;
    
    // Binary operatör için
    struct ASTNode* left;
    struct ASTNode* right;
    TulparTokenType op;  // Operatör türü
    
    // Değişken/fonksiyon için
    char* name;
    DataType data_type;
    // Type decl için
    struct ASTNode** field_types_nodes; // optional
    char** field_names;
    DataType* field_types;
    char** field_custom_types; // TYPE_CUSTOM için hedef type adı
    int field_count;
    struct ASTNode** field_defaults; // alan varsayılan değer ifadeleri (NULL olabilir)
    
    // Fonksiyon için
    struct ASTNode** parameters;  // Parametre listesi
    int param_count;
    struct ASTNode* body;
    // Type method desteği
    char* receiver_type_name;     // func TypeName.method
    struct ASTNode* receiver;     // call sırasında: obj.method()
    
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
    char** argument_names;   // Named argümanlar için; positional ise NULL
    
    // Dizi (Array) için
    struct ASTNode** elements;    // Dizi elemanları
    int element_count;            // Eleman sayısı
    struct ASTNode* index;        // Array access için index (arr[index])
    
    // Object (JSON) için  
    char** object_keys;           // Object key'leri (string array)
    struct ASTNode** object_values; // Object value'ları (ASTNode array)
    int object_count;             // Key-value pair sayısı
    
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"
#include "../lexer/lexer.h"

// Yardımcı fonksiyonlar
static void parser_advance(Parser* parser) {
    if (parser->position < parser->token_count - 1) {
        parser->position++;
        parser->current_token = parser->tokens[parser->position];
    }
}

static Token* parser_peek(Parser* parser) {
    if (parser->position + 1 < parser->token_count) {
        return parser->tokens[parser->position + 1];
    }
    return NULL;
}

static int parser_expect(Parser* parser, TulparTokenType type) {
    if (parser->current_token->type == type) {
        parser_advance(parser);
        return 1;
    }
    printf("Parser Error: Expected token type %d, got %d at line %d\n", 
           type, parser->current_token->type, parser->current_token->line);
    return 0;
}

// AST düğümü oluşturma
ASTNode* ast_node_create(ASTNodeType type) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    node->type = type;
    return node;
}

// AST düğümünü bellekten silme
void ast_node_free(ASTNode* node) {
    if (!node) return;
    
    // String değerleri temizle
    if (node->name) free(node->name);
    if (node->type == AST_STRING_LITERAL && node->value.string_value) {
        free(node->value.string_value);
    }
    
    // Alt düğümleri temizle
    if (node->left) ast_node_free(node->left);
    if (node->right) ast_node_free(node->right);
    if (node->body) ast_node_free(node->body);
    if (node->condition) ast_node_free(node->condition);
    if (node->then_branch) ast_node_free(node->then_branch);
    if (node->else_branch) ast_node_free(node->else_branch);
    if (node->return_value) ast_node_free(node->return_value);
    
    // Dizi düğümlerini temizle
    if (node->parameters) {
        for (int i = 0; i < node->param_count; i++) {
            ast_node_free(node->parameters[i]);
        }
        free(node->parameters);
    }
    
    if (node->arguments) {
        for (int i = 0; i < node->argument_count; i++) {
            ast_node_free(node->arguments[i]);
        }
        free(node->arguments);
    }
    
    if (node->statements) {
        for (int i = 0; i < node->statement_count; i++) {
            ast_node_free(node->statements[i]);
        }
        free(node->statements);
    }
    
    // Object düğümlerini temizle
    if (node->object_keys) {
        for (int i = 0; i < node->object_count; i++) {
            if (node->object_keys[i]) free(node->object_keys[i]);
        }
        free(node->object_keys);
    }
    
    if (node->object_values) {
        for (int i = 0; i < node->object_count; i++) {
            ast_node_free(node->object_values[i]);
        }
        free(node->object_values);
    }
    
    free(node);
}

// Forward declarations
static ASTNode* parse_statement(Parser* parser);
static ASTNode* parse_expression(Parser* parser);
static ASTNode* parse_term(Parser* parser);
static ASTNode* parse_factor(Parser* parser);
static ASTNode* parse_primary(Parser* parser);
static ASTNode* parse_object_literal(Parser* parser);
static ASTNode* parse_assignment(Parser* parser, int expect_semicolon);
static ASTNode* parse_compound_assignment(Parser* parser, int expect_semicolon);
static ASTNode* parse_increment(Parser* parser, int expect_semicolon);
static ASTNode* parse_decrement(Parser* parser, int expect_semicolon);

// Object literal parse ({ "key": value, "key2": value2 })
static ASTNode* parse_object_literal(Parser* parser) {
    ASTNode* node = ast_node_create(AST_OBJECT_LITERAL);
    parser_advance(parser); // '{' atla
    
    node->object_keys = NULL;
    node->object_values = NULL;
    node->object_count = 0;
    
    // Boş object değilse
    if (parser->current_token->type != TOKEN_RBRACE) {
        int capacity = 4;
        node->object_keys = (char**)malloc(sizeof(char*) * capacity);
        node->object_values = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
        
        do {
            // Key (string literal olmalı)
            if (parser->current_token->type != TOKEN_STRING_LITERAL) {
                printf("Parser Error: Object key must be a string at line %d\n", 
                       parser->current_token->line);
                return node;
            }
            
            if (node->object_count >= capacity) {
                capacity *= 2;
                node->object_keys = (char**)realloc(node->object_keys, sizeof(char*) * capacity);
                node->object_values = (ASTNode**)realloc(node->object_values, 
                                                          sizeof(ASTNode*) * capacity);
            }
            
            // Key'i sakla
            node->object_keys[node->object_count] = strdup(parser->current_token->value);
            parser_advance(parser);
            
            // ':' bekle
            if (!parser_expect(parser, TOKEN_COLON)) {
                printf("Parser Error: Expected ':' after object key at line %d\n",
                       parser->current_token->line);
                return node;
            }
            
            // Value'yu parse et
            node->object_values[node->object_count] = parse_expression(parser);
            node->object_count++;
            
            // Virgül varsa devam et
            if (parser->current_token->type == TOKEN_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        } while (1);
    }
    
    parser_expect(parser, TOKEN_RBRACE);
    return node;
}

// Primary expressions (en düşük öncelik)
static ASTNode* parse_primary(Parser* parser) {
    Token* token = parser->current_token;
    
    // Sayı literalleri
    if (token->type == TOKEN_INT_LITERAL) {
        ASTNode* node = ast_node_create(AST_INT_LITERAL);
        char* endptr = NULL;
        long long v = strtoll(token->value, &endptr, 10);
        node->value.int_value = v;
        parser_advance(parser);
        return node;
    }
    
    if (token->type == TOKEN_FLOAT_LITERAL) {
        ASTNode* node = ast_node_create(AST_FLOAT_LITERAL);
        node->value.float_value = atof(token->value);
        parser_advance(parser);
        return node;
    }
    
    // String literalleri
    if (token->type == TOKEN_STRING_LITERAL) {
        ASTNode* node = ast_node_create(AST_STRING_LITERAL);
        node->value.string_value = strdup(token->value);
        parser_advance(parser);
        return node;
    }
    
    // Boolean literalleri
    if (token->type == TOKEN_TRUE || token->type == TOKEN_FALSE) {
        ASTNode* node = ast_node_create(AST_BOOL_LITERAL);
        node->value.bool_value = (token->type == TOKEN_TRUE) ? 1 : 0;
        parser_advance(parser);
        return node;
    }
    
    // Identifier (değişken veya fonksiyon çağrısı)
    if (token->type == TOKEN_IDENTIFIER) {
        char* name = strdup(token->value);
        parser_advance(parser);
        
        // Fonksiyon çağrısı mı?
        if (parser->current_token->type == TOKEN_LPAREN) {
            ASTNode* node = ast_node_create(AST_FUNCTION_CALL);
            node->name = name;
            parser_advance(parser); // '(' atla
            
            // Argümanları parse et
            node->arguments = NULL;
            node->argument_count = 0;
            
            if (parser->current_token->type != TOKEN_RPAREN) {
                int capacity = 4;
                node->arguments = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
                
                do {
                    if (node->argument_count >= capacity) {
                        capacity *= 2;
                        node->arguments = (ASTNode**)realloc(node->arguments, 
                                                             sizeof(ASTNode*) * capacity);
                    }
                    node->arguments[node->argument_count++] = parse_expression(parser);
                    
                    if (parser->current_token->type == TOKEN_COMMA) {
                        parser_advance(parser);
                    } else {
                        break;
                    }
                } while (1);
            }
            
            parser_expect(parser, TOKEN_RPAREN);
            return node;
        }
        
        // Array/Object access (zincirleme destekli: arr[0][1]["key"])
        ASTNode* result = NULL;
        
        if (parser->current_token->type == TOKEN_LBRACKET) {
            // İlk erişim: arr[0]
            result = ast_node_create(AST_ARRAY_ACCESS);
            result->name = name;
            
            parser_advance(parser); // '[' atla
            result->index = parse_expression(parser);
            parser_expect(parser, TOKEN_RBRACKET);
            
            // Zincirleme erişim: [1]["key"][2]...
            while (parser->current_token->type == TOKEN_LBRACKET) {
                ASTNode* nested = ast_node_create(AST_ARRAY_ACCESS);
                nested->left = result;  // Önceki erişimi left'e koy
                nested->name = NULL;    // name artık yok, left var
                
                parser_advance(parser); // '[' atla
                nested->index = parse_expression(parser);
                parser_expect(parser, TOKEN_RBRACKET);
                
                result = nested;
            }
            
            return result;
        }
        
        // Sadece değişken
        ASTNode* node = ast_node_create(AST_IDENTIFIER);
        node->name = name;
        return node;
    }
    
    // Dizi literal ([1, 2, 3])
    if (token->type == TOKEN_LBRACKET) {
        ASTNode* node = ast_node_create(AST_ARRAY_LITERAL);
        parser_advance(parser); // '[' atla
        
        node->elements = NULL;
        node->element_count = 0;
        
        // Boş dizi değilse
        if (parser->current_token->type != TOKEN_RBRACKET) {
            int capacity = 4;
            node->elements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
            
            do {
                if (node->element_count >= capacity) {
                    capacity *= 2;
                    node->elements = (ASTNode**)realloc(node->elements,
                                                        sizeof(ASTNode*) * capacity);
                }
                node->elements[node->element_count++] = parse_expression(parser);
                
                if (parser->current_token->type == TOKEN_COMMA) {
                    parser_advance(parser);
                } else {
                    break;
                }
            } while (1);
        }
        
        parser_expect(parser, TOKEN_RBRACKET);
        return node;
    }
    
    // Object literal ({ "key": value })
    if (token->type == TOKEN_LBRACE) {
        return parse_object_literal(parser);
    }
    
    // Parantez içi ifade
    if (token->type == TOKEN_LPAREN) {
        parser_advance(parser); // '(' atla
        ASTNode* node = parse_expression(parser);
        parser_expect(parser, TOKEN_RPAREN);
        return node;
    }
    
    printf("Parser Error: Unexpected token at line %d\n", token->line);
    return NULL;
}

// Unary expressions (!, -)
static ASTNode* parse_unary(Parser* parser) {
    Token* token = parser->current_token;
    
    // Unary operators
    if (token->type == TOKEN_BANG || token->type == TOKEN_MINUS) {
        ASTNode* node = ast_node_create(AST_UNARY_OP);
        node->op = token->type;
        parser_advance(parser);
        node->left = parse_unary(parser); // Recursive for multiple unary ops
        return node;
    }
    
    return parse_primary(parser);
}

// Factor (*, /)
static ASTNode* parse_factor(Parser* parser) {
    ASTNode* left = parse_unary(parser);
    
    while (parser->current_token->type == TOKEN_MULTIPLY || 
           parser->current_token->type == TOKEN_DIVIDE) {
        TulparTokenType op = parser->current_token->type;
        parser_advance(parser);
        
        ASTNode* node = ast_node_create(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = parse_unary(parser);
        left = node;
    }
    
    return left;
}

// Term (+, -)
static ASTNode* parse_term(Parser* parser) {
    ASTNode* left = parse_factor(parser);
    
    while (parser->current_token->type == TOKEN_PLUS || 
           parser->current_token->type == TOKEN_MINUS) {
        TulparTokenType op = parser->current_token->type;
        parser_advance(parser);
        
        ASTNode* node = ast_node_create(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = parse_factor(parser);
        left = node;
    }
    
    return left;
}

// Comparison (==, !=, <, >, <=, >=)
static ASTNode* parse_comparison(Parser* parser) {
    ASTNode* left = parse_term(parser);
    
    while (parser->current_token->type == TOKEN_EQUAL ||
           parser->current_token->type == TOKEN_NOT_EQUAL ||
           parser->current_token->type == TOKEN_LESS ||
           parser->current_token->type == TOKEN_GREATER ||
           parser->current_token->type == TOKEN_LESS_EQUAL ||
           parser->current_token->type == TOKEN_GREATER_EQUAL) {
        TulparTokenType op = parser->current_token->type;
        parser_advance(parser);
        
        ASTNode* node = ast_node_create(AST_BINARY_OP);
        node->op = op;
        node->left = left;
        node->right = parse_term(parser);
        left = node;
    }
    
    return left;
}

// Logical AND (&&)
static ASTNode* parse_logical_and(Parser* parser) {
    ASTNode* left = parse_comparison(parser);
    
    while (parser->current_token->type == TOKEN_AND) {
        parser_advance(parser);
        
        ASTNode* node = ast_node_create(AST_BINARY_OP);
        node->op = TOKEN_AND;
        node->left = left;
        node->right = parse_comparison(parser);
        left = node;
    }
    
    return left;
}

// Logical OR (||)
static ASTNode* parse_logical_or(Parser* parser) {
    ASTNode* left = parse_logical_and(parser);
    
    while (parser->current_token->type == TOKEN_OR) {
        parser_advance(parser);
        
        ASTNode* node = ast_node_create(AST_BINARY_OP);
        node->op = TOKEN_OR;
        node->left = left;
        node->right = parse_logical_and(parser);
        left = node;
    }
    
    return left;
}

// Expression (en yüksek seviye)
static ASTNode* parse_expression(Parser* parser) {
    return parse_logical_or(parser);
}

// Veri tipini al
static DataType parse_data_type(Parser* parser) {
    DataType type = TYPE_VOID;
    
    switch (parser->current_token->type) {
        case TOKEN_INT_TYPE: type = TYPE_INT; break;
        case TOKEN_FLOAT_TYPE: type = TYPE_FLOAT; break;
        case TOKEN_STR_TYPE: type = TYPE_STRING; break;
        case TOKEN_BOOL_TYPE: type = TYPE_BOOL; break;
        case TOKEN_ARRAY_TYPE: type = TYPE_ARRAY; break;
        case TOKEN_ARRAY_INT: type = TYPE_ARRAY_INT; break;
        case TOKEN_ARRAY_FLOAT: type = TYPE_ARRAY_FLOAT; break;
        case TOKEN_ARRAY_STR: type = TYPE_ARRAY_STR; break;
        case TOKEN_ARRAY_BOOL: type = TYPE_ARRAY_BOOL; break;
        case TOKEN_ARRAY_JSON: type = TYPE_ARRAY_JSON; break;
        default:
            printf("Parser Error: Expected data type at line %d\n", 
                   parser->current_token->line);
            return TYPE_VOID;
    }
    
    parser_advance(parser);
    return type;
}

// Değişken tanımlama: int x = 5;
static ASTNode* parse_variable_declaration(Parser* parser) {
    ASTNode* node = ast_node_create(AST_VARIABLE_DECL);
    node->data_type = parse_data_type(parser);
    
    if (parser->current_token->type != TOKEN_IDENTIFIER) {
        printf("Parser Error: Expected identifier at line %d\n", 
               parser->current_token->line);
        ast_node_free(node);
        return NULL;
    }
    
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);
    
    if (parser->current_token->type == TOKEN_ASSIGN) {
        parser_advance(parser);
        node->right = parse_expression(parser);
    }
    
    parser_expect(parser, TOKEN_SEMICOLON);
    return node;
}

// Atama: x = 10;
static ASTNode* parse_assignment(Parser* parser, int expect_semicolon) {
    ASTNode* node = ast_node_create(AST_ASSIGNMENT);
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);

    parser_expect(parser, TOKEN_ASSIGN);
    node->right = parse_expression(parser);

    if (expect_semicolon) {
        parser_expect(parser, TOKEN_SEMICOLON);
    }

    return node;
}

static ASTNode* parse_compound_assignment(Parser* parser, int expect_semicolon) {
    ASTNode* node = ast_node_create(AST_COMPOUND_ASSIGN);
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);

    // Operator ( +=, -=, *=, /= )
    node->op = parser->current_token->type;
    parser_advance(parser);

    node->right = parse_expression(parser);

    if (expect_semicolon) {
        parser_expect(parser, TOKEN_SEMICOLON);
    }

    return node;
}

static ASTNode* parse_increment(Parser* parser, int expect_semicolon) {
    ASTNode* node = ast_node_create(AST_INCREMENT);
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);

    // '++' tokenını tüket
    parser_advance(parser);

    if (expect_semicolon) {
        parser_expect(parser, TOKEN_SEMICOLON);
    }

    return node;
}

static ASTNode* parse_decrement(Parser* parser, int expect_semicolon) {
    ASTNode* node = ast_node_create(AST_DECREMENT);
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);

    // '--' tokenını tüket
    parser_advance(parser);

    if (expect_semicolon) {
        parser_expect(parser, TOKEN_SEMICOLON);
    }

    return node;
}

// Return statement
static ASTNode* parse_return(Parser* parser) {
    ASTNode* node = ast_node_create(AST_RETURN);
    parser_advance(parser); // 'return' atla
    
    if (parser->current_token->type != TOKEN_SEMICOLON) {
        node->return_value = parse_expression(parser);
    }
    
    parser_expect(parser, TOKEN_SEMICOLON);
    return node;
}

// Break statement
static ASTNode* parse_break(Parser* parser) {
    ASTNode* node = ast_node_create(AST_BREAK);
    parser_advance(parser); // 'break' atla
    parser_expect(parser, TOKEN_SEMICOLON);
    return node;
}

// Continue statement
static ASTNode* parse_continue(Parser* parser) {
    ASTNode* node = ast_node_create(AST_CONTINUE);
    parser_advance(parser); // 'continue' atla
    parser_expect(parser, TOKEN_SEMICOLON);
    return node;
}

// Block: { ... }
static ASTNode* parse_block(Parser* parser) {
    ASTNode* node = ast_node_create(AST_BLOCK);
    parser_advance(parser); // '{' atla
    
    int capacity = 4;
    node->statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->statement_count = 0;
    
    while (parser->current_token->type != TOKEN_RBRACE && 
           parser->current_token->type != TOKEN_EOF) {
        if (node->statement_count >= capacity) {
            capacity *= 2;
            node->statements = (ASTNode**)realloc(node->statements, 
                                                  sizeof(ASTNode*) * capacity);
        }
        
        node->statements[node->statement_count++] = parse_statement(parser);
    }
    
    parser_expect(parser, TOKEN_RBRACE);
    return node;
}

// If statement
static ASTNode* parse_if(Parser* parser) {
    ASTNode* node = ast_node_create(AST_IF);
    parser_advance(parser); // 'if' atla
    
    parser_expect(parser, TOKEN_LPAREN);
    node->condition = parse_expression(parser);
    parser_expect(parser, TOKEN_RPAREN);
    
    node->then_branch = parse_statement(parser);
    
    if (parser->current_token->type == TOKEN_ELSE) {
        parser_advance(parser);
        node->else_branch = parse_statement(parser);
    }
    
    return node;
}

// While loop
static ASTNode* parse_while(Parser* parser) {
    ASTNode* node = ast_node_create(AST_WHILE);
    parser_advance(parser); // 'while' atla
    
    parser_expect(parser, TOKEN_LPAREN);
    node->condition = parse_expression(parser);
    parser_expect(parser, TOKEN_RPAREN);
    
    node->body = parse_statement(parser);
    return node;
}

// For loop - Klasik C-style for
static ASTNode* parse_for(Parser* parser) {
    ASTNode* node = ast_node_create(AST_FOR);
    parser_advance(parser); // 'for' atla

    parser_expect(parser, TOKEN_LPAREN);

    // Başlangıç statement (int i = 0) veya boş olabilir
    if (parser->current_token->type != TOKEN_SEMICOLON) {
        node->init = parse_statement(parser);
    } else {
        node->init = NULL;
        parser_expect(parser, TOKEN_SEMICOLON);
    }

    // Koşul (i < 10) veya boş olabilir
    if (parser->current_token->type != TOKEN_SEMICOLON) {
        node->condition = parse_expression(parser);
    } else {
        node->condition = NULL;
    }
    parser_expect(parser, TOKEN_SEMICOLON);

    // Artırma (i = i + 1, i++, i += 1) veya boş olabilir
    if (parser->current_token->type != TOKEN_RPAREN) {
        if (parser->current_token->type == TOKEN_IDENTIFIER) {
            Token* after = parser_peek(parser);
            if (after) {
                if (after->type == TOKEN_ASSIGN) {
                    node->increment = parse_assignment(parser, 0);
                } else if (after->type == TOKEN_PLUS_EQUAL ||
                           after->type == TOKEN_MINUS_EQUAL ||
                           after->type == TOKEN_MULTIPLY_EQUAL ||
                           after->type == TOKEN_DIVIDE_EQUAL) {
                    node->increment = parse_compound_assignment(parser, 0);
                } else if (after->type == TOKEN_PLUS_PLUS) {
                    node->increment = parse_increment(parser, 0);
                } else if (after->type == TOKEN_MINUS_MINUS) {
                    node->increment = parse_decrement(parser, 0);
                } else {
                    node->increment = parse_expression(parser);
                }
            } else {
                node->increment = parse_expression(parser);
            }
        } else {
            node->increment = parse_expression(parser);
        }
    } else {
        node->increment = NULL;
    }

    parser_expect(parser, TOKEN_RPAREN);

    // Döngü gövdesi
    node->body = parse_statement(parser);

    return node;
}

// For..in loop - foreach style
static ASTNode* parse_for_in(Parser* parser) {
    ASTNode* node = ast_node_create(AST_FOR_IN);
    parser_advance(parser); // 'for' atla
    
    parser_expect(parser, TOKEN_LPAREN);
    
    // Iterator değişken adı
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);
    
    // 'in' keyword
    parser_expect(parser, TOKEN_IN);
    
    // Iterable expression (range(10) gibi)
    node->iterable = parse_expression(parser);
    
    parser_expect(parser, TOKEN_RPAREN);
    
    // Döngü gövdesi
    node->body = parse_statement(parser);
    
    return node;
}

// Fonksiyon tanımlama
static ASTNode* parse_function(Parser* parser) {
    ASTNode* node = ast_node_create(AST_FUNCTION_DECL);
    parser_advance(parser); // 'func' atla
    
    node->name = strdup(parser->current_token->value);
    parser_advance(parser);
    
    parser_expect(parser, TOKEN_LPAREN);
    
    // Parametreleri parse et
    int capacity = 4;
    node->parameters = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    node->param_count = 0;
    
    if (parser->current_token->type != TOKEN_RPAREN) {
        do {
            if (node->param_count >= capacity) {
                capacity *= 2;
                node->parameters = (ASTNode**)realloc(node->parameters, 
                                                      sizeof(ASTNode*) * capacity);
            }
            
            ASTNode* param = ast_node_create(AST_VARIABLE_DECL);
            param->data_type = parse_data_type(parser);
            param->name = strdup(parser->current_token->value);
            parser_advance(parser);
            
            node->parameters[node->param_count++] = param;
            
            if (parser->current_token->type == TOKEN_COMMA) {
                parser_advance(parser);
            } else {
                break;
            }
        } while (1);
    }
    
    parser_expect(parser, TOKEN_RPAREN);
    node->body = parse_block(parser);
    
    return node;
}

// Statement parse etme
static ASTNode* parse_statement(Parser* parser) {
    Token* token = parser->current_token;
    
    // Veri tipi ile başlıyorsa değişken tanımlaması
    if (token->type == TOKEN_INT_TYPE || token->type == TOKEN_FLOAT_TYPE ||
        token->type == TOKEN_STR_TYPE || token->type == TOKEN_BOOL_TYPE ||
        token->type == TOKEN_ARRAY_TYPE || token->type == TOKEN_ARRAY_INT ||
        token->type == TOKEN_ARRAY_FLOAT || token->type == TOKEN_ARRAY_STR ||
        token->type == TOKEN_ARRAY_BOOL || token->type == TOKEN_ARRAY_JSON) {
        return parse_variable_declaration(parser);
    }
    
    // Fonksiyon tanımlaması
    if (token->type == TOKEN_FUNC) {
        return parse_function(parser);
    }
    
    // Return statement
    if (token->type == TOKEN_RETURN) {
        return parse_return(parser);
    }
    
    // Break statement
    if (token->type == TOKEN_BREAK) {
        return parse_break(parser);
    }
    
    // Continue statement
    if (token->type == TOKEN_CONTINUE) {
        return parse_continue(parser);
    }
    
    // If statement
    if (token->type == TOKEN_IF) {
        return parse_if(parser);
    }
    
    // While loop
    if (token->type == TOKEN_WHILE) {
        return parse_while(parser);
    }
    
    // For loop
    if (token->type == TOKEN_FOR) {
        // Lookahead: for (x in ...) vs for (int i = ...)
        int saved_pos = parser->position;
        parser_advance(parser); // for'u atla
        parser_advance(parser); // ( atla
        
        Token* first = parser->current_token;
        parser_advance(parser);
        Token* second = parser->current_token;
        
        // Position'ı geri al
        parser->position = saved_pos;
        parser->current_token = parser->tokens[parser->position];
        
        // Eğer "identifier in" görürsek, foreach
        if (first->type == TOKEN_IDENTIFIER && second->type == TOKEN_IN) {
            return parse_for_in(parser);
        } else {
            return parse_for(parser);
        }
    }
    
    // Block
    if (token->type == TOKEN_LBRACE) {
        return parse_block(parser);
    }
    
    // Identifier (atama veya fonksiyon çağrısı)
    if (token->type == TOKEN_IDENTIFIER) {
        Token* next = parser_peek(parser);
        
        // Array assignment (arr[0] = 5)
        if (next && next->type == TOKEN_LBRACKET) {
            ASTNode* node = ast_node_create(AST_ASSIGNMENT);
            char* name = strdup(parser->current_token->value);
            parser_advance(parser);
            parser_advance(parser); // '[' atla
            
            // arr[index] şeklinde array access oluştur
            ASTNode* access = ast_node_create(AST_ARRAY_ACCESS);
            access->name = name;
            access->index = parse_expression(parser);
            parser_expect(parser, TOKEN_RBRACKET);
            
            node->left = access;  // Sol taraf array access
            parser_expect(parser, TOKEN_ASSIGN);
            node->right = parse_expression(parser);
            parser_expect(parser, TOKEN_SEMICOLON);
            
            return node;
        }
        
        // Atama (=)
        if (next && next->type == TOKEN_ASSIGN) {
            return parse_assignment(parser, 1);
        }
        
        // Compound assignment (+=, -=, *=, /=)
        if (next && (next->type == TOKEN_PLUS_EQUAL || 
                     next->type == TOKEN_MINUS_EQUAL ||
                     next->type == TOKEN_MULTIPLY_EQUAL ||
                     next->type == TOKEN_DIVIDE_EQUAL)) {
            return parse_compound_assignment(parser, 1);
        }
        
        // Increment (++)
        if (next && next->type == TOKEN_PLUS_PLUS) {
            return parse_increment(parser, 1);
        }
        
        // Decrement (--)
        if (next && next->type == TOKEN_MINUS_MINUS) {
            return parse_decrement(parser, 1);
        }
        
        // Fonksiyon çağrısı
        if (next && next->type == TOKEN_LPAREN) {
            ASTNode* call = parse_primary(parser);
            if (parser->current_token->type == TOKEN_SEMICOLON) {
                parser_advance(parser);
            }
            return call;
        }
    }
    
    printf("Parser Error: Unexpected statement at line %d\n", token->line);
    parser_advance(parser);
    return NULL;
}

// Parser oluşturma
Parser* parser_create(Token** tokens, int token_count) {
    Parser* parser = (Parser*)malloc(sizeof(Parser));
    parser->tokens = tokens;
    parser->token_count = token_count;
    parser->position = 0;
    parser->current_token = tokens[0];
    return parser;
}

// Parser bellekten silme
void parser_free(Parser* parser) {
    if (parser) {
        free(parser);
    }
}

// Program parse etme
ASTNode* parser_parse(Parser* parser) {
    ASTNode* program = ast_node_create(AST_PROGRAM);

    int capacity = 4;
    program->statements = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);
    program->statement_count = 0;

    while (parser->current_token->type != TOKEN_EOF) {
        if (program->statement_count >= capacity) {
            capacity *= 2;
            program->statements = (ASTNode**)realloc(program->statements,
                                                     sizeof(ASTNode*) * capacity);
        }

        ASTNode* stmt = parse_statement(parser);
        if (stmt) {
            program->statements[program->statement_count++] = stmt;
        }

        // parse_statement genellikle noktalı virgülü tüketir; burada ekstra işlem yok
    }

    return program;
}

// AST yazdırma (debug için)
void ast_print(ASTNode* node, int indent) {
    if (!node) return;
    
    for (int i = 0; i < indent; i++) printf("  ");
    
    switch (node->type) {
        case AST_PROGRAM:
            printf("PROGRAM\n");
            for (int i = 0; i < node->statement_count; i++) {
                ast_print(node->statements[i], indent + 1);
            }
            break;
            
        case AST_INT_LITERAL:
            printf("INT: %lld\n", node->value.int_value);
            break;
            
        case AST_FLOAT_LITERAL:
            printf("FLOAT: %f\n", node->value.float_value);
            break;
            
        case AST_STRING_LITERAL:
            printf("STRING: \"%s\"\n", node->value.string_value);
            break;
            
        case AST_BOOL_LITERAL:
            printf("BOOL: %s\n", node->value.bool_value ? "true" : "false");
            break;
            
        case AST_IDENTIFIER:
            printf("IDENTIFIER: %s\n", node->name);
            break;
            
        case AST_BINARY_OP:
            printf("BINARY_OP: %d\n", node->op);
            ast_print(node->left, indent + 1);
            ast_print(node->right, indent + 1);
            break;
            
        case AST_VARIABLE_DECL:
            printf("VAR_DECL: %s (type: %d)\n", node->name, node->data_type);
            if (node->right) ast_print(node->right, indent + 1);
            break;
            
        case AST_ASSIGNMENT:
            printf("ASSIGNMENT: %s\n", node->name);
            ast_print(node->right, indent + 1);
            break;
            
        case AST_FUNCTION_DECL:
            printf("FUNC_DECL: %s\n", node->name);
            printf("  Parameters:\n");
            for (int i = 0; i < node->param_count; i++) {
                ast_print(node->parameters[i], indent + 2);
            }
            printf("  Body:\n");
            ast_print(node->body, indent + 2);
            break;
            
        case AST_FUNCTION_CALL:
            printf("FUNC_CALL: %s\n", node->name);
            for (int i = 0; i < node->argument_count; i++) {
                ast_print(node->arguments[i], indent + 1);
            }
            break;
            
        case AST_RETURN:
            printf("RETURN\n");
            if (node->return_value) ast_print(node->return_value, indent + 1);
            break;
            
        case AST_IF:
            printf("IF\n");
            printf("  Condition:\n");
            ast_print(node->condition, indent + 2);
            printf("  Then:\n");
            ast_print(node->then_branch, indent + 2);
            if (node->else_branch) {
                printf("  Else:\n");
                ast_print(node->else_branch, indent + 2);
            }
            break;
            
        case AST_WHILE:
            printf("WHILE\n");
            printf("  Condition:\n");
            ast_print(node->condition, indent + 2);
            printf("  Body:\n");
            ast_print(node->body, indent + 2);
            break;
            
        case AST_FOR:
            printf("FOR\n");
            printf("  Init:\n");
            ast_print(node->init, indent + 2);
            printf("  Condition:\n");
            ast_print(node->condition, indent + 2);
            printf("  Increment:\n");
            ast_print(node->increment, indent + 2);
            printf("  Body:\n");
            ast_print(node->body, indent + 2);
            break;
            
        case AST_FOR_IN:
            printf("FOR_IN: %s\n", node->name);
            printf("  Iterable:\n");
            ast_print(node->iterable, indent + 2);
            printf("  Body:\n");
            ast_print(node->body, indent + 2);
            break;
            
        case AST_BLOCK:
            printf("BLOCK\n");
            for (int i = 0; i < node->statement_count; i++) {
                ast_print(node->statements[i], indent + 1);
            }
            break;
            
        default:
            printf("UNKNOWN\n");
    }
}
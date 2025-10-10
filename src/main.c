#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "interpreter/interpreter.h"

// Dosyadan kaynak kodu oku
char* read_file(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        printf("Hata: '%s' dosyasi acilamadi!\n", filename);
        return NULL;
    }
    
    // Dosya boyutunu bul
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Bellek ayır ve oku
    char* buffer = (char*)malloc(size + 1);
    fread(buffer, 1, size, file);
    buffer[size] = '\0';
    
    fclose(file);
    return buffer;
}

int main(int argc, char** argv) {
    // Windows'ta UTF-8 desteği
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    #endif
    
    // Locale ayarla
    setlocale(LC_ALL, "en_US.UTF-8");
    
    char* source = NULL;
    int from_file = 0;
    
    // Komut satırı argümanlarını kontrol et
    if (argc > 1) {
        // Dosyadan oku
        source = read_file(argv[1]);
        if (!source) {
            return 1;
        }
        from_file = 1;
    } else {
        // Varsayılan test kodu
        source = strdup(
            "int x = 5;\n"
            "float pi = 3.14;\n"
            "str isim = \"Ahmet\";\n"
            "bool aktif = true;\n"
            "\n"
            "func topla(int a, int b) {\n"
            "    int sonuc = a + b;\n"
            "    return sonuc;\n"
            "}\n"
            "\n"
            "int toplam = topla(5, 3);\n"
            "\n"
            "func fibonacci(int n) {\n"
            "    if (n <= 1) {\n"
            "        return n;\n"
            "    }\n"
            "    int a = fibonacci(n - 1);\n"
            "    int b = fibonacci(n - 2);\n"
            "    return a + b;\n"
            "}\n"
            "\n"
            "int fib5 = fibonacci(5);\n"
        );
    }
    
    // Başlık
    if (!from_file) {
        printf("========================================\n");
        printf("   OLang Interpreter - Demo\n");
        printf("========================================\n\n");
        printf("Kaynak Kod:\n");
        printf("-------------------\n%s\n", source);
        printf("-------------------\n\n");
    } else {
        printf("OLang calistiriliyor: %s\n\n", argv[1]);
    }
    
    // ========================================
    // 1. LEXER (Tokenization)
    // ========================================
    if (!from_file) {
        printf("1. LEXER (Tokenization)\n");
        printf("========================\n");
    }
    Lexer* lexer = lexer_create(source);
    
    // Token dizisi oluştur
    int token_capacity = 100;
    int token_count = 0;
    Token** tokens = (Token**)malloc(sizeof(Token*) * token_capacity);
    
    Token* token;
    while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
        if (token_count >= token_capacity) {
            token_capacity *= 2;
            tokens = (Token**)realloc(tokens, sizeof(Token*) * token_capacity);
        }
        tokens[token_count++] = token;
        if (!from_file) {
            token_print(token);
        }
    }
    tokens[token_count++] = token; // EOF token'ı ekle
    
    lexer_free(lexer);
    
    // ========================================
    // 2. PARSER (AST Oluşturma)
    // ========================================
    if (!from_file) {
        printf("\n2. PARSER (AST Olusturma)\n");
        printf("==========================\n");
    }
    
    Parser* parser = parser_create(tokens, token_count);
    ASTNode* ast = parser_parse(parser);
    
    if (!from_file) {
        printf("Abstract Syntax Tree:\n");
        ast_print(ast, 0);
    }
    
    // ========================================
    // 3. INTERPRETER (Kodu Çalıştırma)
    // ========================================
    if (!from_file) {
        printf("\n3. INTERPRETER (Kodu Calistirma)\n");
        printf("=================================\n");
    }
    
    Interpreter* interp = interpreter_create();
    interpreter_execute(interp, ast);
    
    // Sonuçları göster
    if (!from_file) {
        printf("\nDegisken Degerleri:\n");
        printf("-------------------\n");
    } else {
        printf("Sonuc:\n");
        printf("------\n");
    }
    
    // Tüm global değişkenleri göster
    for (int i = 0; i < interp->global_scope->var_count; i++) {
        printf("%s = ", interp->global_scope->variables[i]->name);
        value_print(interp->global_scope->variables[i]->value);
        printf("\n");
    }
    
    if (!from_file) {
        printf("\n========================================\n");
        printf("   OLang basariyla calisti! ✓\n");
        printf("========================================\n");
    } else {
        printf("\n✓ Basariyla tamamlandi.\n");
    }
    
    // Temizlik
    interpreter_free(interp);
    ast_node_free(ast);
    parser_free(parser);
    
    for (int i = 0; i < token_count; i++) {
        token_free(tokens[i]);
    }
    free(tokens);
    free(source);
    
    return 0;
}


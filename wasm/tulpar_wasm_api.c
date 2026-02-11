/**
 * Tulpar WebAssembly API
 * 
 * WebAssembly için basit C API sağlar.
 * Print çıktısını buffer'a yazar ve JavaScript callback'e yönlendirir.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <emscripten.h>

#include "../src/lexer/lexer.h"
#include "../src/parser/parser.h"
#include "../src/vm/compiler.h"
#include "../src/vm/vm.h"

// Output buffer için global değişkenler
static char *g_output_buffer = NULL;
static size_t g_output_size = 0;
static size_t g_output_capacity = 0;

// JavaScript callback fonksiyonu pointer'ı (set edilecek)
static void (*g_print_callback)(const char *) = NULL;

// Output buffer'a string ekle (extern olarak kullanılacak)
void append_to_output(const char *str) {
    if (!str) return;
    
    size_t len = strlen(str);
    if (len == 0) return;
    
    size_t needed = g_output_size + len + 1;
    if (needed > g_output_capacity) {
        size_t new_capacity = g_output_capacity == 0 ? 4096 : g_output_capacity * 2;
        while (new_capacity < needed) new_capacity *= 2;
        
        char *new_buffer = realloc(g_output_buffer, new_capacity);
        if (!new_buffer) return;
        
        g_output_buffer = new_buffer;
        g_output_capacity = new_capacity;
    }
    
    memcpy(g_output_buffer + g_output_size, str, len);
    g_output_size += len;
    g_output_buffer[g_output_size] = '\0';
    
    // JavaScript callback'e gönder
    if (g_print_callback) {
        g_print_callback(str);
    }
}

// Print wrapper - printf yerine kullanılacak
static int wasm_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // Geçici buffer için yeterli alan ayır
    char temp_buffer[4096];
    int len = vsnprintf(temp_buffer, sizeof(temp_buffer), format, args);
    
    va_end(args);
    
    if (len < 0) return len;
    if (len >= (int)sizeof(temp_buffer)) len = sizeof(temp_buffer) - 1;
    temp_buffer[len] = '\0';
    
    append_to_output(temp_buffer);
    
    return len;
}

// Print callback'i ayarla (JavaScript'ten çağrılacak)
EMSCRIPTEN_KEEPALIVE
void tulpar_wasm_set_print_callback(void (*callback)(const char *)) {
    g_print_callback = callback;
}

// VM instance
static VM *g_vm = NULL;

/**
 * WebAssembly modülünü başlat
 * @return 0 başarılı, -1 hata
 */
EMSCRIPTEN_KEEPALIVE
int tulpar_wasm_init(void) {
    if (g_vm != NULL) {
        return 0; // Zaten başlatılmış
    }
    
    // Output buffer'ı başlat
    g_output_capacity = 4096;
    g_output_buffer = malloc(g_output_capacity);
    if (!g_output_buffer) {
        return -1;
    }
    g_output_buffer[0] = '\0';
    g_output_size = 0;
    
    // VM oluştur
    g_vm = vm_create();
    if (!g_vm) {
        free(g_output_buffer);
        g_output_buffer = NULL;
        return -1;
    }
    
    return 0;
}

/**
 * Tulpar kodunu çalıştır
 * @param code Tulpar kaynak kodu
 * @return 0 başarılı, -1 derleme hatası, -2 runtime hatası
 */
EMSCRIPTEN_KEEPALIVE
int tulpar_wasm_run_code(const char *code) {
    if (!g_vm) {
        if (tulpar_wasm_init() != 0) {
            return -1;
        }
    }
    
    // Output buffer'ı temizle
    g_output_size = 0;
    if (g_output_buffer) {
        g_output_buffer[0] = '\0';
    }
    
    if (!code || strlen(code) == 0) {
        return 0;
    }
    
    // 1. Lexer
    Lexer *lexer = lexer_create(code);
    if (!lexer) {
        return -1;
    }
    
    // Token dizisi oluştur
    int token_capacity = 100;
    int token_count = 0;
    Token **tokens = (Token **)malloc(sizeof(Token *) * token_capacity);
    if (!tokens) {
        lexer_free(lexer);
        return -1;
    }
    
    Token *token;
    while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
        if (token_count >= token_capacity) {
            token_capacity *= 2;
            Token **new_tokens = realloc(tokens, sizeof(Token *) * token_capacity);
            if (!new_tokens) {
                // Cleanup
                for (int i = 0; i < token_count; i++) {
                    token_free(tokens[i]);
                }
                free(tokens);
                lexer_free(lexer);
                return -1;
            }
            tokens = new_tokens;
        }
        tokens[token_count++] = token;
    }
    tokens[token_count++] = token; // EOF token
    
    lexer_free(lexer);
    
    // 2. Parser
    Parser *parser = parser_create(tokens, token_count);
    if (!parser) {
        for (int i = 0; i < token_count; i++) {
            token_free(tokens[i]);
        }
        free(tokens);
        return -1;
    }
    
    ASTNode *ast = parser_parse(parser);
    if (!ast) {
        parser_free(parser);
        for (int i = 0; i < token_count; i++) {
            token_free(tokens[i]);
        }
        free(tokens);
        return -1;
    }
    
    // 3. Compiler
    Chunk *chunk = compile(ast);
    if (!chunk) {
        ast_node_free(ast);
        parser_free(parser);
        for (int i = 0; i < token_count; i++) {
            token_free(tokens[i]);
        }
        free(tokens);
        return -1;
    }
    
    // 4. VM'de çalıştır
    ObjFunction *script = vm_new_function(g_vm);
    script->chunk = *chunk;
    free(chunk);
    
    VMResult result = vm_run(g_vm, script);
    
    // Cleanup
    ast_node_free(ast);
    parser_free(parser);
    for (int i = 0; i < token_count; i++) {
        token_free(tokens[i]);
    }
    free(tokens);
    
    if (result == VM_COMPILE_ERROR) {
        return -1;
    } else if (result == VM_RUNTIME_ERROR) {
        return -2;
    }
    
    return 0;
}

/**
 * Output buffer'ı al
 * @return Output string (caller tarafından free edilmemeli)
 */
EMSCRIPTEN_KEEPALIVE
const char *tulpar_wasm_get_output(void) {
    return g_output_buffer ? g_output_buffer : "";
}

/**
 * Output buffer uzunluğunu al
 */
EMSCRIPTEN_KEEPALIVE
size_t tulpar_wasm_get_output_length(void) {
    return g_output_size;
}

/**
 * Temizlik yap
 */
EMSCRIPTEN_KEEPALIVE
void tulpar_wasm_cleanup(void) {
    if (g_vm) {
        vm_free(g_vm);
        g_vm = NULL;
    }
    
    if (g_output_buffer) {
        free(g_output_buffer);
        g_output_buffer = NULL;
    }
    
    g_output_size = 0;
    g_output_capacity = 0;
}

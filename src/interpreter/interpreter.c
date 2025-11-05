#include "interpreter.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

// ============================================================================
// UTF-8 YARDIMCI FONKSİYONLARI
// ============================================================================

// UTF-8 karakterinin byte uzunluğunu döndürür
static int utf8_char_length(unsigned char c) {
    if ((c & 0x80) == 0) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

// UTF-8 string içindeki karakter sayısını döndürür
static int utf8_strlen_cp(const char* str) {
    int length = 0;
    int i = 0;

    while (str[i] != '\0') {
        int char_len = utf8_char_length((unsigned char)str[i]);
        i += char_len;
        length++;
    }

    return length;
}

// UTF-8 string içinden belirtilen index'teki karakteri (string olarak) döndürür
static char* utf8_char_at(const char* str, int index) {
    int i = 0;
    int current = 0;

    while (str[i] != '\0' && current < index) {
        int char_len = utf8_char_length((unsigned char)str[i]);
        i += char_len;
        current++;
    }

    if (str[i] == '\0') {
        return NULL; // Sınır dışında
    }

    int char_len = utf8_char_length((unsigned char)str[i]);
    char* buffer = (char*)malloc((size_t)char_len + 1);
    memcpy(buffer, str + i, (size_t)char_len);
    buffer[char_len] = '\0';
    return buffer;
}

// ============================================================================
// VALUE FONKSİYONLARI
// ============================================================================

Value* value_create_int(long long val) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_INT;
    value->data.int_val = val;
    return value;
}

Value* value_create_float(float val) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_FLOAT;
    value->data.float_val = val;
    return value;
}

Value* value_create_string(char* val) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_STRING;
    value->data.string_val = strdup(val);
    return value;
}

Value* value_create_bool(int val) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_BOOL;
    value->data.bool_val = val;
    return value;
}

Value* value_create_void() {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_VOID;
    return value;
}

// ==========================
// BigInt yardımcıları (pozitif tamsayılar)
// Temsil: yalnızca rakamlardan oluşan decimal string, ön sıfırlar temizlenmiş
// ==========================

static char* bigint_trim(const char* s) {
    while (*s == '0' && s[1] != '\0') s++;
    return strdup(s);
}

static char* bigint_from_ll_str(long long x) {
    if (x <= 0) {
        if (x == 0) return strdup("0");
        // Negatif desteklenmiyor: mutlak değere çevir (ileride işaret eklenebilir)
        unsigned long long ux = (unsigned long long)(-(x + 1)) + 1ULL;
        char buf[32];
        snprintf(buf, sizeof(buf), "%llu", ux);
        return bigint_trim(buf);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", x);
    return bigint_trim(buf);
}

static char* bigint_add_str(const char* a, const char* b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    int L = (la > lb ? la : lb) + 1;
    char* out = (char*)malloc((size_t)L + 1);
    int ia = la - 1, ib = lb - 1, io = L;
    int carry = 0;
    out[io] = '\0';
    while (ia >= 0 || ib >= 0 || carry) {
        int da = (ia >= 0) ? (a[ia] - '0') : 0;
        int db = (ib >= 0) ? (b[ib] - '0') : 0;
        int s = da + db + carry;
        out[--io] = (char)('0' + (s % 10));
        carry = s / 10;
        ia--; ib--;
    }
    while (io > 0) out[--io] = '0';
    char* trimmed = bigint_trim(out + io);
    free(out);
    return trimmed;
}

static char* bigint_mul_str(const char* a, const char* b) {
    if (a[0] == '0' || b[0] == '0') return strdup("0");
    int la = (int)strlen(a), lb = (int)strlen(b);
    int L = la + lb;
    int* tmp = (int*)calloc((size_t)L, sizeof(int));
    for (int i = la - 1; i >= 0; i--) {
        int da = a[i] - '0';
        for (int j = lb - 1; j >= 0; j--) {
            int db = b[j] - '0';
            tmp[i + j + 1] += da * db;
        }
    }
    for (int k = L - 1; k > 0; k--) {
        tmp[k - 1] += tmp[k] / 10;
        tmp[k] %= 10;
    }
    char* out = (char*)malloc((size_t)L + 1);
    for (int i = 0; i < L; i++) out[i] = (char)('0' + tmp[i]);
    out[L] = '\0';
    free(tmp);
    char* trimmed = bigint_trim(out);
    free(out);
    return trimmed;
}

static char* bigint_mul_small(const char* a, long long b) {
    if (b == 0) return strdup("0");
    if (b < 0) b = -b; // işaret desteklenmiyor (ileride eklenebilir)
    char buf[32];
    snprintf(buf, sizeof(buf), "%lld", b);
    return bigint_mul_str(a, buf);
}

Value* value_create_array(int capacity) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_ARRAY;
    
    Array* arr = (Array*)malloc(sizeof(Array));
    arr->elements = (Value**)malloc(sizeof(Value*) * capacity);
    arr->length = 0;
    arr->capacity = capacity;
    arr->elem_type = VAL_VOID;  // Mixed type
    
    value->data.array_val = arr;
    return value;
}

Value* value_create_typed_array(int capacity, ValueType elem_type) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_ARRAY;
    
    Array* arr = (Array*)malloc(sizeof(Array));
    arr->elements = (Value**)malloc(sizeof(Value*) * capacity);
    arr->length = 0;
    arr->capacity = capacity;
    arr->elem_type = elem_type;  // Typed array
    
    value->data.array_val = arr;
    return value;
}

Value* value_create_object() {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_OBJECT;
    value->data.object_val = hash_table_create(16);  // 16 buckets başlangıç
    return value;
}

Value* value_create_bigint(const char* digits) {
    Value* value = (Value*)malloc(sizeof(Value));
    value->type = VAL_BIGINT;
    value->data.bigint_val = bigint_trim(digits);
    return value;
}

// ============================================================================
// HASH TABLE FONKSİYONLARI
// ============================================================================

// Simple hash function (djb2 algorithm)
unsigned int hash_function(const char* key, int bucket_count) {
    unsigned long hash = 5381;
    int c;
    
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    
    return hash % bucket_count;
}

// Hash table oluştur
HashTable* hash_table_create(int bucket_count) {
    HashTable* table = (HashTable*)malloc(sizeof(HashTable));
    table->bucket_count = bucket_count;
    table->size = 0;
    table->buckets = (HashEntry**)calloc(bucket_count, sizeof(HashEntry*));
    return table;
}

// Hash table temizle
void hash_table_free(HashTable* table) {
    if (!table) return;
    
    for (int i = 0; i < table->bucket_count; i++) {
        HashEntry* entry = table->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            free(entry->key);
            value_free(entry->value);
            free(entry);
            entry = next;
        }
    }
    
    free(table->buckets);
    free(table);
}

// Key-value ekle/güncelle
void hash_table_set(HashTable* table, const char* key, Value* value) {
    unsigned int index = hash_function(key, table->bucket_count);
    HashEntry* entry = table->buckets[index];
    
    // Key zaten var mı kontrol et
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            // Güncelle
            value_free(entry->value);
            entry->value = value;
            return;
        }
        entry = entry->next;
    }
    
    // Yeni entry oluştur
    HashEntry* new_entry = (HashEntry*)malloc(sizeof(HashEntry));
    new_entry->key = strdup(key);
    new_entry->value = value;
    new_entry->next = table->buckets[index];
    table->buckets[index] = new_entry;
    table->size++;
}

// Key ile value al
Value* hash_table_get(HashTable* table, const char* key) {
    unsigned int index = hash_function(key, table->bucket_count);
    HashEntry* entry = table->buckets[index];
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            return entry->value;
        }
        entry = entry->next;
    }
    
    return NULL;  // Bulunamadı
}

// Key var mı kontrol et
int hash_table_has(HashTable* table, const char* key) {
    return hash_table_get(table, key) != NULL;
}

// Key sil
void hash_table_delete(HashTable* table, const char* key) {
    unsigned int index = hash_function(key, table->bucket_count);
    HashEntry* entry = table->buckets[index];
    HashEntry* prev = NULL;
    
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            if (prev) {
                prev->next = entry->next;
            } else {
                table->buckets[index] = entry->next;
            }
            
            free(entry->key);
            value_free(entry->value);
            free(entry);
            table->size--;
            return;
        }
        prev = entry;
        entry = entry->next;
    }
}

// Hash table yazdır (debug)
void hash_table_print(HashTable* table) {
    printf("{");
    int first = 1;
    
    for (int i = 0; i < table->bucket_count; i++) {
        HashEntry* entry = table->buckets[i];
        while (entry) {
            if (!first) printf(", ");
            printf("\"%s\": ", entry->key);
            value_print(entry->value);
            first = 0;
            entry = entry->next;
        }
    }
    
    printf("}");
}

// ============================================================================
// ARRAY FONKSİYONLARI
// ============================================================================

void array_push(Array* arr, Value* val) {
    // Tip kontrolü (eğer typed array ise)
    if (arr->elem_type != VAL_VOID && arr->elem_type != val->type) {
        printf("Hata: Dizi sadece ");
        switch (arr->elem_type) {
            case VAL_INT: printf("int"); break;
            case VAL_FLOAT: printf("float"); break;
            case VAL_STRING: printf("str"); break;
            case VAL_BOOL: printf("bool"); break;
            default: printf("bilinmeyen tip"); break;
        }
        printf(" tipinde eleman kabul eder!\n");
        return;
    }
    
    if (arr->length >= arr->capacity) {
        arr->capacity *= 2;
        arr->elements = (Value**)realloc(arr->elements, 
                                        sizeof(Value*) * arr->capacity);
    }
    arr->elements[arr->length++] = value_copy(val);
}

Value* array_pop(Array* arr) {
    if (arr->length == 0) {
        return value_create_void();
    }
    Value* val = arr->elements[--arr->length];
    return val;  // Ownership transfer
}

Value* array_get(Array* arr, int index) {
    if (index < 0 || index >= arr->length) {
        printf("Hata: Dizi index sınırları dışında: %d (uzunluk: %d)\n", 
               index, arr->length);
        return value_create_void();
    }
    return value_copy(arr->elements[index]);
}

void array_set(Array* arr, int index, Value* val) {
    if (index < 0 || index >= arr->length) {
        printf("Hata: Dizi index sınırları dışında: %d (uzunluk: %d)\n", 
               index, arr->length);
        return;
    }
    
    // Tip kontrolü (eğer typed array ise)
    if (arr->elem_type != VAL_VOID && arr->elem_type != val->type) {
        printf("Hata: Dizi sadece ");
        switch (arr->elem_type) {
            case VAL_INT: printf("int"); break;
            case VAL_FLOAT: printf("float"); break;
            case VAL_STRING: printf("str"); break;
            case VAL_BOOL: printf("bool"); break;
            default: printf("bilinmeyen tip"); break;
        }
        printf(" tipinde eleman kabul eder!\n");
        return;
    }
    
    value_free(arr->elements[index]);
    arr->elements[index] = value_copy(val);
}

// ============================================================================
// VALUE FONKSİYONLARI
// ============================================================================

Value* value_copy(Value* val) {
    if (!val) return NULL;
    
    Value* copy = (Value*)malloc(sizeof(Value));
    copy->type = val->type;
    
    switch (val->type) {
        case VAL_INT:
            copy->data.int_val = val->data.int_val;
            break;
        case VAL_FLOAT:
            copy->data.float_val = val->data.float_val;
            break;
        case VAL_BIGINT:
            copy->data.bigint_val = strdup(val->data.bigint_val);
            break;
        case VAL_STRING:
            copy->data.string_val = strdup(val->data.string_val);
            break;
        case VAL_BOOL:
            copy->data.bool_val = val->data.bool_val;
            break;
        case VAL_ARRAY: {
            Array* src = val->data.array_val;
            Array* dst = (Array*)malloc(sizeof(Array));
            dst->capacity = src->capacity;
            dst->length = src->length;
            dst->elem_type = src->elem_type;
            dst->elements = (Value**)malloc(sizeof(Value*) * dst->capacity);
            
            for (int i = 0; i < src->length; i++) {
                dst->elements[i] = value_copy(src->elements[i]);
            }
            
            copy->data.array_val = dst;
            break;
        }
        case VAL_OBJECT: {
            // Deep copy hash table
            HashTable* src = val->data.object_val;
            HashTable* dst = hash_table_create(src->bucket_count);
            
            for (int i = 0; i < src->bucket_count; i++) {
                HashEntry* entry = src->buckets[i];
                while (entry) {
                    hash_table_set(dst, entry->key, value_copy(entry->value));
                    entry = entry->next;
                }
            }
            
            copy->data.object_val = dst;
            break;
        }
        default:
            break;
    }
    
    return copy;
}

void value_free(Value* val) {
    if (!val) return;
    
    if (val->type == VAL_STRING && val->data.string_val) {
        free(val->data.string_val);
    }
    
    if (val->type == VAL_ARRAY && val->data.array_val) {
        Array* arr = val->data.array_val;
        for (int i = 0; i < arr->length; i++) {
            value_free(arr->elements[i]);
        }
        free(arr->elements);
        free(arr);
    }
    
    if (val->type == VAL_OBJECT && val->data.object_val) {
        hash_table_free(val->data.object_val);
    }
    if (val->type == VAL_BIGINT && val->data.bigint_val) {
        free(val->data.bigint_val);
    }
    
    free(val);
}

void value_print(Value* val) {
    if (!val) {
        printf("NULL");
        return;
    }
    
    switch (val->type) {
        case VAL_INT:
            printf("%lld", val->data.int_val);
            break;
        case VAL_FLOAT:
            printf("%g", val->data.float_val);
            break;
        case VAL_BIGINT:
            printf("%s", val->data.bigint_val);
            break;
        case VAL_STRING:
            printf("%s", val->data.string_val);
            break;
        case VAL_BOOL:
            printf("%s", val->data.bool_val ? "true" : "false");
            break;
        case VAL_ARRAY: {
            Array* arr = val->data.array_val;
            printf("[");
            for (int i = 0; i < arr->length; i++) {
                value_print(arr->elements[i]);
                if (i < arr->length - 1) {
                    printf(", ");
                }
            }
            printf("]");
            break;
        }
        case VAL_OBJECT:
            hash_table_print(val->data.object_val);
            break;
        case VAL_VOID:
            printf("void");
            break;
        default:
            printf("unknown");
            break;
    }
}

int value_is_truthy(Value* val) {
    if (!val) return 0;
    
    switch (val->type) {
        case VAL_BOOL:
            return val->data.bool_val;
        case VAL_INT:
            return val->data.int_val != 0;
        case VAL_FLOAT:
            return val->data.float_val != 0.0f;
        case VAL_STRING:
            return val->data.string_val != NULL && strlen(val->data.string_val) > 0;
        default:
            return 0;
    }
}

// ============================================================================
// SYMBOL TABLE FONKSİYONLARI
// ============================================================================

SymbolTable* symbol_table_create(SymbolTable* parent) {
    SymbolTable* table = (SymbolTable*)malloc(sizeof(SymbolTable));
    table->var_capacity = 16;
    table->var_count = 0;
    table->variables = (Variable**)malloc(sizeof(Variable*) * table->var_capacity);
    table->parent = parent;
    return table;
}

void symbol_table_free(SymbolTable* table) {
    if (!table) return;
    
    for (int i = 0; i < table->var_count; i++) {
        free(table->variables[i]->name);
        value_free(table->variables[i]->value);
        free(table->variables[i]);
    }
    free(table->variables);
    free(table);
}

void symbol_table_set(SymbolTable* table, char* name, Value* value) {
    // Önce mevcut değişkeni ara
    for (int i = 0; i < table->var_count; i++) {
        if (strcmp(table->variables[i]->name, name) == 0) {
            // Mevcut değişkeni güncelle
            value_free(table->variables[i]->value);
            table->variables[i]->value = value_copy(value);
            return;
        }
    }
    
    // Yeni değişken ekle
    if (table->var_count >= table->var_capacity) {
        table->var_capacity *= 2;
        table->variables = (Variable**)realloc(table->variables, 
                                               sizeof(Variable*) * table->var_capacity);
    }
    
    Variable* var = (Variable*)malloc(sizeof(Variable));
    var->name = strdup(name);
    var->value = value_copy(value);
    table->variables[table->var_count++] = var;
}

Value* symbol_table_get(SymbolTable* table, char* name) {
    // Mevcut scope'ta ara
    for (int i = 0; i < table->var_count; i++) {
        if (strcmp(table->variables[i]->name, name) == 0) {
            return table->variables[i]->value;
        }
    }
    
    // Parent scope'ta ara
    if (table->parent) {
        return symbol_table_get(table->parent, name);
    }
    
    return NULL;
}

// ============================================================================
// INTERPRETER FONKSİYONLARI
// ============================================================================

Interpreter* interpreter_create() {
    Interpreter* interp = (Interpreter*)malloc(sizeof(Interpreter));
    interp->global_scope = symbol_table_create(NULL);
    interp->current_scope = interp->global_scope;
    interp->function_capacity = 16;
    interp->function_count = 0;
    interp->functions = (Function**)malloc(sizeof(Function*) * interp->function_capacity);
    interp->type_capacity = 8;
    interp->type_count = 0;
    interp->types = (TypeDef**)malloc(sizeof(TypeDef*) * interp->type_capacity);
    interp->return_value = NULL;
    interp->should_return = 0;
    interp->should_break = 0;
    interp->should_continue = 0;
    return interp;
}

void interpreter_free(Interpreter* interp) {
    if (!interp) return;
    
    symbol_table_free(interp->global_scope);
    
    for (int i = 0; i < interp->function_count; i++) {
        free(interp->functions[i]->name);
        free(interp->functions[i]);
    }
    free(interp->functions);
    for (int i = 0; i < interp->type_count; i++) {
        for (int j = 0; j < interp->types[i]->field_count; j++) free(interp->types[i]->field_names[j]);
        free(interp->types[i]->field_names);
        free(interp->types[i]->field_types);
        free(interp->types[i]->name);
        free(interp->types[i]);
    }
    free(interp->types);
    
    if (interp->return_value) {
        value_free(interp->return_value);
    }
    
    free(interp);
}

void interpreter_register_function(Interpreter* interp, char* name, ASTNode* node) {
    if (interp->function_count >= interp->function_capacity) {
        interp->function_capacity *= 2;
        interp->functions = (Function**)realloc(interp->functions,
                                                sizeof(Function*) * interp->function_capacity);
    }
    
    Function* func = (Function*)malloc(sizeof(Function));
    func->name = strdup(name);
    func->node = node;
    interp->functions[interp->function_count++] = func;
}

Function* interpreter_get_function(Interpreter* interp, char* name) {
    for (int i = 0; i < interp->function_count; i++) {
        if (strcmp(interp->functions[i]->name, name) == 0) {
            return interp->functions[i];
        }
    }
    return NULL;
}

TypeDef* interpreter_get_type(Interpreter* interp, const char* name) {
    for (int i = 0; i < interp->type_count; i++) {
        if (strcmp(interp->types[i]->name, name) == 0) return interp->types[i];
    }
    return NULL;
}

void interpreter_register_type(Interpreter* interp, TypeDef* t) {
    if (interp->type_count >= interp->type_capacity) {
        interp->type_capacity *= 2;
        interp->types = (TypeDef**)realloc(interp->types, sizeof(TypeDef*) * interp->type_capacity);
    }
    interp->types[interp->type_count++] = t;
}

// İleri bildirimler
Value* interpreter_eval_expression(Interpreter* interp, ASTNode* node);
void interpreter_execute_statement(Interpreter* interp, ASTNode* node);

// ============================================================================
// İFADE DEĞERLENDİRME (Expression Evaluation)
// ============================================================================

Value* interpreter_eval_expression(Interpreter* interp, ASTNode* node) {
    if (!node) return value_create_void();
    
    switch (node->type) {
        case AST_INT_LITERAL:
            return value_create_int(node->value.int_value);
            
        case AST_FLOAT_LITERAL:
            return value_create_float(node->value.float_value);
            
        case AST_STRING_LITERAL:
            return value_create_string(node->value.string_value);
            
        case AST_BOOL_LITERAL:
            return value_create_bool(node->value.bool_value);
        
        case AST_ARRAY_LITERAL: {
            // Dizi literal: [1, 2, 3]
            Value* arr = value_create_array(node->element_count > 0 ? node->element_count : 4);
            
            for (int i = 0; i < node->element_count; i++) {
                Value* elem = interpreter_eval_expression(interp, node->elements[i]);
                array_push(arr->data.array_val, elem);
                value_free(elem);
            }
            
            return arr;
        }
        
        case AST_OBJECT_LITERAL: {
            // Object literal: { "key": value, "key2": value2 }
            Value* obj = value_create_object();
            
            for (int i = 0; i < node->object_count; i++) {
                const char* key = node->object_keys[i];
                Value* value = interpreter_eval_expression(interp, node->object_values[i]);
                hash_table_set(obj->data.object_val, key, value);
            }
            
            return obj;
        }
        
        case AST_ARRAY_ACCESS: {
            // Array/Object erişimi: arr[0] or obj["key"] or nested arr[0][1]["key"]
            Value* container = NULL;
            
            // Zincirleme erişim mi? (left var mı?)
            if (node->left) {
                // Nested access: önce left'i değerlendir
                container = interpreter_eval_expression(interp, node->left);
            } else {
                // İlk erişim: değişkenden al
                container = symbol_table_get(interp->current_scope, node->name);
                if (!container) {
                    printf("Hata: Tanımlanmamış değişken '%s'\n", node->name);
                    exit(1);
                }
                // Symbol table'dan aldığımız için copy yapma (referans)
                // Ama nested'de eval'den gelirse zaten yeni value
            }
            
            Value* index_val = interpreter_eval_expression(interp, node->index);
            Value* result = NULL;
            
            // Array access (integer index)
            if (container->type == VAL_ARRAY) {
                if (index_val->type != VAL_INT) {
                    printf("Hata: Dizi index integer olmalı\n");
                    value_free(index_val);
                    if (node->left) value_free(container);
                    exit(1);
                }
                
                int index = index_val->data.int_val;
                value_free(index_val);
                result = array_get(container->data.array_val, index);
                
                // Left'ten gelen container'ı temizle
                if (node->left) value_free(container);
                return result;
            }
            
            // Object access (string key)
            if (container->type == VAL_OBJECT) {
                if (index_val->type != VAL_STRING) {
                    printf("Hata: Object key string olmalı\n");
                    value_free(index_val);
                    if (node->left) value_free(container);
                    exit(1);
                }
                
                const char* key = index_val->data.string_val;
                Value* found = hash_table_get(container->data.object_val, key);
                value_free(index_val);
                
                if (!found) {
                    printf("Hata: Object'te '%s' key'i bulunamadı\n", key);
                    if (node->left) value_free(container);
                    return value_create_void();
                }
                
                result = value_copy(found);
                
                // Left'ten gelen container'ı temizle
                if (node->left) value_free(container);
                return result;
            }
            
            // String access (character by index)
            if (container->type == VAL_STRING) {
                if (index_val->type != VAL_INT) {
                    printf("Hata: String index integer olmalı\n");
                    value_free(index_val);
                    if (node->left) value_free(container);
                    exit(1);
                }
                
                int idx = index_val->data.int_val;
                const char* str = container->data.string_val;
                int len = utf8_strlen_cp(str);
                
                // Index sınır kontrolü
                if (idx < 0 || idx >= len) {
                    printf("Hata: String index sınırların dışında (0-%d arası olmalı, %d verildi)\n", 
                           len - 1, idx);
                    value_free(index_val);
                    if (node->left) value_free(container);
                    exit(1);
                }
                
                char* char_str = utf8_char_at(str, idx);
                if (!char_str) {
                    printf("Hata: UTF-8 karakter çözümlenemedi\n");
                    value_free(index_val);
                    if (node->left) value_free(container);
                    exit(1);
                }

                result = value_create_string(char_str);
                free(char_str);
                
                value_free(index_val);
                if (node->left) value_free(container);
                return result;
            }
            
            printf("Hata: Erişilen değer bir dizi, object veya string değil\n");
            value_free(index_val);
            if (node->left) value_free(container);
            exit(1);
        }
            
        case AST_IDENTIFIER: {
            Value* val = symbol_table_get(interp->current_scope, node->name);
            if (!val) {
                printf("Hata: Tanımlanmamış değişken '%s'\n", node->name);
                exit(1);
            }
            return value_copy(val);
        }
            
        case AST_BINARY_OP: {
            Value* left = interpreter_eval_expression(interp, node->left);
            Value* right = interpreter_eval_expression(interp, node->right);
            Value* result = NULL;
            
            // Aritmetik operatörler
            if (node->op == TOKEN_PLUS) {
                if (left->type == VAL_BIGINT || right->type == VAL_BIGINT) {
                    const char* la = (left->type == VAL_BIGINT) ? left->data.bigint_val : bigint_from_ll_str(left->data.int_val);
                    const char* rb = (right->type == VAL_BIGINT) ? right->data.bigint_val : bigint_from_ll_str(right->data.int_val);
                    char* sum = bigint_add_str(la, rb);
                    if (left->type != VAL_BIGINT) free((char*)la);
                    if (right->type != VAL_BIGINT) free((char*)rb);
                    result = value_create_bigint(sum);
                    free(sum);
                } else if (left->type == VAL_INT && right->type == VAL_INT) {
                    long long a = left->data.int_val;
                    long long b = right->data.int_val;
                    if ((b > 0 && a > (LLONG_MAX - b)) || (b < 0 && a < (LLONG_MIN - b))) {
                        char* sa = bigint_from_ll_str(a);
                        char* sb = bigint_from_ll_str(b);
                        char* sum = bigint_add_str(sa, sb);
                        free(sa); free(sb);
                        result = value_create_bigint(sum);
                        free(sum);
                    } else {
                        result = value_create_int(a + b);
                    }
                } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_float(l + r);
                } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
                    // String birleştirme
                    char* str = (char*)malloc(strlen(left->data.string_val) + 
                                            strlen(right->data.string_val) + 1);
                    strcpy(str, left->data.string_val);
                    strcat(str, right->data.string_val);
                    result = value_create_string(str);
                    free(str);
                }
            }
            else if (node->op == TOKEN_MINUS) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    long long a = left->data.int_val;
                    long long b = right->data.int_val;
                    // Taşma kontrolü: a - b == a + (-b)
                    if ((-b > 0 && a > (LLONG_MAX + b)) || (-b < 0 && a < (LLONG_MIN + b))) {
                        float l = (float)a;
                        float r = (float)b;
                        result = value_create_float(l - r);
                    } else {
                        result = value_create_int(a - b);
                    }
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_float(l - r);
                }
            }
            else if (node->op == TOKEN_MULTIPLY) {
                if (left->type == VAL_BIGINT || right->type == VAL_BIGINT) {
                    if (left->type == VAL_BIGINT && right->type == VAL_BIGINT) {
                        char* prod = bigint_mul_str(left->data.bigint_val, right->data.bigint_val);
                        result = value_create_bigint(prod);
                        free(prod);
                    } else {
                        const char* big = (left->type == VAL_BIGINT) ? left->data.bigint_val : right->data.bigint_val;
                        long long small = (left->type == VAL_INT) ? left->data.int_val : right->data.int_val;
                        char* prod = bigint_mul_small(big, small);
                        result = value_create_bigint(prod);
                        free(prod);
                    }
                } else if (left->type == VAL_INT && right->type == VAL_INT) {
                    long long a = left->data.int_val;
                    long long b = right->data.int_val;
                    if (a != 0 && ((b > 0 && (a > LLONG_MAX / b || a < LLONG_MIN / b))
                                   || (b < 0 && (a == LLONG_MIN || -a > LLONG_MAX / -b)))) {
                        char* sa = bigint_from_ll_str(a);
                        char* sb = bigint_from_ll_str(b);
                        char* prod = bigint_mul_str(sa, sb);
                        free(sa); free(sb);
                        result = value_create_bigint(prod);
                        free(prod);
                    } else {
                        result = value_create_int(a * b);
                    }
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_float(l * r);
                }
            }
            else if (node->op == TOKEN_DIVIDE) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    if (right->data.int_val == 0) {
                        printf("Hata: Sıfıra bölme!\n");
                        exit(1);
                    }
                    result = value_create_int(left->data.int_val / right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    if (r == 0.0f) {
                        printf("Hata: Sıfıra bölme!\n");
                        exit(1);
                    }
                    result = value_create_float(l / r);
                }
            }
            // Karşılaştırma operatörleri
            else if (node->op == TOKEN_EQUAL) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val == right->data.int_val);
                } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l == r);
                } else if (left->type == VAL_BOOL && right->type == VAL_BOOL) {
                    result = value_create_bool(left->data.bool_val == right->data.bool_val);
                } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
                    result = value_create_bool(strcmp(left->data.string_val, right->data.string_val) == 0);
                }
            }
            else if (node->op == TOKEN_NOT_EQUAL) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val != right->data.int_val);
                } else if (left->type == VAL_FLOAT || right->type == VAL_FLOAT) {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l != r);
                } else if (left->type == VAL_BOOL && right->type == VAL_BOOL) {
                    result = value_create_bool(left->data.bool_val != right->data.bool_val);
                } else if (left->type == VAL_STRING && right->type == VAL_STRING) {
                    result = value_create_bool(strcmp(left->data.string_val, right->data.string_val) != 0);
                }
            }
            else if (node->op == TOKEN_LESS) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val < right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l < r);
                }
            }
            else if (node->op == TOKEN_GREATER) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val > right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l > r);
                }
            }
            else if (node->op == TOKEN_LESS_EQUAL) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val <= right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l <= r);
                }
            }
            else if (node->op == TOKEN_GREATER_EQUAL) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_bool(left->data.int_val >= right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_bool(l >= r);
                }
            }
            // Mantıksal operatörler
            else if (node->op == TOKEN_AND) {
                int left_truthy = value_is_truthy(left);
                int right_truthy = value_is_truthy(right);
                result = value_create_bool(left_truthy && right_truthy);
            }
            else if (node->op == TOKEN_OR) {
                int left_truthy = value_is_truthy(left);
                int right_truthy = value_is_truthy(right);
                result = value_create_bool(left_truthy || right_truthy);
            }
            
            value_free(left);
            value_free(right);
            
            if (!result) {
                printf("Hata: Desteklenmeyen operatör!\n");
                exit(1);
            }
            
            return result;
        }
        
        case AST_UNARY_OP: {
            Value* operand = interpreter_eval_expression(interp, node->left);
            Value* result = NULL;
            
            if (node->op == TOKEN_BANG) {
                // Logical NOT
                int truthy = value_is_truthy(operand);
                result = value_create_bool(!truthy);
            }
            else if (node->op == TOKEN_MINUS) {
                // Unary minus
                if (operand->type == VAL_INT) {
                    result = value_create_int(-operand->data.int_val);
                } else if (operand->type == VAL_FLOAT) {
                    result = value_create_float(-operand->data.float_val);
                }
            }
            
            value_free(operand);
            
            if (!result) {
                printf("Hata: Desteklenmeyen unary operatör!\n");
                exit(1);
            }
            
            return result;
        }
            
        case AST_FUNCTION_CALL: {
            // Built-in fonksiyonları kontrol et
            
            // print() fonksiyonu
            if (strcmp(node->name, "print") == 0) {
                for (int i = 0; i < node->argument_count; i++) {
                    Value* val = interpreter_eval_expression(interp, node->arguments[i]);
                    value_print(val);
                    if (i < node->argument_count - 1) {
                        printf(" ");
                    }
                    value_free(val);
                }
                printf("\n");
                return value_create_void();
            }
            
            // input() fonksiyonu - string okur
            if (strcmp(node->name, "input") == 0) {
                // Prompt varsa yazdır
                if (node->argument_count > 0) {
                    Value* prompt = interpreter_eval_expression(interp, node->arguments[0]);
                    if (prompt->type == VAL_STRING) {
                        printf("%s", prompt->data.string_val);
                        fflush(stdout);
                    }
                    value_free(prompt);
                }
                
                // Kullanıcıdan input al
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), stdin)) {
                    // Satır sonu karakterini kaldır
                    size_t len = strlen(buffer);
                    if (len > 0 && buffer[len-1] == '\n') {
                        buffer[len-1] = '\0';
                    }
                    return value_create_string(buffer);
                }
                return value_create_string("");
            }
            
            // inputInt() fonksiyonu - integer okur
            if (strcmp(node->name, "inputInt") == 0) {
                // Prompt varsa yazdır
                if (node->argument_count > 0) {
                    Value* prompt = interpreter_eval_expression(interp, node->arguments[0]);
                    if (prompt->type == VAL_STRING) {
                        printf("%s", prompt->data.string_val);
                        fflush(stdout);
                    }
                    value_free(prompt);
                }
                
                // Kullanıcıdan input al
                long long num;
                if (scanf("%lld", &num) == 1) {
                    // Buffer'ı temizle
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF);
                    return value_create_int(num);
                }
                return value_create_int(0);
            }
            
            // inputFloat() fonksiyonu - float okur
            if (strcmp(node->name, "inputFloat") == 0) {
                // Prompt varsa yazdır
                if (node->argument_count > 0) {
                    Value* prompt = interpreter_eval_expression(interp, node->arguments[0]);
                    if (prompt->type == VAL_STRING) {
                        printf("%s", prompt->data.string_val);
                        fflush(stdout);
                    }
                    value_free(prompt);
                }
                
                // Kullanıcıdan input al
                float num;
                if (scanf("%f", &num) == 1) {
                    // Buffer'ı temizle
                    int c;
                    while ((c = getchar()) != '\n' && c != EOF);
                    return value_create_float(num);
                }
                return value_create_float(0.0f);
            }
            
            // range() fonksiyonu - foreach için
            if (strcmp(node->name, "range") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    if (arg->type == VAL_INT) {
                        long long count = arg->data.int_val;
                        value_free(arg);
                        return value_create_int(count);
                    }
                    value_free(arg);
                }
                return value_create_int(0);
            }
            
            // toInt() - type conversion
            if (strcmp(node->name, "toInt") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    Value* result = NULL;
                    
                    if (arg->type == VAL_INT) {
                        result = value_create_int(arg->data.int_val);
                    } else if (arg->type == VAL_FLOAT) {
                        result = value_create_int((long long)arg->data.float_val);
                    } else if (arg->type == VAL_BOOL) {
                        result = value_create_int(arg->data.bool_val ? 1 : 0);
                    } else if (arg->type == VAL_STRING) {
                        char* endptr = NULL;
                        long long v = strtoll(arg->data.string_val, &endptr, 10);
                        result = value_create_int(v);
                    }
                    
                    value_free(arg);
                    return result ? result : value_create_int(0);
                }
                return value_create_int(0);
            }
            
            // toFloat() - type conversion
            if (strcmp(node->name, "toFloat") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    Value* result = NULL;
                    
                    if (arg->type == VAL_INT) {
                        result = value_create_float((float)arg->data.int_val);
                    } else if (arg->type == VAL_FLOAT) {
                        result = value_create_float(arg->data.float_val);
                    } else if (arg->type == VAL_BOOL) {
                        result = value_create_float(arg->data.bool_val ? 1.0f : 0.0f);
                    } else if (arg->type == VAL_STRING) {
                        result = value_create_float(atof(arg->data.string_val));
                    }
                    
                    value_free(arg);
                    return result ? result : value_create_float(0.0f);
                }
                return value_create_float(0.0f);
            }
            
            // toString() - type conversion
            if (strcmp(node->name, "toString") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    char buffer[256];
                    
                    if (arg->type == VAL_INT) {
                        snprintf(buffer, sizeof(buffer), "%lld", arg->data.int_val);
                    } else if (arg->type == VAL_FLOAT) {
                        snprintf(buffer, sizeof(buffer), "%g", arg->data.float_val);
                    } else if (arg->type == VAL_BOOL) {
                        snprintf(buffer, sizeof(buffer), "%s", arg->data.bool_val ? "true" : "false");
                    } else if (arg->type == VAL_STRING) {
                        value_free(arg);
                        return value_create_string(arg->data.string_val);
                    } else {
                        buffer[0] = '\0';
                    }
                    
                    value_free(arg);
                    return value_create_string(buffer);
                }
                return value_create_string("");
            }
            
            // toBool() - type conversion
            if (strcmp(node->name, "toBool") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    int result = value_is_truthy(arg);
                    value_free(arg);
                    return value_create_bool(result);
                }
                return value_create_bool(0);
            }
            
            // length() - dizi veya string uzunluğu
            if (strcmp(node->name, "length") == 0) {
                if (node->argument_count > 0) {
                    Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                    int len = 0;
                    
                    if (arg->type == VAL_ARRAY) {
                        len = arg->data.array_val->length;
                    } else if (arg->type == VAL_STRING) {
                        len = strlen(arg->data.string_val);
                    }
                    
                    value_free(arg);
                    return value_create_int(len);
                }
                return value_create_int(0);
            }
            
            // push() - diziye eleman ekle
            if (strcmp(node->name, "push") == 0) {
                if (node->argument_count >= 2) {
                    // İlk argüman: dizi değişkeni (identifier olarak gelir)
                    if (node->arguments[0]->type == AST_IDENTIFIER) {
                        char* arr_name = node->arguments[0]->name;
                        Value* arr_val = symbol_table_get(interp->current_scope, arr_name);
                        
                        if (arr_val && arr_val->type == VAL_ARRAY) {
                            Value* elem = interpreter_eval_expression(interp, node->arguments[1]);
                            array_push(arr_val->data.array_val, elem);
                            value_free(elem);
                            return value_create_void();
                        }
                    }
                }
                return value_create_void();
            }
            
            // pop() - diziden eleman çıkar
            if (strcmp(node->name, "pop") == 0) {
                if (node->argument_count >= 1) {
                    // İlk argüman: dizi değişkeni (identifier olarak gelir)
                    if (node->arguments[0]->type == AST_IDENTIFIER) {
                        char* arr_name = node->arguments[0]->name;
                        Value* arr_val = symbol_table_get(interp->current_scope, arr_name);
                        
                        if (arr_val && arr_val->type == VAL_ARRAY) {
                            return array_pop(arr_val->data.array_val);
                        }
                    }
                }
                return value_create_void();
            }
            
            // ========================================================================
            // MATEMATİK FONKSİYONLARI
            // ========================================================================
            
            // Helper: Get numeric value as double
            #define GET_NUM_ARG(idx) ({ \
                Value* _arg = interpreter_eval_expression(interp, node->arguments[idx]); \
                double _val = 0.0; \
                if (_arg->type == VAL_INT) _val = (double)_arg->data.int_val; \
                else if (_arg->type == VAL_FLOAT) _val = (double)_arg->data.float_val; \
                value_free(_arg); \
                _val; \
            })
            
            // abs(x) - mutlak değer
            if (strcmp(node->name, "abs") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)fabs(x));
            }
            
            // sqrt(x) - karekök
            if (strcmp(node->name, "sqrt") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)sqrt(x));
            }
            
            // pow(x, y) - üs alma
            if (strcmp(node->name, "pow") == 0 && node->argument_count >= 2) {
                double x = GET_NUM_ARG(0);
                double y = GET_NUM_ARG(1);
                return value_create_float((float)pow(x, y));
            }
            
            // floor(x) - aşağı yuvarlama
            if (strcmp(node->name, "floor") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_int((int)floor(x));
            }
            
            // ceil(x) - yukarı yuvarlama
            if (strcmp(node->name, "ceil") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_int((int)ceil(x));
            }
            
            // round(x) - yuvarlama
            if (strcmp(node->name, "round") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_int((int)round(x));
            }
            
            // sin(x) - sinüs
            if (strcmp(node->name, "sin") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)sin(x));
            }
            
            // cos(x) - kosinüs
            if (strcmp(node->name, "cos") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)cos(x));
            }
            
            // tan(x) - tanjant
            if (strcmp(node->name, "tan") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)tan(x));
            }
            
            // asin(x) - arcsinüs
            if (strcmp(node->name, "asin") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)asin(x));
            }
            
            // acos(x) - arckosinüs
            if (strcmp(node->name, "acos") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)acos(x));
            }
            
            // atan(x) - arctanjant
            if (strcmp(node->name, "atan") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)atan(x));
            }
            
            // atan2(y, x) - iki argümanlı arctanjant
            if (strcmp(node->name, "atan2") == 0 && node->argument_count >= 2) {
                double y = GET_NUM_ARG(0);
                double x = GET_NUM_ARG(1);
                return value_create_float((float)atan2(y, x));
            }
            
            // exp(x) - e üzeri x
            if (strcmp(node->name, "exp") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)exp(x));
            }
            
            // log(x) - doğal logaritma (ln)
            if (strcmp(node->name, "log") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)log(x));
            }
            
            // log10(x) - 10 tabanlı logaritma
            if (strcmp(node->name, "log10") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)log10(x));
            }
            
            // log2(x) - 2 tabanlı logaritma
            if (strcmp(node->name, "log2") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)log2(x));
            }
            
            // sinh(x) - hiperbolik sinüs
            if (strcmp(node->name, "sinh") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)sinh(x));
            }
            
            // cosh(x) - hiperbolik kosinüs
            if (strcmp(node->name, "cosh") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)cosh(x));
            }
            
            // tanh(x) - hiperbolik tanjant
            if (strcmp(node->name, "tanh") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)tanh(x));
            }
            
            // min(a, b, ...) - minimum değer
            if (strcmp(node->name, "min") == 0 && node->argument_count >= 1) {
                double min_val = GET_NUM_ARG(0);
                for (int i = 1; i < node->argument_count; i++) {
                    double val = GET_NUM_ARG(i);
                    if (val < min_val) min_val = val;
                }
                return value_create_float((float)min_val);
            }
            
            // max(a, b, ...) - maximum değer
            if (strcmp(node->name, "max") == 0 && node->argument_count >= 1) {
                double max_val = GET_NUM_ARG(0);
                for (int i = 1; i < node->argument_count; i++) {
                    double val = GET_NUM_ARG(i);
                    if (val > max_val) max_val = val;
                }
                return value_create_float((float)max_val);
            }
            
            // random() - 0 ile 1 arası rastgele sayı
            if (strcmp(node->name, "random") == 0) {
                static int seeded = 0;
                if (!seeded) {
                    srand((unsigned int)time(NULL));
                    seeded = 1;
                }
                return value_create_float((float)rand() / (float)RAND_MAX);
            }
            
            // randint(a, b) - a ile b arası rastgele tam sayı
            if (strcmp(node->name, "randint") == 0 && node->argument_count >= 2) {
                static int seeded = 0;
                if (!seeded) {
                    srand((unsigned int)time(NULL));
                    seeded = 1;
                }
                int a = (int)GET_NUM_ARG(0);
                int b = (int)GET_NUM_ARG(1);
                int result = a + rand() % (b - a + 1);
                return value_create_int(result);
            }
            
            // cbrt(x) - küp kök
            if (strcmp(node->name, "cbrt") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_float((float)cbrt(x));
            }
            
            // hypot(x, y) - hipotenüs (sqrt(x^2 + y^2))
            if (strcmp(node->name, "hypot") == 0 && node->argument_count >= 2) {
                double x = GET_NUM_ARG(0);
                double y = GET_NUM_ARG(1);
                return value_create_float((float)hypot(x, y));
            }
            
            // fmod(x, y) - kayan nokta mod
            if (strcmp(node->name, "fmod") == 0 && node->argument_count >= 2) {
                double x = GET_NUM_ARG(0);
                double y = GET_NUM_ARG(1);
                return value_create_float((float)fmod(x, y));
            }
            
            // ========================================================================
            // STRING FONKSİYONLARI
            // ========================================================================
            
            // Helper: Get string argument
            #define GET_STR_ARG(idx) ({ \
                Value* _arg = interpreter_eval_expression(interp, node->arguments[idx]); \
                char* _str = NULL; \
                if (_arg->type == VAL_STRING) _str = _arg->data.string_val; \
                _str; \
            })
            
            // upper(s) - büyük harfe çevir
            if (strcmp(node->name, "upper") == 0 && node->argument_count >= 1) {
                Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                if (arg->type == VAL_STRING) {
                    char* str = strdup(arg->data.string_val);
                    for (int i = 0; str[i]; i++) {
                        str[i] = toupper((unsigned char)str[i]);
                    }
                    value_free(arg);
                    Value* result = value_create_string(str);
                    free(str);
                    return result;
                }
                value_free(arg);
                return value_create_string("");
            }
            
            // lower(s) - küçük harfe çevir
            if (strcmp(node->name, "lower") == 0 && node->argument_count >= 1) {
                Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                if (arg->type == VAL_STRING) {
                    char* str = strdup(arg->data.string_val);
                    for (int i = 0; str[i]; i++) {
                        str[i] = tolower((unsigned char)str[i]);
                    }
                    value_free(arg);
                    Value* result = value_create_string(str);
                    free(str);
                    return result;
                }
                value_free(arg);
                return value_create_string("");
            }
            
            // trim(s) - baş ve sondaki boşlukları sil
            if (strcmp(node->name, "trim") == 0 && node->argument_count >= 1) {
                Value* arg = interpreter_eval_expression(interp, node->arguments[0]);
                if (arg->type == VAL_STRING) {
                    char* str = arg->data.string_val;
                    char* start = str;
                    char* end = str + strlen(str) - 1;
                    
                    // Baştan boşlukları atla
                    while (*start && isspace((unsigned char)*start)) start++;
                    
                    // Sondan boşlukları atla
                    while (end > start && isspace((unsigned char)*end)) end--;
                    
                    // Yeni string oluştur
                    int len = end - start + 1;
                    char* result_str = (char*)malloc(len + 1);
                    strncpy(result_str, start, len);
                    result_str[len] = '\0';
                    
                    value_free(arg);
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                value_free(arg);
                return value_create_string("");
            }
            
            // replace(s, old, new) - değiştir
            if (strcmp(node->name, "replace") == 0 && node->argument_count >= 3) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* old_val = interpreter_eval_expression(interp, node->arguments[1]);
                Value* new_val = interpreter_eval_expression(interp, node->arguments[2]);
                
                if (str_val->type == VAL_STRING && old_val->type == VAL_STRING && new_val->type == VAL_STRING) {
                    char* str = str_val->data.string_val;
                    char* old = old_val->data.string_val;
                    char* new = new_val->data.string_val;
                    
                    int old_len = strlen(old);
                    int new_len = strlen(new);
                    
                    // Kaç kez geçiyor say
                    int count = 0;
                    char* p = str;
                    while ((p = strstr(p, old)) != NULL) {
                        count++;
                        p += old_len;
                    }
                    
                    // Yeni string için yer ayır
                    int result_len = strlen(str) + count * (new_len - old_len);
                    char* result_str = (char*)malloc(result_len + 1);
                    char* dst = result_str;
                    
                    // Replace işlemi
                    p = str;
                    char* found;
                    while ((found = strstr(p, old)) != NULL) {
                        int prefix_len = found - p;
                        strncpy(dst, p, prefix_len);
                        dst += prefix_len;
                        strcpy(dst, new);
                        dst += new_len;
                        p = found + old_len;
                    }
                    strcpy(dst, p);
                    
                    value_free(str_val);
                    value_free(old_val);
                    value_free(new_val);
                    
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                
                value_free(str_val);
                value_free(old_val);
                value_free(new_val);
                return value_create_string("");
            }
            
            // contains(s, sub) - alt string var mı
            if (strcmp(node->name, "contains") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* sub_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                int result = 0;
                if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
                    result = (strstr(str_val->data.string_val, sub_val->data.string_val) != NULL);
                }
                
                value_free(str_val);
                value_free(sub_val);
                return value_create_bool(result);
            }
            
            // startsWith(s, prefix) - ile başlıyor mu
            if (strcmp(node->name, "startsWith") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* prefix_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                int result = 0;
                if (str_val->type == VAL_STRING && prefix_val->type == VAL_STRING) {
                    result = (strncmp(str_val->data.string_val, prefix_val->data.string_val, 
                                     strlen(prefix_val->data.string_val)) == 0);
                }
                
                value_free(str_val);
                value_free(prefix_val);
                return value_create_bool(result);
            }
            
            // endsWith(s, suffix) - ile bitiyor mu
            if (strcmp(node->name, "endsWith") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* suffix_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                int result = 0;
                if (str_val->type == VAL_STRING && suffix_val->type == VAL_STRING) {
                    int str_len = strlen(str_val->data.string_val);
                    int suffix_len = strlen(suffix_val->data.string_val);
                    
                    if (suffix_len <= str_len) {
                        result = (strcmp(str_val->data.string_val + str_len - suffix_len, 
                                        suffix_val->data.string_val) == 0);
                    }
                }
                
                value_free(str_val);
                value_free(suffix_val);
                return value_create_bool(result);
            }
            
            // indexOf(s, sub) - ilk konum (-1 = bulunamadı)
            if (strcmp(node->name, "indexOf") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* sub_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                int result = -1;
                if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
                    char* found = strstr(str_val->data.string_val, sub_val->data.string_val);
                    if (found) {
                        result = found - str_val->data.string_val;
                    }
                }
                
                value_free(str_val);
                value_free(sub_val);
                return value_create_int(result);
            }
            
            // substring(s, start, end) - alt string
            if (strcmp(node->name, "substring") == 0 && node->argument_count >= 3) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* start_val = interpreter_eval_expression(interp, node->arguments[1]);
                Value* end_val = interpreter_eval_expression(interp, node->arguments[2]);
                
                if (str_val->type == VAL_STRING && start_val->type == VAL_INT && end_val->type == VAL_INT) {
                    char* str = str_val->data.string_val;
                    int start = start_val->data.int_val;
                    int end = end_val->data.int_val;
                    int len = strlen(str);
                    
                    // Sınır kontrolleri
                    if (start < 0) start = 0;
                    if (end > len) end = len;
                    if (start > end) start = end;
                    
                    int sub_len = end - start;
                    char* result_str = (char*)malloc(sub_len + 1);
                    strncpy(result_str, str + start, sub_len);
                    result_str[sub_len] = '\0';
                    
                    value_free(str_val);
                    value_free(start_val);
                    value_free(end_val);
                    
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                
                value_free(str_val);
                value_free(start_val);
                value_free(end_val);
                return value_create_string("");
            }
            
            // repeat(s, n) - tekrarla
            if (strcmp(node->name, "repeat") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* n_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                if (str_val->type == VAL_STRING && n_val->type == VAL_INT) {
                    char* str = str_val->data.string_val;
                    int n = n_val->data.int_val;
                    int str_len = strlen(str);
                    
                    if (n <= 0) {
                        value_free(str_val);
                        value_free(n_val);
                        return value_create_string("");
                    }
                    
                    char* result_str = (char*)malloc(str_len * n + 1);
                    result_str[0] = '\0';
                    
                    for (int i = 0; i < n; i++) {
                        strcat(result_str, str);
                    }
                    
                    value_free(str_val);
                    value_free(n_val);
                    
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                
                value_free(str_val);
                value_free(n_val);
                return value_create_string("");
            }
            
            // reverse(s) - ters çevir
            if (strcmp(node->name, "reverse") == 0 && node->argument_count >= 1) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                
                if (str_val->type == VAL_STRING) {
                    char* str = str_val->data.string_val;
                    int len = strlen(str);
                    char* result_str = (char*)malloc(len + 1);
                    
                    for (int i = 0; i < len; i++) {
                        result_str[i] = str[len - 1 - i];
                    }
                    result_str[len] = '\0';
                    
                    value_free(str_val);
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                
                value_free(str_val);
                return value_create_string("");
            }
            
            // isEmpty(s) - boş mu
            if (strcmp(node->name, "isEmpty") == 0 && node->argument_count >= 1) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                
                int result = 1;
                if (str_val->type == VAL_STRING) {
                    result = (strlen(str_val->data.string_val) == 0);
                }
                
                value_free(str_val);
                return value_create_bool(result);
            }
            
            // count(s, sub) - kaç kez geçiyor
            if (strcmp(node->name, "count") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* sub_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                int count = 0;
                if (str_val->type == VAL_STRING && sub_val->type == VAL_STRING) {
                    char* str = str_val->data.string_val;
                    char* sub = sub_val->data.string_val;
                    int sub_len = strlen(sub);
                    
                    char* p = str;
                    while ((p = strstr(p, sub)) != NULL) {
                        count++;
                        p += sub_len;
                    }
                }
                
                value_free(str_val);
                value_free(sub_val);
                return value_create_int(count);
            }
            
            // capitalize(s) - ilk harf büyük
            if (strcmp(node->name, "capitalize") == 0 && node->argument_count >= 1) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                
                if (str_val->type == VAL_STRING) {
                    char* str = strdup(str_val->data.string_val);
                    
                    if (str[0]) {
                        str[0] = toupper((unsigned char)str[0]);
                        for (int i = 1; str[i]; i++) {
                            str[i] = tolower((unsigned char)str[i]);
                        }
                    }
                    
                    value_free(str_val);
                    Value* result = value_create_string(str);
                    free(str);
                    return result;
                }
                
                value_free(str_val);
                return value_create_string("");
            }
            
            // isDigit(s) - sadece rakam mı
            if (strcmp(node->name, "isDigit") == 0 && node->argument_count >= 1) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                
                int result = 0;
                if (str_val->type == VAL_STRING && strlen(str_val->data.string_val) > 0) {
                    result = 1;
                    for (char* p = str_val->data.string_val; *p; p++) {
                        if (!isdigit((unsigned char)*p)) {
                            result = 0;
                            break;
                        }
                    }
                }
                
                value_free(str_val);
                return value_create_bool(result);
            }
            
            // isAlpha(s) - sadece harf mi
            if (strcmp(node->name, "isAlpha") == 0 && node->argument_count >= 1) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                
                int result = 0;
                if (str_val->type == VAL_STRING && strlen(str_val->data.string_val) > 0) {
                    result = 1;
                    for (char* p = str_val->data.string_val; *p; p++) {
                        if (!isalpha((unsigned char)*p)) {
                            result = 0;
                            break;
                        }
                    }
                }
                
                value_free(str_val);
                return value_create_bool(result);
            }
            
            // split(s, delimiter) - string'i böl ve dizi döndür
            if (strcmp(node->name, "split") == 0 && node->argument_count >= 2) {
                Value* str_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* delim_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                if (str_val->type == VAL_STRING && delim_val->type == VAL_STRING) {
                    char* str = strdup(str_val->data.string_val);
                    char* delim = delim_val->data.string_val;
                    
                    // Önce kaç parça olacağını say
                    int count = 1;
                    char* temp = str;
                    while ((temp = strstr(temp, delim)) != NULL) {
                        count++;
                        temp += strlen(delim);
                    }
                    
                    // Dizi oluştur
                    Value* arr = value_create_typed_array(count, VAL_STRING);
                    
                    // String'i böl
                    char* token = str;
                    char* next = NULL;
                    for (int i = 0; i < count; i++) {
                        next = strstr(token, delim);
                        
                        if (next) {
                            *next = '\0';
                            array_push(arr->data.array_val, value_create_string(token));
                            token = next + strlen(delim);
                        } else {
                            array_push(arr->data.array_val, value_create_string(token));
                        }
                    }
                    
                    free(str);
                    value_free(str_val);
                    value_free(delim_val);
                    return arr;
                }
                
                value_free(str_val);
                value_free(delim_val);
                return value_create_array(0);
            }
            
            // join(separator, array) - diziyi birleştir
            if (strcmp(node->name, "join") == 0 && node->argument_count >= 2) {
                Value* sep_val = interpreter_eval_expression(interp, node->arguments[0]);
                Value* arr_val = interpreter_eval_expression(interp, node->arguments[1]);
                
                if (sep_val->type == VAL_STRING && arr_val->type == VAL_ARRAY) {
                    char* sep = sep_val->data.string_val;
                    Array* arr = arr_val->data.array_val;
                    
                    // Toplam uzunluk hesapla
                    int total_len = 0;
                    for (int i = 0; i < arr->length; i++) {
                        if (arr->elements[i]->type == VAL_STRING) {
                            total_len += strlen(arr->elements[i]->data.string_val);
                        }
                        if (i > 0) {
                            total_len += strlen(sep);
                        }
                    }
                    
                    // Sonuç string'i oluştur
                    char* result_str = (char*)malloc(total_len + 1);
                    result_str[0] = '\0';
                    
                    for (int i = 0; i < arr->length; i++) {
                        if (i > 0) {
                            strcat(result_str, sep);
                        }
                        if (arr->elements[i]->type == VAL_STRING) {
                            strcat(result_str, arr->elements[i]->data.string_val);
                        }
                    }
                    
                    value_free(sep_val);
                    value_free(arr_val);
                    
                    Value* result = value_create_string(result_str);
                    free(result_str);
                    return result;
                }
                
                value_free(sep_val);
                value_free(arr_val);
                return value_create_string("");
            }
            
            // trunc(x) - ondalık kısmı atar
            if (strcmp(node->name, "trunc") == 0 && node->argument_count >= 1) {
                double x = GET_NUM_ARG(0);
                return value_create_int((int)trunc(x));
            }
            
            // Kullanıcı tanımlı fonksiyonlar
            Function* func = interpreter_get_function(interp, node->name);
            if (!func) {
                // Type constructor?
                TypeDef* t = interpreter_get_type(interp, node->name);
                if (t) {
                    // Argümanları değerlendir ve object oluştur (named args destekli)
                    Value* obj = value_create_object();
                    int used_fields = 0;
                    int* filled = (int*)calloc(t->field_count, sizeof(int));
                    // Named arg varsa eşle
                    int has_named = 0;
                    for (int i = 0; i < node->argument_count; i++) {
                        if (node->argument_names && node->argument_names[i]) { has_named = 1; break; }
                    }
                    if (has_named) {
                        for (int i = 0; i < node->argument_count; i++) {
                            if (!node->argument_names[i]) {
                                printf("Hata: Type '%s' için tüm argümanlar named olmalı veya hiçbiri olmamalı\n", node->name);
                                exit(1);
                            }
                            const char* fname = node->argument_names[i];
                            int idx = -1;
                            for (int k = 0; k < t->field_count; k++) {
                                if (strcmp(t->field_names[k], fname) == 0) { idx = k; break; }
                            }
                            if (idx < 0) {
                                printf("Hata: Type '%s' alanı bulunamadı: %s\n", node->name, fname);
                                exit(1);
                            }
                            if (filled[idx]) {
                                printf("Hata: Type '%s' alanı iki kez atandı: %s\n", node->name, fname);
                                exit(1);
                            }
                            Value* arg = interpreter_eval_expression(interp, node->arguments[i]);
                            hash_table_set(obj->data.object_val, t->field_names[idx], arg);
                            filled[idx] = 1;
                            used_fields++;
                        }
                        // Eksik alanları default ile doldur
                        for (int k = 0; k < t->field_count; k++) {
                            if (!filled[k]) {
                                if (t->field_defaults && t->field_defaults[k]) {
                                    Value* dv = interpreter_eval_expression(interp, t->field_defaults[k]);
                                    hash_table_set(obj->data.object_val, t->field_names[k], dv);
                                    filled[k] = 1;
                                    used_fields++;
                                } else {
                                    printf("Hata: Type '%s' için eksik alan: %s\n", node->name, t->field_names[k]);
                                    exit(1);
                                }
                            }
                        }
                    } else {
                        if (node->argument_count != t->field_count) {
                            printf("Hata: Type '%s' için beklenen argüman sayısı %d, verilen %d\n", node->name, t->field_count, node->argument_count);
                            exit(1);
                        }
                        for (int i = 0; i < t->field_count; i++) {
                            Value* arg = interpreter_eval_expression(interp, node->arguments[i]);
                            hash_table_set(obj->data.object_val, t->field_names[i], arg);
                        }
                    }
                    free(filled);
                    return obj;
                }
                printf("Hata: Tanımlanmamış fonksiyon '%s'\n", node->name);
                exit(1);
            }
            
            // Parametreleri önce değerlendir (mevcut scope'ta)
            Value** arg_values = (Value**)malloc(sizeof(Value*) * node->argument_count);
            for (int i = 0; i < node->argument_count; i++) {
                arg_values[i] = interpreter_eval_expression(interp, node->arguments[i]);
            }
            
            // Yeni scope oluştur
            SymbolTable* old_scope = interp->current_scope;
            interp->current_scope = symbol_table_create(interp->global_scope);
            
            // Parametreleri yeni scope'a ekle
            for (int i = 0; i < node->argument_count; i++) {
                symbol_table_set(interp->current_scope, 
                               func->node->parameters[i]->name, 
                               arg_values[i]);
                value_free(arg_values[i]);
            }
            free(arg_values);
            
            // Fonksiyon gövdesini çalıştır
            interp->should_return = 0;
            interpreter_execute_statement(interp, func->node->body);
            
            // Return değerini al
            Value* result = interp->return_value ? 
                          value_copy(interp->return_value) : 
                          value_create_void();
            
            // Scope'u geri al
            symbol_table_free(interp->current_scope);
            interp->current_scope = old_scope;
            
            if (interp->return_value) {
                value_free(interp->return_value);
                interp->return_value = NULL;
            }
            interp->should_return = 0;
            
            return result;
        }
            
        default:
            return value_create_void();
    }
}

// ============================================================================
// STATEMENT ÇALIŞTIRMA (Statement Execution)
// ============================================================================

void interpreter_execute_statement(Interpreter* interp, ASTNode* node) {
    if (!node || interp->should_return) return;
    
    switch (node->type) {
        case AST_TYPE_DECL: {
            TypeDef* t = (TypeDef*)malloc(sizeof(TypeDef));
            t->name = strdup(node->name);
            t->field_count = node->field_count;
            t->field_names = (char**)malloc(sizeof(char*) * t->field_count);
            t->field_types = (DataType*)malloc(sizeof(DataType) * t->field_count);
            t->field_defaults = (ASTNode**)malloc(sizeof(ASTNode*) * t->field_count);
            for (int i = 0; i < t->field_count; i++) {
                t->field_names[i] = strdup(node->field_names[i]);
                t->field_types[i] = node->field_types[i];
                t->field_defaults[i] = node->field_defaults ? node->field_defaults[i] : NULL;
            }
            interpreter_register_type(interp, t);
            break;
        }
        case AST_PROGRAM:
        case AST_BLOCK:
            for (int i = 0; i < node->statement_count && !interp->should_return; i++) {
                interpreter_execute_statement(interp, node->statements[i]);
            }
            break;
            
        case AST_VARIABLE_DECL: {
            Value* val = NULL;
            if (node->right) {
                val = interpreter_eval_expression(interp, node->right);
                
                // Eğer tipli array tanımı ise ve değer de array ise, tip kontrolü yap
                if (val->type == VAL_ARRAY) {
                    ValueType required_type = VAL_VOID;
                    
                    switch (node->data_type) {
                        case TYPE_ARRAY_INT: required_type = VAL_INT; break;
                        case TYPE_ARRAY_FLOAT: required_type = VAL_FLOAT; break;
                        case TYPE_ARRAY_STR: required_type = VAL_STRING; break;
                        case TYPE_ARRAY_BOOL: required_type = VAL_BOOL; break;
                        default: break;
                    }
                    
                    // Array'in tipini güncelle
                    if (required_type != VAL_VOID) {
                        val->data.array_val->elem_type = required_type;
                        
                        // Mevcut elemanları kontrol et
                        for (int i = 0; i < val->data.array_val->length; i++) {
                            if (val->data.array_val->elements[i]->type != required_type) {
                                printf("Hata: Array literal'deki tüm elemanlar ");
                                switch (required_type) {
                                    case VAL_INT: printf("int"); break;
                                    case VAL_FLOAT: printf("float"); break;
                                    case VAL_STRING: printf("str"); break;
                                    case VAL_BOOL: printf("bool"); break;
                                    default: break;
                                }
                                printf(" tipinde olmalı!\n");
                                value_free(val);
                                exit(1);
                            }
                        }
                    }
                }
            } else {
                // Varsayılan değer
                switch (node->data_type) {
                    case TYPE_INT:
                        val = value_create_int(0);
                        break;
                    case TYPE_FLOAT:
                        val = value_create_float(0.0f);
                        break;
                    case TYPE_STRING:
                        val = value_create_string("");
                        break;
                    case TYPE_BOOL:
                        val = value_create_bool(0);
                        break;
                    case TYPE_ARRAY:
                        val = value_create_array(4);  // Mixed type array
                        break;
                    case TYPE_ARRAY_INT:
                        val = value_create_typed_array(4, VAL_INT);
                        break;
                    case TYPE_ARRAY_FLOAT:
                        val = value_create_typed_array(4, VAL_FLOAT);
                        break;
                    case TYPE_ARRAY_STR:
                        val = value_create_typed_array(4, VAL_STRING);
                        break;
                    case TYPE_ARRAY_BOOL:
                        val = value_create_typed_array(4, VAL_BOOL);
                        break;
                    case TYPE_ARRAY_JSON:
                        val = value_create_array(4);  // JSON-like mixed array
                        break;
                    default:
                        val = value_create_void();
                        break;
                }
            }
            
            symbol_table_set(interp->current_scope, node->name, val);
            value_free(val);
            break;
        }
            
        case AST_ASSIGNMENT: {
            Value* val = interpreter_eval_expression(interp, node->right);
            
            // Eğer sol taraf array/object erişimi zinciri ise
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                // Zinciri çöz: base name ve segment listesi
                ASTNode* seg = node->left;
                // En sola kadar git
                while (seg->left) seg = seg->left;
                if (!seg->name) {
                    printf("Hata: Geçersiz atama sol tarafı\n");
                    value_free(val);
                    exit(1);
                }
                // Base container
                Value* container = symbol_table_get(interp->current_scope, seg->name);
                if (!container) {
                    printf("Hata: '%s' tanımlı değil\n", seg->name);
                    value_free(val);
                    exit(1);
                }
                // Zinciri baştan tekrar yürü ve parent+key bul
                // seg şu anda ilk düğüm; parent için son düğümden bir önceki noktayı bulmalıyız
                // İlk düğüm tekrar
                ASTNode* walker = node->left;
                Value* current = container;
                Value* parent = NULL;
                while (walker->left) {
                    // İleriye gitmek için önce sol'u işle
                    walker = walker->left;
                }
                // Şimdi en sol seg'e tekrar başlayarak node->left'e kadar ilerleyelim
                // Yeniden başlat
                // Zinciri iteratif gezmek için liste üretelim
                int depth = 0; ASTNode* tmp = node->left; while (tmp) { depth++; tmp = tmp->left; }
                ASTNode** nodes = (ASTNode**)malloc(sizeof(ASTNode*) * depth);
                tmp = node->left;
                for (int i = depth - 1; i >= 0; i--) { nodes[i] = tmp; tmp = tmp->left; }
                // nodes[0] en sol (base), nodes[depth-1] en sağ (target)
                for (int i = 0; i < depth - 1; i++) {
                    ASTNode* n = nodes[i];
                    // index'i değerlendir
                    Value* idx = interpreter_eval_expression(interp, n->index);
                    parent = current;
                    // İleri container'a ilerle
                    if (parent->type == VAL_OBJECT) {
                        if (idx->type != VAL_STRING) { printf("Hata: Object key string olmalı\n"); value_free(idx); value_free(val); exit(1);} 
                        Value* child = hash_table_get(parent->data.object_val, idx->data.string_val);
                        if (!child) { printf("Hata: Object yolunda eksik alan: %s\n", idx->data.string_val); value_free(idx); value_free(val); exit(1);} 
                        current = child;
                    } else if (parent->type == VAL_ARRAY) {
                        if (idx->type != VAL_INT) { printf("Hata: Dizi index integer olmalı\n"); value_free(idx); value_free(val); exit(1);} 
                        int index = idx->data.int_val;
                        // Doğrudan pointer erişimi
                        if (index < 0 || index >= parent->data.array_val->length) { printf("Hata: Dizi sınırları dışında\n"); value_free(idx); value_free(val); exit(1);} 
                        current = parent->data.array_val->elements[index];
                    } else {
                        printf("Hata: Ara segment dizi veya object olmalı\n"); value_free(idx); value_free(val); exit(1);
                    }
                    value_free(idx);
                }
                // Şimdi parent=current_parent, target key son segmentin index'i
                Value* target_parent = current;
                ASTNode* last = nodes[depth - 1];
                Value* last_idx = interpreter_eval_expression(interp, last->index);
                if (target_parent->type == VAL_OBJECT) {
                    if (last_idx->type != VAL_STRING) { printf("Hata: Object key string olmalı\n"); value_free(last_idx); value_free(val); exit(1);} 
                    hash_table_set(target_parent->data.object_val, last_idx->data.string_val, value_copy(val));
                    value_free(last_idx);
                } else if (target_parent->type == VAL_ARRAY) {
                    if (last_idx->type != VAL_INT) { printf("Hata: Dizi index integer olmalı\n"); value_free(last_idx); value_free(val); exit(1);} 
                    array_set(target_parent->data.array_val, last_idx->data.int_val, val);
                    value_free(last_idx);
                } else {
                    printf("Hata: Hedef konteyner dizi veya object olmalı\n"); value_free(last_idx); value_free(val); exit(1);
                }
                free(nodes);
            } else {
                // Normal assignment
                symbol_table_set(interp->current_scope, node->name, val);
            }
            
            value_free(val);
            break;
        }
        
        case AST_COMPOUND_ASSIGN: {
            // x += 5 gibi
            Value* current = symbol_table_get(interp->current_scope, node->name);
            if (!current) {
                printf("Hata: Tanımlanmamış değişken '%s'\n", node->name);
                exit(1);
            }
            
            Value* right_val = interpreter_eval_expression(interp, node->right);
            Value* result = NULL;
            
            if (node->op == TOKEN_PLUS_EQUAL) {
                if (current->type == VAL_BIGINT || right_val->type == VAL_BIGINT) {
                    const char* la = (current->type == VAL_BIGINT) ? current->data.bigint_val : bigint_from_ll_str(current->data.int_val);
                    const char* rb = (right_val->type == VAL_BIGINT) ? right_val->data.bigint_val : bigint_from_ll_str(right_val->data.int_val);
                    char* sum = bigint_add_str(la, rb);
                    if (current->type != VAL_BIGINT) free((char*)la);
                    if (right_val->type != VAL_BIGINT) free((char*)rb);
                    result = value_create_bigint(sum);
                    free(sum);
                } else if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    long long a = current->data.int_val;
                    long long b = right_val->data.int_val;
                    if ((b > 0 && a > (LLONG_MAX - b)) || (b < 0 && a < (LLONG_MIN - b))) {
                        char* sa = bigint_from_ll_str(a);
                        char* sb = bigint_from_ll_str(b);
                        char* sum = bigint_add_str(sa, sb);
                        free(sa); free(sb);
                        result = value_create_bigint(sum);
                        free(sum);
                    } else {
                        result = value_create_int(a + b);
                    }
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : (float)current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : (float)right_val->data.int_val;
                    result = value_create_float(l + r);
                }
            }
            else if (node->op == TOKEN_MINUS_EQUAL) {
                if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    long long a = current->data.int_val;
                    long long b = right_val->data.int_val;
                    if ((-b > 0 && a > (LLONG_MAX + b)) || (-b < 0 && a < (LLONG_MIN + b))) {
                        float l = (float)a; float r = (float)b;
                        result = value_create_float(l - r);
                    } else {
                        result = value_create_int(a - b);
                    }
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : (float)current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : (float)right_val->data.int_val;
                    result = value_create_float(l - r);
                }
            }
            else if (node->op == TOKEN_MULTIPLY_EQUAL) {
                if (current->type == VAL_BIGINT || right_val->type == VAL_BIGINT) {
                    if (current->type == VAL_BIGINT && right_val->type == VAL_BIGINT) {
                        char* prod = bigint_mul_str(current->data.bigint_val, right_val->data.bigint_val);
                        result = value_create_bigint(prod);
                        free(prod);
                    } else {
                        const char* big = (current->type == VAL_BIGINT) ? current->data.bigint_val : right_val->data.bigint_val;
                        long long small = (current->type == VAL_INT) ? current->data.int_val : right_val->data.int_val;
                        char* prod = bigint_mul_small(big, small);
                        result = value_create_bigint(prod);
                        free(prod);
                    }
                } else if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    long long a = current->data.int_val;
                    long long b = right_val->data.int_val;
                    if (a != 0 && ((b > 0 && (a > LLONG_MAX / b || a < LLONG_MIN / b)) ||
                                   (b < 0 && (a == LLONG_MIN || -a > LLONG_MAX / -b)))) {
                        char* sa = bigint_from_ll_str(a);
                        char* sb = bigint_from_ll_str(b);
                        char* prod = bigint_mul_str(sa, sb);
                        free(sa); free(sb);
                        result = value_create_bigint(prod);
                        free(prod);
                    } else {
                        result = value_create_int(a * b);
                    }
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : (float)current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : (float)right_val->data.int_val;
                    result = value_create_float(l * r);
                }
            }
            else if (node->op == TOKEN_DIVIDE_EQUAL) {
                if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    result = value_create_int(current->data.int_val / right_val->data.int_val);
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : right_val->data.int_val;
                    result = value_create_float(l / r);
                }
            }
            
            if (result) {
                symbol_table_set(interp->current_scope, node->name, result);
                value_free(result);
            }
            value_free(right_val);
            break;
        }
        
        case AST_INCREMENT: {
            // x++
            Value* current = symbol_table_get(interp->current_scope, node->name);
            if (!current) {
                printf("Hata: Tanımlanmamış değişken '%s'\n", node->name);
                exit(1);
            }
            
            Value* result = NULL;
            if (current->type == VAL_INT) {
                result = value_create_int(current->data.int_val + 1);
            } else if (current->type == VAL_FLOAT) {
                result = value_create_float(current->data.float_val + 1.0f);
            }
            
            if (result) {
                symbol_table_set(interp->current_scope, node->name, result);
                value_free(result);
            }
            break;
        }
        
        case AST_DECREMENT: {
            // x--
            Value* current = symbol_table_get(interp->current_scope, node->name);
            if (!current) {
                printf("Hata: Tanımlanmamış değişken '%s'\n", node->name);
                exit(1);
            }
            
            Value* result = NULL;
            if (current->type == VAL_INT) {
                result = value_create_int(current->data.int_val - 1);
            } else if (current->type == VAL_FLOAT) {
                result = value_create_float(current->data.float_val - 1.0f);
            }
            
            if (result) {
                symbol_table_set(interp->current_scope, node->name, result);
                value_free(result);
            }
            break;
        }
            
        case AST_FUNCTION_DECL:
            interpreter_register_function(interp, node->name, node);
            break;
            
        case AST_RETURN:
            if (interp->return_value) {
                value_free(interp->return_value);
            }
            interp->return_value = interpreter_eval_expression(interp, node->return_value);
            interp->should_return = 1;
            break;
        
        case AST_BREAK:
            interp->should_break = 1;
            break;
        
        case AST_CONTINUE:
            interp->should_continue = 1;
            break;
            
        case AST_IF: {
            Value* cond = interpreter_eval_expression(interp, node->condition);
            
            if (value_is_truthy(cond)) {
                interpreter_execute_statement(interp, node->then_branch);
            } else if (node->else_branch) {
                interpreter_execute_statement(interp, node->else_branch);
            }
            
            value_free(cond);
            break;
        }
            
        case AST_WHILE: {
            while (1) {
                Value* cond = interpreter_eval_expression(interp, node->condition);
                int should_continue = value_is_truthy(cond);
                value_free(cond);
                
                if (!should_continue || interp->should_return || interp->should_break) {
                    break;
                }
                
                interpreter_execute_statement(interp, node->body);
                
                if (interp->should_continue) {
                    interp->should_continue = 0;
                    continue;
                }
                
                if (interp->should_break) {
                    break;
                }
            }
            interp->should_break = 0;
            break;
        }
            
        case AST_FOR: {
            // Init statement'ı çalıştır (int i = 0)
            if (node->init) {
                interpreter_execute_statement(interp, node->init);
            }
            
            // For döngüsü
            while (1) {
                // Koşulu kontrol et
                if (node->condition) {
                    Value* cond = interpreter_eval_expression(interp, node->condition);
                    int should_continue = value_is_truthy(cond);
                    value_free(cond);
                    
                    if (!should_continue || interp->should_return || interp->should_break) {
                        break;
                    }
                }
                
                // Döngü gövdesini çalıştır
                interpreter_execute_statement(interp, node->body);
                
                if (interp->should_return || interp->should_break) {
                    break;
                }
                
                if (interp->should_continue) {
                    interp->should_continue = 0;
                    // Continue: increment'i çalıştır ve devam et
                    if (node->increment) {
                        interpreter_execute_statement(interp, node->increment);
                    }
                    continue;
                }
                
                // Increment statement'ı çalıştır (i = i + 1)
                if (node->increment) {
                    interpreter_execute_statement(interp, node->increment);
                }
            }
            interp->should_break = 0;
            break;
        }
            
        case AST_FOR_IN: {
            // Iterable'ı değerlendir (range(10) gibi)
            Value* iterable_val = interpreter_eval_expression(interp, node->iterable);
            
            // Eğer range() fonksiyon çağrısıysa, int döner
            if (iterable_val->type == VAL_INT) {
                int count = iterable_val->data.int_val;
                
                // 0'dan count'a kadar döngü
                for (int i = 0; i < count; i++) {
                    if (interp->should_return || interp->should_break) break;
                    
                    // Iterator değişkenini güncelle
                    Value* iter_val = value_create_int(i);
                    symbol_table_set(interp->current_scope, node->name, iter_val);
                    value_free(iter_val);
                    
                    // Döngü gövdesini çalıştır
                    interpreter_execute_statement(interp, node->body);
                    
                    if (interp->should_continue) {
                        interp->should_continue = 0;
                        continue;
                    }
                    
                    if (interp->should_break) {
                        break;
                    }
                }
            }
            
            interp->should_break = 0;
            value_free(iterable_val);
            break;
        }
            
        case AST_FUNCTION_CALL: {
            Value* result = interpreter_eval_expression(interp, node);
            value_free(result);
            break;
        }
            
        default:
            break;
    }
}

// ============================================================================
// ANA İNTERPRETER FONKSİYONU
// ============================================================================

void interpreter_execute(Interpreter* interp, ASTNode* node) {
    interpreter_execute_statement(interp, node);
}

Value* interpreter_eval(Interpreter* interp, ASTNode* node) {
    return interpreter_eval_expression(interp, node);
}


#include "interpreter.h"
#include "../parser/parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// VALUE FONKSİYONLARI
// ============================================================================

Value* value_create_int(int val) {
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
    
    free(val);
}

void value_print(Value* val) {
    if (!val) {
        printf("NULL");
        return;
    }
    
    switch (val->type) {
        case VAL_INT:
            printf("%d", val->data.int_val);
            break;
        case VAL_FLOAT:
            printf("%g", val->data.float_val);
            break;
        case VAL_STRING:
            printf("\"%s\"", val->data.string_val);
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
            
            printf("Hata: Erişilen değer bir dizi veya object değil\n");
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
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_int(left->data.int_val + right->data.int_val);
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
                    result = value_create_int(left->data.int_val - right->data.int_val);
                } else {
                    float l = (left->type == VAL_FLOAT) ? left->data.float_val : left->data.int_val;
                    float r = (right->type == VAL_FLOAT) ? right->data.float_val : right->data.int_val;
                    result = value_create_float(l - r);
                }
            }
            else if (node->op == TOKEN_MULTIPLY) {
                if (left->type == VAL_INT && right->type == VAL_INT) {
                    result = value_create_int(left->data.int_val * right->data.int_val);
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
                    }
                    value_free(prompt);
                }
                
                // Kullanıcıdan input al
                int num;
                if (scanf("%d", &num) == 1) {
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
                        int count = arg->data.int_val;
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
                        result = value_create_int((int)arg->data.float_val);
                    } else if (arg->type == VAL_BOOL) {
                        result = value_create_int(arg->data.bool_val ? 1 : 0);
                    } else if (arg->type == VAL_STRING) {
                        result = value_create_int(atoi(arg->data.string_val));
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
                        snprintf(buffer, sizeof(buffer), "%d", arg->data.int_val);
                    } else if (arg->type == VAL_FLOAT) {
                        snprintf(buffer, sizeof(buffer), "%g", arg->data.float_val);
                    } else if (arg->type == VAL_BOOL) {
                        snprintf(buffer, sizeof(buffer), "%s", arg->data.bool_val ? "true" : "false");
                    } else if (arg->type == VAL_STRING) {
                        value_free(arg);
                        return value_create_string(arg->data.string_val);
                    } else {
                        snprintf(buffer, sizeof(buffer), "");
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
            
            // Kullanıcı tanımlı fonksiyonlar
            Function* func = interpreter_get_function(interp, node->name);
            if (!func) {
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
            
            // Eğer sol taraf array access ise (arr[0] = 5)
            if (node->left && node->left->type == AST_ARRAY_ACCESS) {
                ASTNode* access = node->left;
                Value* arr_val = symbol_table_get(interp->current_scope, access->name);
                
                if (!arr_val || arr_val->type != VAL_ARRAY) {
                    printf("Hata: '%s' bir dizi değil\n", access->name);
                    value_free(val);
                    exit(1);
                }
                
                Value* index_val = interpreter_eval_expression(interp, access->index);
                if (index_val->type != VAL_INT) {
                    printf("Hata: Dizi index integer olmalı\n");
                    value_free(val);
                    value_free(index_val);
                    exit(1);
                }
                
                int index = index_val->data.int_val;
                value_free(index_val);
                
                array_set(arr_val->data.array_val, index, val);
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
                if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    result = value_create_int(current->data.int_val + right_val->data.int_val);
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : right_val->data.int_val;
                    result = value_create_float(l + r);
                }
            }
            else if (node->op == TOKEN_MINUS_EQUAL) {
                if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    result = value_create_int(current->data.int_val - right_val->data.int_val);
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : right_val->data.int_val;
                    result = value_create_float(l - r);
                }
            }
            else if (node->op == TOKEN_MULTIPLY_EQUAL) {
                if (current->type == VAL_INT && right_val->type == VAL_INT) {
                    result = value_create_int(current->data.int_val * right_val->data.int_val);
                } else {
                    float l = (current->type == VAL_FLOAT) ? current->data.float_val : current->data.int_val;
                    float r = (right_val->type == VAL_FLOAT) ? right_val->data.float_val : right_val->data.int_val;
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


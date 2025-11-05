#ifndef INTERPRETER_H
#define INTERPRETER_H

#include "../parser/parser.h"

// Runtime değer türleri
typedef enum {
    VAL_INT,
    VAL_FLOAT,
    VAL_STRING,
    VAL_BOOL,
    VAL_ARRAY,      // Diziler
    VAL_OBJECT,     // JSON Objects (hash table)
    VAL_BIGINT,     // Keyfi büyüklükte tamsayı (decimal string)
    VAL_VOID,
    VAL_FUNCTION
} ValueType;

// Forward declaration
typedef struct Value Value;
typedef struct HashTable HashTable;

// Hash table entry (key-value pair)
typedef struct HashEntry {
    char* key;
    Value* value;
    struct HashEntry* next;  // Chaining for collision handling
} HashEntry;

// Hash table structure
struct HashTable {
    HashEntry** buckets;     // Array of bucket pointers
    int bucket_count;        // Number of buckets
    int size;                // Number of entries
};

// Array structure
typedef struct {
    Value** elements;    // Dizi elemanları
    int length;          // Dizi uzunluğu
    int capacity;        // Kapasite
    ValueType elem_type; // Eleman tipi (VAL_VOID = mixed)
} Array;

// Runtime değer
struct Value {
    ValueType type;
    union {
        long long int_val;
        float float_val;
        char* string_val;
        int bool_val;
        Array* array_val;
        HashTable* object_val;  // Object için hash table
        char* bigint_val;       // BigInt (sadece rakamlar, isteğe bağlı '+' işaretsiz)
    } data;
};

// Değişken (Symbol Table için)
typedef struct {
    char* name;
    Value* value;
} Variable;

// Fonksiyon tanımı
typedef struct {
    char* name;
    ASTNode* node;  // Fonksiyonun AST düğümü
} Function;

// Symbol Table (Değişken depolama)
typedef struct SymbolTable {
    Variable** variables;
    int var_count;
    int var_capacity;
    struct SymbolTable* parent;  // Scope için parent symbol table
} SymbolTable;

// Interpreter context
typedef struct {
    SymbolTable* global_scope;
    SymbolTable* current_scope;
    Function** functions;
    int function_count;
    int function_capacity;
    Value* return_value;  // Fonksiyonlardan dönüş değeri için
    int should_return;    // Return statement kontrolü
    int should_break;     // Break statement kontrolü
    int should_continue;  // Continue statement kontrolü
} Interpreter;

// Value fonksiyonları
Value* value_create_int(long long val);
Value* value_create_float(float val);
Value* value_create_string(char* val);
Value* value_create_bool(int val);
Value* value_create_array(int capacity);
Value* value_create_typed_array(int capacity, ValueType elem_type);
Value* value_create_object();
Value* value_create_void();
Value* value_create_bigint(const char* digits);
Value* value_copy(Value* val);
void value_free(Value* val);
void value_print(Value* val);
int value_is_truthy(Value* val);

// Hash Table fonksiyonları
HashTable* hash_table_create(int bucket_count);
void hash_table_free(HashTable* table);
unsigned int hash_function(const char* key, int bucket_count);
void hash_table_set(HashTable* table, const char* key, Value* value);
Value* hash_table_get(HashTable* table, const char* key);
int hash_table_has(HashTable* table, const char* key);
void hash_table_delete(HashTable* table, const char* key);
void hash_table_print(HashTable* table);

// Array fonksiyonları
void array_push(Array* arr, Value* val);
Value* array_pop(Array* arr);
Value* array_get(Array* arr, int index);
void array_set(Array* arr, int index, Value* val);

// Symbol Table fonksiyonları
SymbolTable* symbol_table_create(SymbolTable* parent);
void symbol_table_free(SymbolTable* table);
void symbol_table_set(SymbolTable* table, char* name, Value* value);
Value* symbol_table_get(SymbolTable* table, char* name);

// Interpreter fonksiyonları
Interpreter* interpreter_create();
void interpreter_free(Interpreter* interp);
void interpreter_execute(Interpreter* interp, ASTNode* node);
Value* interpreter_eval(Interpreter* interp, ASTNode* node);
void interpreter_register_function(Interpreter* interp, char* name, ASTNode* node);
Function* interpreter_get_function(Interpreter* interp, char* name);

#endif


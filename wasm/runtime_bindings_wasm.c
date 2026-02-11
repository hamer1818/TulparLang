/**
 * WebAssembly için özelleştirilmiş runtime bindings
 * Print fonksiyonunu override eder
 * File I/O, network, database gibi özellikler devre dışıdır
 */

#include "../src/vm/vm.h"
#include <stdio.h>
#include <string.h>

// WebAssembly build'inde sadece temel özellikler desteklenir:
// - Print fonksiyonları
// - Matematik fonksiyonları (runtime_bindings.c'den gelecek)
// - String fonksiyonları (runtime_bindings.c'den gelecek)
// - Array ve JSON işlemleri (runtime_bindings.c'den gelecek)
//
// Devre dışı özellikler:
// - File I/O (file_read, file_write, vb.)
// - Network (socket_* fonksiyonları)
// - Database (db_* fonksiyonları)
// - Threading (thread_* fonksiyonları)

// WebAssembly API'den gelen print fonksiyonu
extern int wasm_printf(const char *format, ...);
extern void append_to_output(const char *str);

// Print_vm_value override - WebAssembly için
void print_vm_value(VMValue value) {
    char buffer[1024];
    int pos = 0;
    
    switch (value.type) {
    case VM_VAL_VOID:
        append_to_output("void");
        break;
    case VM_VAL_BOOL:
        append_to_output(AS_BOOL(value) ? "true" : "false");
        break;
    case VM_VAL_INT: {
        snprintf(buffer, sizeof(buffer), "%lld", AS_INT(value));
        append_to_output(buffer);
        break;
    }
    case VM_VAL_FLOAT: {
        snprintf(buffer, sizeof(buffer), "%g", AS_FLOAT(value));
        append_to_output(buffer);
        break;
    }
    case VM_VAL_OBJ:
        if (IS_STRING(value)) {
            append_to_output(AS_STRING(value)->chars);
        } else if (IS_ARRAY(value)) {
            ObjArray *arr = AS_ARRAY(value);
            append_to_output("[");
            for (int i = 0; i < arr->count; i++) {
                if (i > 0)
                    append_to_output(", ");
                print_vm_value(arr->items[i]);
            }
            append_to_output("]");
        } else if (IS_OBJECT(value)) {
            ObjObject *obj = AS_OBJECT(value);
            append_to_output("{");
            for (int i = 0; i < obj->count; i++) {
                if (i > 0)
                    append_to_output(", ");
                append_to_output("\"");
                append_to_output(obj->keys[i]->chars);
                append_to_output("\": ");
                print_vm_value(obj->values[i]);
            }
            append_to_output("}");
        } else {
            append_to_output("<object>");
        }
        break;
    default:
        append_to_output("<unknown>");
        break;
    }
}

// Print fonksiyonları - WebAssembly için override
void print_value(VMValue *value_ptr) {
    if (value_ptr) {
        print_vm_value(*value_ptr);
    }
    append_to_output("\n");
}

void print_value_inline(VMValue *value_ptr) {
    if (value_ptr) {
        print_vm_value(*value_ptr);
    }
}

void print_newline(void) {
    append_to_output("\n");
}

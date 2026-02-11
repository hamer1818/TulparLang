// ============================================
// Thread Management Structures
// ============================================

#include "interpreter.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

typedef struct {
  Interpreter *parent_interp; // Kopyalanacak kaynak (dikkatli olunmalı)
  char *func_name;
  Value *arg;
  // ASTNode *root_node; // Gerekirse
} ThreadArgs;

// Statik AST root (basitlik için)
// Gerçekte bu interpreter içinde olmalı ama şimdilik global tutalım
// ki threadler erişebilsin
static ASTNode *GLOBAL_AST_ROOT = NULL;

// Helper: Interpreter clone (Thread için)
// Sadece fonksiyonları ve o anki global değişkenlerin snapshot'ını alır
Interpreter *interpreter_clone(Interpreter *src) {
  Interpreter *dest = interpreter_create();

  // Fonksiyonları kopyala (referans olarak, çünkü kod değişmez)
  // Dikkat: Function struct'ı pointer, içeriği değişmezse sorun yok
  for (int i = 0; i < src->function_count; i++) {
    Function *f = src->functions[i];
    interpreter_register_function(dest, f->name, f->node);
  }

  // Tipleri kopyala
  for (int i = 0; i < src->type_count; i++) {
    TypeDef *t = src->types[i];
    interpreter_register_type(dest, t);
  }

  // Global değişkenleri kopyala (Deep Copy gerekli!)
  // Çünkü thread içinde değişken değişirse ana thread etkilenmemeli (veya tam
  // tersi race condition) Şimdilik Shallow Copy + Value Copy yapalım
  SymbolTable *src_global = src->global_scope;
  for (int i = 0; i < src_global->var_count; i++) {
    Variable *var = src_global->variables[i];
    if (var && var->name && var->value) {
      // Değeri kopyala
      Value *val_copy = value_copy(var->value);
      symbol_table_set(dest->global_scope, var->name, val_copy);
    }
  }

  return dest;
}

// POSIX Thread entry point
void *thread_entry_point(void *arg) {
  ThreadArgs *args = (ThreadArgs *)arg;

  // Yeni interpreter oluştur ve init et
  Interpreter *thread_interp = interpreter_clone(args->parent_interp);

  // Fonksiyonu bul
  Function *func = interpreter_get_function(thread_interp, args->func_name);
  if (func) {
    ASTNode *func_node = func->node;
    if (func_node && func_node->body) {
      // Argüman varsa ilk parametreye ata
      if (func_node->param_count > 0 && args->arg) {
        symbol_table_set(thread_interp->global_scope,
                         func_node->parameters[0]->name, args->arg);
      }

      interpreter_execute(thread_interp, func_node->body);
    }
  }

  // Temizlik
  interpreter_free(thread_interp);
  free(args->func_name);
  free(args);

  return NULL;
}

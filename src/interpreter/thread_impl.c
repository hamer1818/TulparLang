// ============================================
// Thread Management Structures
// ============================================

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

// Thread Entry Point (Windows)
#ifdef _WIN32
DWORD WINAPI thread_entry_point(LPVOID lpParam) {
  ThreadArgs *args = (ThreadArgs *)lpParam;

  // Yeni interpreter oluştur ve init et
  Interpreter *thread_interp = interpreter_clone(args->parent_interp);

  // Fonksiyonu bul
  Function *func = interpreter_get_function(thread_interp, args->func_name);
  if (func) {
    // Fonksiyon çağrısı için ortamı hazırla
    // ... (argüman geçirme mantığı - burası biraz karmaşık, direkt eval
    // yapamayız) Basitçe: Fonksiyonun AST'sini execute et, ama argümanları
    // scope'a ekle

    // Geçici çözüm: Fonksiyonun parametre adını bulip sembol tablosuna ekle
    // AST FuncDecl noduna erişim lazım.
    ASTNode *func_node = func->node;
    if (func_node && func_node->parameters && func_node->param_count > 0) {
      // İlk parametreye args->arg değerini ata
      char *param_name = func_node->parameters[0];
      // Yeni scope oluştur (fonksiyon scope'u) -> interpreter_execute zaten
      // yapar

      // HACK: Global scope'a argümanı ekleyip fonksiyonu çağıracağız
      // Veya "client_socket" gibi global bir değişken set edeceğiz thread
      // içinde
      symbol_table_set(thread_interp->global_scope, "_thread_arg", args->arg);
    }

    // Fonksiyonu çalıştır
    // Fonksiyon AST'si bir BLOCK'tur (FuncDecl body'si)
    // Direkt body'yi çalıştırabiliriz ama parametre ataması eksik kalır.

    // Doğrusu: Function Call simülasyonu
    // Ama interpreter_eval_function_call private olabilir.
    // Şimdilik: Fonksiyon gövdesini çalıştır, argümanı global "_arg" olarak
    // ver.

    // Düzeltme: interpreter_execute fonksiyonu ASTNode alır.
    // Biz fonksiyonun BODY'sini (Block) çalıştırmalıyız.
    // Parametreleri de global scope'a "inject" edelim.
    if (func_node->body) {
      // Argüman varsa ilk parametreye ata
      if (func_node->param_count > 0) {
        symbol_table_set(thread_interp->global_scope, func_node->parameters[0],
                         value_copy(args->arg));
      }

      interpreter_execute(thread_interp, func_node->body);
    }
  }

  // Temizlik
  interpreter_free(thread_interp);
  // args->arg zaten kopyalandı veya free edildi sorumluluğu kime ait?
  // value_copy yaptık sembol tablosuna, args->arg ana thread'e ait.
  // Ana thread free etmemeli (Value ref counting yok).
  // Argument ana thread'den kopyalanmış (deep copy) gelmeli ThreadArgs'a.
  // Evet, thread_create fonksiyonunda copy yapacağız.
  value_free(args->arg); // ThreadArgs içindeki kopyayı temizle
  free(args->func_name);
  free(args);

  return 0;
}
#else
// POSIX Thread entry point
void *thread_entry_point(void *arg) {
  // Aynı mantık...
  return NULL;
}
#endif

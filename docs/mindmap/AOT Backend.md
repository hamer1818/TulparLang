---
tags: [component, backend, llvm]
---

# AOT Backend (LLVM)

`src/aot/` — **tek** execution path. LLVM 18 IR üretip native binary linkler.

## Dosyalar (AOT_SOURCES, CMakeLists.txt)
- `aot_pipeline.cpp` — giriş: `aot_compile`, `aot_compile_and_run`, `aot_compile_and_run_silent`. Link arama dizinleri: `build_link_search_dirs()` (exe dizini → `<exe>/lib` → `build-<platform>/`).
- `llvm_backend.cpp` — IR üretimi, builtin fonksiyon deklarasyonları + çağrı dispatch'i.
- `llvm_types.cpp`, `llvm_values.cpp` — tip/değer üretimi.

## Bilinmesi gerekenler
- **`null` literal (2026-06-22):** lexer `TOKEN_NULL` → AST `NullLiteral`/`AST_NULL_LITERAL` → codegen `llvm_vm_val_void` (`VM_VAL_VOID`=3). JSON'a `null` serialize eder (`js_serialize` else-dalı + `vmvalue_to_cjson` fallback), `print` "null" basar (`vm_print_value`), falsy. `true`/`false` deseninin birebir aynası (lexer/ast_nodes/parser std::visit + AST_ enum/codegen/typeinfer/ast_visitor). `tests/null_literal.test.tpr` (5/5). → [[Lexer]] [[Parser]] [[Type Inference]]
- **Tanımsız identifier artık çökmüyor (2026-06-22):** `AST_IDENTIFIER` hata yolu `nullptr` yerine güvenli placeholder (`llvm_vm_val_void`) döner; `had_error` zaten set → derleme temiz hata mesajıyla durur. Önceden `nullptr` obje-literal değeri/atama RHS'inde null-deref → segfault'tu. Daha önce "non-deterministic" sanılan segfault'un asıl köküydü (`null`'ın tanımsız identifier'a düşmesi).
- Builtin'ler `LLVMAddFunction` ile deklare edilip isimle (`aot_*`) [[Runtime]]'a linklenir. Deklarasyon tipi runtime imzasıyla uyuşmalı — uyuşmazsa **module verification** uyarısı (örn. `db_last_insert_id`/`db_error` by-pointer deklare edilip by-value çağrılıyordu → düzeltildi, [[SQLite and DB]]).
- **Default-arg padding:** typed user-fn çağrı yolunda eksik arg'lar boxed 0 ile doldurulur. → [[Parser]]
- **Yeni builtin ekleme deseni (2-arg, VMValue→VMValue):** örnek `parse_multipart` (2026-06-22). 4 dokunuş: (1) `llvm_backend.hpp`'ye `func_aot_*` field; (2) `llvm_backend.cpp` decl — `llvm_make_vmvalue_func_type` + `ptr_type` arg'lar, `_ptr` sembol adıyla `LLVMAddFunction`; (3) dispatch — `aot_split_ptr` gibi her arg `alloca→store→bitcast(ptr)`, `llvm_call_vmvalue_func`; (4) `src/lsp/builtins.cpp` LSP girişi. C tarafı `runtime_bindings.cpp`'de `_ptr` wrapper struct arg'ları by-pointer açar (SysV/MinGW payload-drop'tan kaçınmak için). Runtime-visible → **her iki target + repo-kökü `libtulpar_runtime.a`** kopyalanmalı.
- `arena_save`/`arena_restore`/`arena_drop` codegen'de **isimle** tanınır (builtins.cpp/typeinfer'de değil). → [[Memory Model]]
- Mimariye özel LLVM bileşenleri CMake'te seçilir (`x86*` vs `aarch64*`).
- **O3 verify-fallback güvencesi** (`llvm_backend_optimize`): codegen geçerli IR üretir ama LLVM 18 O3/InstCombine `foldOpIntoPhi` ile karşılaştırma fast-path'inin (`op_int`/`op_float`/`op_fallback` merge'i) truthiness `icmp ne <as>,0`'ını phi'ye katlayıp geçersiz `phi i1` (fallback'in ham i64'ü operand) üretebiliyor → "Global module verification failed". Artık O3 öncesi modül `LLVMCloneModule` ile snapshot'lanır; optimize sonrası `LLVMVerifyModule` başarısızsa temiz (optimize edilmemiş) snapshot'a düşülür — ISel'e asla geçersiz IR gitmez. Tetiklenen modül o derleme için O3 kaybeder (nadir; karşılaştırma-yoğun mixed int/float fonksiyonlar). `select i1,1,0` ile zext'i değiştirme denemesi işe yaramadı: InstCombine select'i tekrar `zext`'e kanonikleştiriyor. 2026-06-22.
- Ctrl+C: `AOT_RAN_NONZERO` enum'u, program'ın non-zero exit'ini AOT link hatasından ayırır (`aot_pipeline.hpp/.cpp`, `src/main.cpp`); SIGINT temiz çıkış.

## İlgili
[[Runtime]] · [[Memory Model]] · [[Type Inference]] · [[Build System]]

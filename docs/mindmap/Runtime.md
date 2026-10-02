---
tags: [component, runtime]
---

# Runtime (src/vm/ — VM değil!)

İsim tarihsel; bu bir execution engine **değil**, AOT binary'lerin link'lediği **paylaşılan runtime**. `tulpar_runtime` static lib bu setten derlenir (`-DTULPAR_RUNTIME_ONLY`).

## Parçalar
- `runtime_bindings.cpp` — **her `aot_*` builtin** (print, sockets, db, threads, async, time, json, regex...). En çok dokunulan dosya.
- `vm.cpp` — arena allocator + obje/string allocator'ları (`allocate_object`, `vm_alloc_string`, `vm_allocate_array/object`, `vm_array_push`). → [[Memory Model]]
- `vm.hpp` — runtime değer tipleri: `VMValue`, `Obj*` (historik isim; "runtime value representation" diye oku).
- `bytecode.cpp/.hpp` — yalnız `ObjFunction` içine gömülü `Chunk` tipi için kaldı.

## Kaldırıldı (geri getirme!)
- `src/vm/compiler.cpp` (AST→bytecode), `vm_run` (interpreter loop), `run_repl`. → [[Decisions]]

## Değerin metni — TEK kural (2026-10-02)
`print`, `toString`, `"..." + x`, `t"{x}"`, `sb_append` aynı biçimleyiciden
geçer (`aot_value_repr` / `repr_value`; struct için `repr_struct_slots` →
`aot_struct_print` / `aot_struct_format`). Sayı/bool her yerde tekil
`print(<sayı>)` metni: float `aot_format_float` (en kısa geri dönen; `nan`,
`inf`, `-inf` platformdan bağımsız), bool `true/false` — struct alanı, tuple
elemanı ve kap elemanı dahil; f32 alan okunduğu double ile. `toJson` ayrı
sözleşme (NaN/sonsuz → `null`). gdb/DAP printer'ı (`tools/gdb/tulpar_printers.py`)
aynı kuralı Python'da tekrarlar. Kapı: `tests/deger_metni.{sh,test.tpr}`.
→ [[Tuzaklar]] 7j

## Per-thread durum
Arena, checkpoint stack, region — hepsi `thread_local` (thread_create / pool worker'lar için). → [[Memory Model]]

## İlgili
[[Memory Model]] · [[AOT Backend]] · [[Async Runtime]] · [[SQLite and DB]]

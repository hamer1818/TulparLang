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

## Per-thread durum
Arena, checkpoint stack, region — hepsi `thread_local` (thread_create / pool worker'lar için). → [[Memory Model]]

## İlgili
[[Memory Model]] · [[AOT Backend]] · [[Async Runtime]] · [[SQLite and DB]]

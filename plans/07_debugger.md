# Plan 07 — Debugger MVP (DWARF + DAP)

**Durum:** PROPOSED
**Tahmin:** 5-7 PR
**Risk:** Yüksek — yeni codegen path (DWARF emit) + yeni TCP protokol
sunucusu (DAP); LSP'den daha karmaşık
**Mottoya katkı:** Python kolay (breakpoint, step, watch — modern dil
beklentisi)

## Hedef

VS Code'da kullanıcı bir `.tpr` dosyasında satır numarasının soluna
tıklasın, mavi nokta görsün. `F5` ile çalıştırsın. Program o satırda
dursun. Variables paneli `int n = 5`, `str name = "Hamza"`'yı
göstersin. F10 step over, F11 step into, F5 continue çalışsın.

Şu an:
- AOT binary'ler debug info içermiyor (`-g` yok).
- `gdb ./tulpar_aot` Tulpar source satırına değil, LLVM-generated
  basic block adreslerine bakıyor — useless.
- VS Code eklentisinde debugger UI yok; sadece dosya açma + LSP.

## Mevcut durum (kaynak)

- `src/aot/aot_pipeline.cpp`: clang link adımı `-g` flag'i geçmiyor.
- `src/aot/llvm_backend.cpp`: `LLVMDIBuilderRef` ve
  `LLVMDIBuilderCreate*` çağrıları yok.
- `vscode-tulpar/` package.json'da `debuggers` section yok.

## İki büyük parça

### Parça A — DWARF / CodeView emit

LLVM debug info builder API'sı:

```cpp
LLVMDIBuilderRef dib = LLVMCreateDIBuilder(module);
LLVMMetadataRef file = LLVMDIBuilderCreateFile(dib, "app.tpr", "/path");
LLVMMetadataRef cu = LLVMDIBuilderCreateCompileUnit(
    dib, LLVMDWARFSourceLanguageC, file, "tulpar", 0, ...);
LLVMMetadataRef func_md = LLVMDIBuilderCreateFunction(
    dib, scope, "main", "main", file, line, ...);
LLVMSetSubprogram(llvm_fn, func_md);
// Her statement için:
LLVMSetCurrentDebugLocation2(builder, LLVMDIBuilderCreateDebugLocation(
    ctx, line, col, scope, NULL));
// Build sonunda:
LLVMDIBuilderFinalize(dib);
```

Sonra clang link'inde `-g` flag → final binary'de `.debug_info`
section. gdb/lldb bunu okur.

Windows'ta DWARF mı CodeView mı? clang-cl `-gcodeview` üretir,
gcc/MSYS2 clang DWARF üretir. Önerim: ne çıkıyorsa onu emit et
(DWARF Linux/macOS, CodeView Windows MSVC, DWARF Windows MinGW).

### Parça B — DAP (Debug Adapter Protocol) sunucusu

VS Code'un debugger client'ı **DAP** ile konuşur (LSP'nin debug
versiyonu). JSON-RPC stdio veya TCP üzerinden.

İki yaklaşım:

**B1. Native gdb wrapper.** Yeni bir `tulpar debug <file>` komutu:
- `tulpar build --debug <file>` ile DWARF'lı binary üret.
- `gdb` başlat (`gdb --interpreter=mi3 ./binary`).
- gdb-mi (machine interface) ↔ DAP arası translation katmanı.
- Avantaj: gerçek gdb breakpoint/step/eval mantığı bedava.
- Dezavantaj: gdb'nin kullanıcı sistemine kurulu olması lazım.

**B2. Kendi minimal debugger.** Native ptrace/Win32-DebugAPI'ye
sarmal:
- Avantaj: gdb dependency yok, controlled UX.
- Dezavantaj: 10× iş — instruction stepping, register dump,
  symbolication kendi yaz.

**RFC önerisi: B1.** v0.9-RC için gdb wrapper yeterli. v1.0+ B2
düşünülür.

## Adımlar

### PR 1 — `tulpar build --debug` flag

- `aot_pipeline.cpp`: `--debug` veya `-g` arg accept.
- clang link cmd'sine `-g` ekle.
- Test: `tulpar build --debug examples/01_hello_world.tpr` → çıktıda
  `objdump -h` `.debug_info` görünür.

### PR 2 — LLVM DIBuilder iskelet

- `llvm_backend.cpp`: backend struct'ında `LLVMDIBuilderRef` ve
  `current_subprogram` slot'ları.
- `--debug` modunda compile unit + file metadata oluştur.
- Her function declaration'da `LLVMSetSubprogram` ile bağla.
- Her statement codegen'inde `LLVMSetCurrentDebugLocation2` çağır.
- Test: `gdb` ile `break main:3` çalışıyor.

### PR 3 — Lokal değişken metadata

- Her `var int x = ...` için `LLVMDIBuilderCreateAutoVariable` ve
  `LLVMDIBuilderInsertDeclareAtEnd`.
- Test: `gdb` `print n` lokali okur.

### PR 4 — DAP server (Tulpar dilinde, dogfood)

- `lib/dap.tpr` veya `src/cli/debug_cmd.cpp` — TCP/stdio JSON-RPC.
- gdb-mi command'larına proxy: `initialize`, `launch`,
  `setBreakpoints`, `next`, `stepIn`, `continue`,
  `stackTrace`, `variables`, `evaluate`.
- Test: VS Code "Run and Debug" panel'inden launch et.

### PR 5 — VS Code eklentisi debugger entegrasyonu

- `vscode-tulpar/package.json` `contributes.debuggers`.
- `.vscode/launch.json` snippet: `{"type": "tulpar", "request":
  "launch", "program": "${file}"}`.
- F5 ile çalışır.

### PR 6 — Breakpoint UX cilası

- Conditional breakpoints (`condition` field DAP'da var).
- Logpoints (`logMessage`).
- Watch expressions (`evaluate` repeated).

### PR 7 — Inline values + hover

- Eklentide `DebugAdapterInlineValuesProvider` — değişken değerleri
  satır kenarına yazılır (PyDev / IntelliJ tarzı).

## Açık sorular

1. gdb mı kendi debugger mı? RFC B1 (gdb wrapper).
2. Windows CodeView vs DWARF — toolchain ne diyorsa.
3. JIT debugger? VM path'i için ayrı bir debugger gerekir; MVP AOT
   yalnız.
4. Source map'i AOT IR'da nasıl korunur? LLVM `!dbg` metadata zaten
   bu işi yapıyor.
5. Async stack (Plan 06) debugger entegrasyonu — sonraki faz, async
   coroutine frame'leri tarayan custom unwinder.

## Risk değerlendirmesi

- **Yüksek:** Inline'ed function'lar gdb'de "function inlined" satırı
  gösterir, kullanıcı kafa karışır. AOT optimizer'a `--debug` modunda
  `-O0` zorla.
- **Orta:** Windows DAP path debugger toolchain'ine bağımlı —
  pacman'dan `mingw-w64-x86_64-gdb` yoksa fail. Doc + install
  prompt gerek.
- **Orta:** `--debug` build'i `-g` ile compile-time `-O0` üretmeli;
  prod binary ile karışmamalı. CMake target separation.
- **Düşük:** DAP protocol changes — stable spec, low churn.

## İlgili işler

- Plan 06 (async/await) — async stack debugger entegrasyonu için.
- LSP server (PR'lar tamam) — DAP server aynı dogfood pattern.
- `vscode-tulpar` eklentisi — debugger contributes block buraya iner.

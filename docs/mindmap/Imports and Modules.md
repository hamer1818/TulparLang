---
tags: [component, frontend]
---

# Imports & Modules

`import "name"` çözümleme sırası (`src/aot/llvm_backend.cpp` import handler): gömülü stdlib adı → **yüklü bir yerel eklentinin modülü** (`tulpar-ext.json` `modules`, 2026-10-02 — [[Eklentiler]]) → `name` → `name.tpr` → `tulpar_modules/<name>/<name>.tpr` → `tulpar_modules/<name>.tpr`. Son ikisi `tulpar pkg install`'un kullandığı yer. Typeinfer (`load_import_source`) ve ayrıştırıcının ön taraması (`parser_import_loader`) aynı sırayı izler. Hiçbir yerden çözülmeyen yol-olmayan ad (`import "engine"`) hatanın altında `--ext` ipucunu basar.

## Alias'lı import
`import "name" as alias;` → modüldeki her top-level `func` `<alias>__<name>`'e yeniden adlandırılır, intra-module çağrılar da. İki kütüphane aynı `route`/`helper`'ı export etse çakışmaz. Builtin'ler ve importer'ın kendi fonksiyonları dokunulmaz. Rewrite: `src/parser/import_alias.cpp` (AOT `AST_IMPORT` codegen'den çağrılır).

## Çağrı biçimleri
`m.func(args)` (Python tarzı) ≡ `m__func(args)` (mangled). `parse_postfix` `<id>.<id>(args)` → tek `FunctionCall`. `obj.field` okuma/yazma ayrı (ArrayAccess desugar). **Gerçek obje method'u (`obj.method(x)`) desteklenmiyor.** → [[Parser]]

## Tanı konumu (modülün hatası modülün dosyasıyla)
İçe aktarılan modülün **her** tanısı modülün kendi yolu ve satırıyla basılır:
ayrıştırma (K056, 2026-09-27), kodgen ve sözcükleyici (oyun geri bildirimi #4,
2026-10-08). Mekanizma: `ImportedModule::source` / `diag_file` saklanır;
`AST_IMPORT` kodgeni modülü işlerken `backend->source_text/source_filename`
değerlerini modülünkine çevirir (RAII, iç içe import kendi bağlamını kurar);
`import_register_types` BİLEREK dışarıda — struct yerleşim çatışması içe
aktaranın import satırında gösteriliyor. Sözcükleyici dosya adını ve
sessizliği `diagnostics.hpp`'den okur (`diag_file()` / `diag_quiet()`;
ayrıştırıcının `parser_set_diagnostic_context` / `parser_set_quiet`'i oraya da
yazar). LSP: `Diagnostic.file` kök belge değilse tanı kök belgede
`anchor_line`'a (ona götüren üst düzey import) taşınır, `relatedInformation`
modülün konumunu verir. Kapı: `tests/modul_tani_konumu.sh`, `tests/lsp_audit.py`.
Sınır: debug bilgisi (DWARF) modül fonksiyonlarını hâlâ kök dosyanın
`DIFile`'ına bağlıyor; typeinfer modül GÖVDELERİNİ denetlemiyor (yalnız imza).
→ [[Tuzaklar#7t. Tanı bağlamını tek katmanda düzeltmek — öbür katman kök dosyanın adını basmaya devam eder]]

## İlgili
[[Parser]] · [[Standard Library]] · [[Tooling]] · [[Eklentiler]]

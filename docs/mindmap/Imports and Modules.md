---
tags: [component, frontend]
---

# Imports & Modules

`import "name"` çözümleme sırası: gömülü stdlib adı → **yüklü bir yerel eklentinin modülü** (`tulpar-ext.json` `modules`, 2026-10-02 — [[Eklentiler]]) → DİSK adayları (`src/common/import_resolve.hpp`, TEK kaynak — kodgen `import_load_module`, önbellek `resolve_import`, typeinfer `load_import_source`, ayrıştırıcının ön taraması `parser_import_loader` hepsi onu çağırır):
1. `<içe aktaranın dizini>/name.tpr`, 2. `<içe aktaranın dizini>/name` — **2026-10-08'den beri ana dosya için de** (ana dosyanın dizini; oyun geri bildirimi #6),
3. `name`, 4. `name.tpr` — çalışma dizini (eski kural, geri dönüş),
5. `tulpar_modules/<name>/<name>.tpr`, 6. `tulpar_modules/<name>.tpr` (`tulpar pkg install`).

Yalnız düzenli dosyalar aday (dizin değil). 1-2 ile 3-4 FARKLI dosyalara çıkarsa 1-2 kazanır ve uyarı basılır (`resolves in two places`); 2026-10-08 öncesi derleyici 3-4'ü seçerdi. Tekilleştirme dosya kimliğiyle (`ImportState::islenen`, mutlak yol): aynı dosyanın iki yazımı bir kez yüklenir, aynı yazımın iki farklı dosyası ikisi de; ana dosyanın kimliği baştan içeride (geri içe aktarma no-op). Önbellek anahtarı aynı çözümü + gölgelenen adayın varlığını taşır. Hiçbir yerden çözülmeyen yol-olmayan ad (`import "engine"`) hatanın altında `--ext` ipucunu basar. Bilinen boşluk: kullanılmayan bir import'un bulunamaması derlemeyi durdurmuyor (`Error:` basılıp çıkış 0). `tulpar.toml` ve `tulpar_modules/` hâlâ çalışma dizininde aranır. Kapılar: `tests/import_yolu.sh`, `tests/onbellek.sh`.

## Alias'lı import
`import "name" as alias;` → modüldeki her top-level `func` `<alias>__<name>`'e yeniden adlandırılır, intra-module çağrılar da. İki kütüphane aynı `route`/`helper`'ı export etse çakışmaz. Builtin'ler ve importer'ın kendi fonksiyonları dokunulmaz. Rewrite: `src/parser/import_alias.cpp` (AOT `AST_IMPORT` codegen'den çağrılır).

## Çağrı biçimleri
`m.func(args)` (Python tarzı) ≡ `m__func(args)` (mangled). `parse_postfix` `<id>.<id>(args)` → tek `FunctionCall`. `obj.field` okuma/yazma ayrı (ArrayAccess desugar). **Gerçek obje method'u (`obj.method(x)`) desteklenmiyor.** → [[Parser]]

## Görünürlük kuralı (2026-10-08, oyun geri bildirimi #5)
Düz `import` her şeyi tek global ad alanına koyar; **bir modül kendi
import'larını ve kendisini (doğrudan ya da dolaylı) içe aktaran her dosyanın
üst düzey adlarını — global VE fonksiyon — metin sırasından bağımsız görür.**
Daha sonra içe aktarılan KARDEŞ modülü görmez (ne fonksiyonunu ne globalini);
"bulunamadı" hatası kardeşin adını ve çözümü verir: kullandığını kendin içe
aktar (ikinci `import` tekilleştirilir, yani zararsız). Mekanizma: ana
dosyanın Pass 0.15'i — bir modülün andığı (çağrı/tanımlayıcı), hiçbir
modülün tanımlamadığı ve hiçbir modülde değişken adı olmayan ana dosya
fonksiyonlarının imzaları import'lardan (Pass 0.2) ÖNCE; geri kalanı her
zamanki gibi Pass 1a'da. Modüller zaten kendi imzalarını iç import'larından
önce bildiriyordu. Seçicilik bilerek: eskiden derlenen programların IR'i
(fonksiyon sırası dahil) aynı kalıyor — bütün imzaları öne almak sırayı
değiştirdi ve bölümlü üretimin ikili kimlik kapısı LLVM 18'de kırmızıya
döndü ([[Tuzaklar#7o. Aynı IR, FARKLI makine kodu — kod üretimini bölmek "zararsız" değil]]).

Neden kardeş görünür yapılmadı: fonksiyonların başlatılması yok, öne almak
güvenli; ama kardeşin GLOBAL'ini öne almak, önceki kardeşin üst düzey kodunun
henüz başlatılmamış bir değer (0) okumasına izin verirdi — derleme hatası
yerine SESSİZ yanlış değer. (Aynı sessizlik ice aktaranın globali için bugün
de var: modülün üst düzey kodu, ana dosyanın üst düzey atamalarından ÖNCE
koşar.) Fonksiyonlar ve globaller aynı kurala uysun diye kardeşte ikisi de
görünmez. Kapı: `tests/modul_ice_aktaran.sh`. Karar: [[Decisions]].

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

# Roadmap Plans

Bu dizin TulparLang'ın bir sonraki etabı için hazırlanmış uygulama
planlarını içerir. [STATUS.md](../STATUS.md) mevcut durumu (tamamlanan
PR'lar + açık eksikler) gösterir; bu klasör ileri bakar (çok-PR'lık
uygulama planları).

Her plan dosyası **bağımsız ve self-contained** — tek başına okunup tek
PR'da uygulanabilir. Plan numaraları öncelik değil, başlangıçtaki
oluşturulma sırasıdır.

| # | Başlık | Durum | Tahmin | Risk |
|---|--------|-------|--------|------|
| 01 | [Real object method calls (`p.greet()`)](01_method_calls.md) | ✅ DONE (PR #44) | 2-3 PR | Düşük-Orta |
| 02 | [Package registry tamamlaması](02_pkg_registry.md) | ✅ DONE (PR'lar #47–#229) | 2-4 PR | Düşük |
| 03 | [typeinfer strict mode](03_typeinfer_strict.md) | ✅ DONE (PR'lar #32 + #43) | 1-2 PR | Düşük |
| 04 | [Native record/struct types (unboxed)](04_native_structs.md) | ✅ DONE (PR'lar #48–#50) | 4-6 PR | Yüksek |
| 05 | [Generics (parametric polymorphism)](05_generics.md) | 🔲 PROPOSED | 5-8 PR | Yüksek |
| 06 | [`async` / `await` keywords](06_async_await.md) | 🔲 PROPOSED | 4-6 PR | Yüksek |
| 07 | [Debugger MVP (DWARF + DAP)](07_debugger.md) | ✅ DONE (PR'lar #160–#223) | 5-7 PR | Yüksek |

Tipik bir uygulama akışı:

1. Plan dosyasını oku, "Açık sorular" bölümünü kararlaştır
2. "Adımlar" başlığı altındaki kalemleri sırasıyla uygula — her madde
   bir PR (build.sh test + ilgili `tests/*.test.tpr` yeşilse merge)
3. PR merge sonrası plan dosyasının üstüne `**Status: COMPLETED
   (yyyy-mm-dd, PR #N)**` ibaresi düş
4. Plan tamamlandıktan sonra dosyayı silmek yerine arşivde tut —
   gelecekteki refactor'lar için "neden bu şekilde yaptık" referansı

Yeni bir plan eklerken bu README'deki tabloyu da güncelle.

# Roadmap Plans

Bu dizin TulparLang'ın bir sonraki etabı için hazırlanmış uygulama
planlarını içerir. EKSIKLER.md geçmişe bakar (resolved bug'ları
arşivler); bu klasör ileri bakar (henüz açılmamış iş kalemleri).

Her plan dosyası **bağımsız ve self-contained** — tek başına okunup tek
PR'da uygulanabilir. Plan numaraları öncelik değil, başlangıçtaki
oluşturulma sırasıdır.

| # | Başlık | Tahmin | Risk | Mottoya katkı |
|---|--------|--------|------|---------------|
| 01 | [Real object method calls (`p.greet()`)](01_method_calls.md) | 2-3 PR | Düşük-Orta | Python kolay |
| 02 | [Package registry tamamlaması](02_pkg_registry.md) | 2-4 PR | Düşük | Ekosistem |
| 03 | [typeinfer strict mode](03_typeinfer_strict.md) | 1-2 PR | Düşük | Python kolay (erken hata) |
| 04 | [Native record/struct types (unboxed)](04_native_structs.md) | 4-6 PR | Yüksek | C kadar hızlı |

Tipik bir uygulama akışı:

1. Plan dosyasını oku, "Açık sorular" bölümünü kararlaştır
2. "Adımlar" başlığı altındaki kalemleri sırasıyla uygula — her madde
   bir PR (build.sh test + ilgili `tests/*.test.tpr` yeşilse merge)
3. PR merge sonrası plan dosyasının üstüne `**Status: COMPLETED
   (yyyy-mm-dd, PR #N)**` ibaresi düş
4. Plan tamamlandıktan sonra dosyayı silmek yerine arşivde tut —
   gelecekteki refactor'lar için "neden bu şekilde yaptık" referansı

Yeni bir plan eklerken bu README'deki tabloyu da güncelle.

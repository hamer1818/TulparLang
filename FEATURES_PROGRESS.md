# TulparLang - Özellik İlerlemesi ve Yol Haritası Durumu

Bu doküman, planlanan başlıklar için mevcut durumu özetler.

## ✅ Tamamlananlar

- Type ekosistemi
  - Type metotları: `func Person.fullName() { ... }`
  - Nested type constructor zinciri: `Order(customer: Person(...))`
- JSON Serde
  - `toJson(value)`
  - `toJson(value, pretty)` - Pretty-print seçeneği (opsiyonel 2. argüman)
  - `fromJson("TypeName", jsonStr)` (eksik alanlar default ile tamamlanır)
  - `fromJson("TypeName", jsonStr, strict)` - Strict/lenient kipleri (opsiyonel 3. argüman)
- BigInt
  - Bölme (div), Mod (mod), Hızlı üs alma (pow, int taban/üs için BigInt)

## 🟡 Kısmen Tamamlananlar

- Hata deneyimi (line/column bilgisi)
  - Eklendi: dizi/object/string erişimi, sıfıra bölme (int/float/BigInt), sıfıra mod (int/BigInt), tanımsız değişken, fromJson eksik alan, push/pop/length, `toInt/Float/String` ve `pow/mod` tip kontrolleri, `toBool` için ayrıntılı tür uyarısı (truthiness üzerinde uyarı)
  - Bekleyen: diğer kenar vakaları (tip uyumsuzlukları, aritmetik taşma vb.)

## ⏳ Bekleyenler

- Performans/Kararlılık
  - BigInt çarpma/bölme performans iyileştirmeleri
  - Parser/Interpreter mikro-optimizasyonlar
- Geliştirici deneyimi
  - REPL (interaktif mod)
  - Modül/Import sistemi
  - Test runner çıktısında hatalı satıra atlama/özet

## 📌 Notlar

- Type metotları çağrısında `self` alıcı olarak kullanılabilir: `func Person.fullName() { return self["name"]; }`
- BigInt işlemlerinde taban/üs tamsayı ise `pow` BigInt döndürür; aksi halde `float` kullanılır.
- `fromJson("Type", js)` tipi işaretler ve default alanları uygular; eksik zorunlu alanlar hata üretir.

## 📅 Önerilen Sıra (Sonraki Adımlar)

1. Hatalara line/column bilgisinin yaygınlaştırılması (push/pop, length, dönüştürmeler, aritmetik)
2. BigInt performans iyileştirmeleri (çarpma/bölme)
3. REPL ve Modül/Import
4. Test runner iyileştirmeleri (hatalı satıra atlama, özet)



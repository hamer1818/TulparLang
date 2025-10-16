# TulparLang Changelog

## [1.2.2] - 2025-10-09 - arrayJson Support 🎉

### ✨ Yeni Özellikler

#### arrayJson - JSON-Like Diziler
- **Yeni Tip**: `arrayJson` keyword eklendi
- JSON formatına benzer veri yapıları için özel dizi tipi
- Nested (iç içe) array desteği
- Karışık tip desteği (int, float, str, bool bir arada)

**Örnek Kullanımlar:**
```tulpar
// Kullanıcı verisi
arrayJson kullanici = ["Ali", 25, true, "Mühendis"];

// API Response
arrayJson response = [200, "Success", true];

// Nested arrays
arrayJson users = [["user1", 25], ["user2", 30]];

// Config data
arrayJson config = ["TulparLang", "1.2.2", true, 8080];
```

### 📝 Dokümantasyon
- `examples/13_json_arrays.tpr` - 10 farklı kullanım örneği
- `examples/README.md` güncellendi

### 🔧 Teknik Değişiklikler
- `TOKEN_ARRAY_JSON` lexer'a eklendi
- `TYPE_ARRAY_JSON` parser'a eklendi
- Interpreter'da mixed array desteği

### 📊 İstatistikler
- **Yeni Dosya**: 3 (ARRAYJSON_KULLANIM.md, 13_json_arrays.tpr, CHANGELOG.md)
- **Güncellenen Dosya**: 6 (lexer.h, lexer.c, parser.h, parser.c, interpreter.c, README.md)
- **Toplam Array Tipi**: 6 (array, arrayInt, arrayFloat, arrayStr, arrayBool, arrayJson)

---

## [1.2.1] - 2025-10-09 - Type-Safe Arrays

### ✨ Yeni Özellikler

#### Tip Güvenlikli Diziler
- `arrayInt` - Sadece integer dizileri
- `arrayFloat` - Sadece float dizileri
- `arrayStr` - Sadece string dizileri
- `arrayBool` - Sadece boolean dizileri

#### Tip Kontrolü
- Push zamanı tip kontrolü
- Set zamanı tip kontrolü
- Array literal tanımlama kontrolü
- Anlaşılır hata mesajları

### 📝 Dokümantasyon
- `TIP_GUVENLIKLI_DIZILER.md` eklendi
- `FAZ2_GUNCEL_RAPOR.md` eklendi

---

## [1.2.0] - 2025-10-09 - Arrays (Faz 2)

### ✨ Yeni Özellikler

#### Diziler (Arrays)
- `array` keyword - PHP tarzı array syntax
- Array literal syntax: `[1, 2, 3]`
- Array element access: `arr[0]`
- Array element assignment: `arr[0] = 5`
- Karışık tip desteği: `[1, "Ali", 3.14, true]`

#### Built-in Fonksiyonlar
- `length(arr)` - Dizi ve string uzunluğu
- `push(arr, val)` - Diziye eleman ekle
- `pop(arr)` - Diziden son elemanı çıkar

### 📝 Dokümantasyon
- `FAZ2_RAPOR.md` eklendi
- Örnek dosyalar eklendi

---

## [1.1.0] - 2025-10-09 - Core Features (Faz 1)

### ✨ Yeni Özellikler

#### Mantıksal Operatörler
- `&&` - AND operatörü
- `||` - OR operatörü
- `!` - NOT operatörü

#### Increment / Decrement
- `++` - Increment
- `--` - Decrement

#### Compound Assignment
- `+=` - Add and assign
- `-=` - Subtract and assign
- `*=` - Multiply and assign
- `/=` - Divide and assign

#### Kontrol Akışı
- `break` - Döngüden çık
- `continue` - Döngü iterasyonunu atla

#### Tip Dönüşümleri
- `toInt(val)` - Int'e çevir
- `toFloat(val)` - Float'a çevir
- `toString(val)` - String'e çevir
- `toBool(val)` - Bool'a çevir

### 📝 Dokümantasyon
- `FAZ1_RAPOR.md` eklendi
- Test dosyaları eklendi

---

## [1.0.0] - 2025-10-08 - Initial Release

### ✨ Temel Özellikler

#### Veri Tipleri
- `int` - Tam sayılar
- `float` - Ondalıklı sayılar
- `str` - String
- `bool` - Boolean

#### Operatörler
- Aritmetik: `+`, `-`, `*`, `/`
- Karşılaştırma: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Unary: `-` (negatif)

#### Kontrol Akışı
- `if` / `else`
- `while` döngüsü
- `for` döngüsü (C-style)
- `for..in` döngüsü (JavaScript-style)

#### Fonksiyonlar
- Fonksiyon tanımlama: `func name(params) { ... }`
- Return statement
- Rekursif fonksiyonlar
- Scope yönetimi

#### Built-in Fonksiyonlar
- `print()` - Çoklu argüman desteği
- `input()` - String girişi
- `inputInt()` - Integer girişi
- `inputFloat()` - Float girişi
- `range(n)` - For..in için range

#### Proje Yapısı
- Modüler yapı: lexer, parser, interpreter klasörleri
- Makefile ile build sistemi
- WSL desteği

### 📝 Dokümantasyon
- `README.md` - Ana dokümantasyon
- `QUICKSTART.md` - Hızlı başlangıç
- `KULLANIM.md` - Detaylı kullanım
- `GELECEK_OZELLIKLER.md` - Roadmap
- Örnek dosyalar

---

## Versiyon Numaralandırma

TulparLang [Semantic Versioning](https://semver.org/) kullanır:
- **MAJOR**: Geriye uyumsuz değişiklikler
- **MINOR**: Yeni özellikler (geriye uyumlu)
- **PATCH**: Bug fix'ler ve küçük iyileştirmeler

---

**Son Güncelleme**: 9 Ekim 2025  
**Aktif Versiyon**: 1.2.2


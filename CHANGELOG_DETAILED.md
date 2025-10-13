# CHANGELOG - OLang

Tüm önemli değişiklikler bu dosyada dokümante edilmiştir.

## [1.5.0] - 2025-10-13

### ✨ Eklenenler
- **String İşleme Kütüphanesi** - 16 yerleşik string fonksiyonu
  - **Dönüşüm**: `upper()`, `lower()`, `capitalize()`, `reverse()`
  - **Temizleme**: `trim()`, `replace(old, new)`
  - **Arama**: `contains(sub)`, `startsWith(prefix)`, `endsWith(suffix)`, `indexOf(sub)`, `count(sub)`
  - **Alt String**: `substring(start, end)`, `repeat(n)`
  - **Bölme/Birleştirme**: `split(delimiter)` → array, `join(separator, array)` → string
  - **Kontrol**: `isEmpty()`, `isDigit()`, `isAlpha()`
  
- **Özellikler**:
  - Tüm string fonksiyonları UTF-8 uyumlu
  - `split()` string'i diziye ayırır
  - `join()` diziyi string'e birleştirir
  - Email, URL, metin işleme kolaylaştı
  
- **Örnek**: `examples/19_string_functions.olang` - Tüm 16 fonksiyonun testleri

### 🔧 İyileştirmeler
- `#include <ctype.h>` eklendi (toupper, tolower, isdigit, isalpha için)
- String fonksiyonları bellek güvenli (malloc/free)
- Hata kontrolü ve sınır kontrolleri eklendi

### 📊 İstatistikler
- Toplam built-in fonksiyon sayısı: 39 → **55** (+16 string)
- Örnek dosya sayısı: 19 → **20**
- Toplam kod satırı: ~4300 → **~4500**

---

## [1.4.1] - 2025-10-13

### ✨ Eklenenler
- **String Indexing** - String'lere karakter seviyesinde erişim
  - Syntax: `str[index]` → tek karakterlik string döner
  - Örnek: `"Merhaba"[0]` → `"M"`
  - Index sınır kontrolü (0 ile uzunluk-1 arası)
  - JSON zincirleme ile uyumlu: `data["name"][0]`
  - İç içe yapılarda çalışır: `users[0]["name"][0]`
- **Örnek**: `examples/18_string_indexing.olang` ve `examples/test_string_simple.olang`

### 🐛 Düzeltmeler
- Hata mesajı güncellendi: "Erişilen değer bir dizi veya object değil" → "...veya string değil"

### 📊 İstatistikler
- Örnek dosya sayısı: 17 → **19**
- Toplam kod satırı: ~4200 → **~4300**

---

## [1.4.0] - 2025-10-13

### ✨ Eklenenler
- **Matematik Kütüphanesi** - 27 yerleşik matematik fonksiyonu
  - Temel: `abs()`, `sqrt()`, `cbrt()`, `pow()`, `hypot()`
  - Yuvarlama: `floor()`, `ceil()`, `round()`, `trunc()`
  - Trigonometrik: `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2()`
  - Hiperbolik: `sinh()`, `cosh()`, `tanh()`
  - Logaritma/Üstel: `exp()`, `log()`, `log10()`, `log2()`
  - İstatistik: `min()`, `max()` (sınırsız argüman)
  - Rastgele: `random()`, `randint()`
  - Diğer: `fmod()`
- **Dokümantasyon**: `MATH_FUNCTIONS.md` - Kapsamlı matematik fonksiyonları kılavuzu
- **Örnek**: `examples/17_math_functions.olang` - Tüm matematik fonksiyonlarının testi

### 📊 İstatistikler
- Toplam built-in fonksiyon sayısı: 12 → **39**
- Örnek dosya sayısı: 16 → **17**
- Toplam kod satırı: ~4000 → **~4200**

---

## [1.3.0] - 2025-10-13

### ✨ Eklenenler
- **UTF-8 Desteği** - Türkçe karakterler (ş, ğ, ü, ö, ç, ı)
  - Identifier'larda UTF-8 karakter kullanımı
  - Console output UTF-8 encoding
- **JSON Object Literals** - Hash table implementasyonu
  - `arrayJson obj = { "key": value }` syntax
  - djb2 hash algorithm, 16 bucket
  - Key-value erişimi
- **Nested Objects** - Sınırsız derinlikte iç içe objeler
  - Array içinde object
  - Object içinde array
- **Chained Access** - Zincirleme erişim
  - `arr[0]["key"][1]` syntax
  - Sınırsız seviye chaining
  - Recursive evaluation
- **Escape Sequences** - String formatl ama
  - `\n` - Yeni satır
  - `\t` - Tab
  - `\\` - Backslash
  - `\"` - Çift tırnak
  - `\r` - Carriage return
  - `\0` - Null karakter
- **Dokümantasyon**:
  - `examples/14_json_objects.olang` - JSON object örnekleri
  - `examples/15_nested_access.olang` - Zincirleme erişim testleri
  - `examples/16_escape_sequences.olang` - Escape sequence örnekleri

### 🔧 Değişiklikler
- Lexer: UTF-8 multi-byte character support
- Lexer: Escape sequence parsing
- Parser: Object literal parsing
- Parser: Chained array access (while loop)
- Interpreter: Hash table data structure
- Interpreter: Recursive nested access evaluation

### 📊 İstatistikler
- Veri tipleri: 9 → **10** (object eklendi, sonra arrayJson'a birleştirildi)
- Örnek dosyalar: 13 → **16**

---

## [1.2.2] - 2025-10-09

### ✨ Eklenenler
- **arrayJson** - JSON-like mixed arrays
- Nested array desteği

---

## [1.2.0] - Faz 2

### ✨ Eklenenler
- **Type-safe Arrays**
  - `arrayInt` - Integer dizileri
  - `arrayFloat` - Float dizileri
  - `arrayStr` - String dizileri
  - `arrayBool` - Boolean dizileri
  - `arrayJson` - JSON-like diziler
- **Array Fonksiyonları**
  - `length(arr)` - Dizi uzunluğu
  - `push(arr, val)` - Eleman ekleme
  - `pop(arr)` - Eleman çıkarma
- **Mixed Arrays** - `array` tipi

---

## [1.1.0] - Faz 1

### ✨ Eklenenler
- **Mantıksal Operatörler**: `&&`, `||`, `!`
- **Increment/Decrement**: `++`, `--`
- **Compound Assignment**: `+=`, `-=`, `*=`, `/=`
- **Break & Continue** - Döngü kontrolü
- **Type Conversion Fonksiyonları**
  - `toInt()`
  - `toFloat()`
  - `toString()`
  - `toBool()`

---

## [1.0.0] - Core Release

### ✨ Eklenenler
- **Temel Veri Tipleri**: `int`, `float`, `str`, `bool`
- **Operatörler**: Aritmetik (`+`, `-`, `*`, `/`), Karşılaştırma (`==`, `!=`, `<`, `>`, `<=`, `>=`)
- **Kontrol Yapıları**
  - `if/else`
  - `while` döngüsü
  - `for` döngüsü (C-style)
  - `for..in` döngüsü
- **Fonksiyonlar**
  - Kullanıcı tanımlı fonksiyonlar
  - Recursive fonksiyonlar
  - Return statements
- **Built-in Fonksiyonlar**
  - `print()` - Ekrana yazdırma
  - `input()` - String input
  - `inputInt()` - Integer input
  - `inputFloat()` - Float input
  - `range()` - Foreach helper
- **Mimari**
  - Lexer - Tokenization
  - Parser - AST oluşturma
  - Interpreter - Runtime execution
  - Symbol Table - Değişken yönetimi
  - Function Table - Fonksiyon yönetimi
  - Scope management

---

## Versiyon Numaralandırma

OLang [Semantic Versioning](https://semver.org/) kullanır:
- **Major**: Büyük değişiklikler, geriye uyumsuz değişiklikler
- **Minor**: Yeni özellikler, geriye uyumlu
- **Patch**: Hata düzeltmeleri, küçük iyileştirmeler

**Mevcut Versiyon**: v1.4.0

# OLang - Gelecek Özellikler ve Roadmap

## ✅ FAZ 1 - TAMAMLANDI! 🎉

### Temel Eksiklikler (Tamamlandı)
- [x] **Mantıksal Operatörler**: `&&`, `||`, `!`
- [x] **Increment/Decrement**: `++`, `--`
- [x] **Compound Assignment**: `+=`, `-=`, `*=`, `/=`
- [x] **Break ve Continue**: Döngü kontrol ifadeleri
- [x] **Type Conversion**: `toInt()`, `toFloat()`, `toString()`, `toBool()`

**Durum**: ✅ Tamamen çalışıyor ve test edildi!

---

## 📋 FAZ 2 - Veri Yapıları (Sonraki Adım)

### 2.1 Diziler (Arrays) - Yüksek Öncelik
**Tahmini Süre**: 4-6 saat

```olang
// Dizi tanımlama
int[] sayilar = [1, 2, 3, 4, 5];
str[] isimler = ["Ali", "Veli", "Ayşe"];

// Dizi erişimi
int ilk = sayilar[0];
sayilar[2] = 10;

// Dizi uzunluğu
int uzunluk = length(sayilar);

// Dizi metodları
push(sayilar, 6);         // Sona ekle
int son = pop(sayilar);   // Sondan çıkar
```

**İhtiyaçlar**:
- Lexer: `[`, `]` token'ları
- Parser: Dizi literal ve index erişimi
- Interpreter: Dinamik array implementasyonu
- Built-in fonksiyonlar: `length()`, `push()`, `pop()`

### 2.2 String Metodları - Orta Öncelik
**Tahmini Süre**: 2-3 saat

```olang
str metin = "Merhaba Dünya";
int uzunluk = length(metin);
str buyuk = toUpper(metin);
str kucuk = toLower(metin);
str[] parcalar = split(metin, " ");
bool iceriyor = contains(metin, "Dünya");
```

**İhtiyaçlar**:
- Built-in fonksiyonlar: `length()`, `toUpper()`, `toLower()`, `split()`, `contains()`, `charAt()`, `substring()`

---

## 📋 FAZ 3 - İleri Seviye Özellikler

### 3.1 Struct/Object - Orta Öncelik
**Tahmini Süre**: 6-8 saat

```olang
struct Person {
    str name;
    int age;
    str city;
}

Person kisi = Person("Ali", 25, "İstanbul");
print(kisi.name, kisi.age);
```

### 3.2 Dosya İşlemleri - Düşük Öncelik
**Tahmini Süre**: 3-4 saat

```olang
str icerik = readFile("data.txt");
writeFile("output.txt", "Merhaba");
bool varMi = fileExists("test.txt");
```

### 3.3 Hata Yönetimi - Orta Öncelik
**Tahmini Süre**: 4-5 saat

```olang
try {
    int sonuc = 10 / 0;
} catch (err) {
    print("Hata:", err);
}
```

### 3.4 Lambda/Anonymous Functions - Düşük Öncelik
**Tahmini Süre**: 5-6 saat

```olang
func(int x) -> int adder = func(int a) {
    return a + x;
};

int sonuc = adder(5);
```

---

## 📋 FAZ 4 - Optimizasyon ve İyileştirmeler

### 4.1 Standard Library
- Math: `abs()`, `sqrt()`, `pow()`, `min()`, `max()`
- Random: `random()`, `randomInt()`
- Time: `now()`, `sleep()`

### 4.2 Performans İyileştirmeleri
- AST optimizasyonu
- Constant folding
- Dead code elimination

### 4.3 Better Error Messages
- Satır ve sütun numaraları ile hata mesajları
- Stack trace
- Syntax highlighting

### 4.4 REPL (Interactive Mode)
- Interaktif komut satırı
- `.olang` dosya okumadan kod yazma

---

## 🎯 ÖNERİLEN SIRA

### Hemen Şimdi (1-2 hafta):
1. **Diziler** - En önemli eksiklik
2. **String metodları** - Pratik ve kullanışlı
3. **Math fonksiyonları** - Kolay ve faydalı

### Orta Vadeli (1 ay):
4. **Struct/Object** - Daha gelişmiş programlar için
5. **Hata yönetimi** - Kod kalitesi için
6. **Dosya işlemleri** - Gerçek uygulamalar için

### Uzun Vadeli (2-3 ay):
7. **Lambda fonksiyonlar** - Fonksiyonel programlama
8. **REPL** - Geliştirici deneyimi
9. **Performans optimizasyonları** - Hız iyileştirmeleri

---

## 📊 Mevcut Durum

### ✅ Tamamlanmış Özellikler:
- Veri tipleri: `int`, `float`, `str`, `bool`
- Değişkenler ve atama
- Aritmetik operatörler: `+`, `-`, `*`, `/`
- Karşılaştırma: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Mantıksal operatörler: `&&`, `||`, `!`
- Kontrol yapıları: `if/else`
- Döngüler: `while`, `for`, `for..in`
- Döngü kontrol: `break`, `continue`
- Fonksiyonlar: tanımlama, çağırma, parametreler, return
- Built-in fonksiyonlar: `print()`, `input()`, `inputInt()`, `inputFloat()`, `range()`
- Type conversion: `toInt()`, `toFloat()`, `toString()`, `toBool()`
- Increment/Decrement: `++`, `--`
- Compound assignment: `+=`, `-=`, `*=`, `/=`
- Recursive fonksiyonlar
- Scope yönetimi (global/local)

### ⏳ Eksik Özellikler:
- Diziler (arrays)
- String metodları
- Struct/Object
- Dosya işlemleri
- Hata yönetimi (try/catch)
- Lambda fonksiyonlar
- REPL modu
- Standard library (math, random, time)

---

## 💡 Sonuç

OLang şu anda **temel bir programlama dili** olarak çalışıyor! 🎉

**Faz 1 tamamlandı** ve dil artık çok daha güçlü. Sıradaki en önemli özellik **diziler** olmalı çünkü:
- Koleksiyon işlemleri için kritik
- Birçok algoritma için gerekli
- Öğrenme eğrisinde doğal bir sonraki adım

**Bir sonraki adım**: Faz 2'yi başlatalım ve dizileri ekleyelim! 🚀

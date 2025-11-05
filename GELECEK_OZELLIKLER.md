# TulparLang - Gelecek Özellikler ve Roadmap

## ✅ FAZ 1 - TAMAMLANDI! 🎉

### Temel Eksiklikler (Tamamlandı)
- [x] **Mantıksal Operatörler**: `&&`, `||`, `!`
- [x] **Increment/Decrement**: `++`, `--`
- [x] **Compound Assignment**: `+=`, `-=`, `*=`, `/=`
- [x] **Break ve Continue**: Döngü kontrol ifadeleri
- [x] **Type Conversion**: `toInt()`, `toFloat()`, `toString()`, `toBool()`

**Durum**: ✅ Tamamen çalışıyor ve test edildi!

---

## ✅ FAZ 2 - Veri Yapıları (Tamamlandı)

### 2.1 Diziler (Arrays)
**Durum**: ✅ Tamamlandı

```tulpar
// Dizi tanımlama (Tulpar söz dizimi)
arrayInt sayilar = [1, 2, 3, 4, 5];
arrayStr isimler = ["Ali", "Veli", "Ayşe"];

// Dizi erişimi
int ilk = sayilar[0];
sayilar[2] = 10;

// Dizi uzunluğu
int uzunluk = length(sayilar);

// Dizi fonksiyonları
push(sayilar, 6);         // Sona ekle
int son = pop(sayilar);   // Sondan çıkar
```

**Notlar**:
- Tipli diziler: `arrayInt`, `arrayFloat`, `arrayStr`, `arrayBool`
- Karışık tip dizi: `array`
- Sağlanan fonksiyonlar: `length()`, `push()`, `pop()`

### 2.2 String Metodları
**Durum**: ✅ Tamamlandı (çekirdek fonksiyonlar)

```tulpar
str metin = "Merhaba Dünya";
int uzunluk = length(metin);
str buyuk = upper(metin);
str kucuk = lower(metin);
arrayStr parcalar = split(metin, " ");
bool iceriyor = contains(metin, "Dünya");
str parcasi = substring(metin, 0, 7);
```

**Mevcut fonksiyonlar**:
- `length()`, `upper()`, `lower()`, `split()`, `contains()`, `substring()`, `replace()`, `trim()`, `indexOf()`, `startsWith()`, `endsWith()`, `repeat()`, `reverse()`, `isEmpty()`, `isDigit()`, `isAlpha()`

---

## 📋 FAZ 3 - İleri Seviye Özellikler

### 3.1 Struct/Object - Tamamlananlar ve Plan
**Durum**: 🟢 Kısmen tamamlandı (Object + type, named args, default alanlar, nested dot-assign)

Mevcut (Object - dinamik ve dot-assign):
```tulpar
var user = { "name": "Ali", "age": 25, "city": "İstanbul" };
print(user["name"], user["age"]);

array users = [
  { "name": "Ali",  "age": 25 },
  { "name": "Ayşe", "age": 30 }
];
print(length(users));
print(users[0]["name"]);

func makePerson(str name, int age, str city) {
    return { "name": name, "age": age, "city": city };
}
var p = makePerson("Veli", 28, "Ankara");
print(p["city"]);
```

Tamamlanan (type - statik şema, named arg, default):
```tulpar
type Person {
    str name;
    int age;
    str city = "İstanbul";
}

// Named arg ile constructor
Person kisi = Person(name: "Ali", age: 25);
print(kisi.name, kisi.age, kisi.city); // city → "İstanbul"

// Dot-assign (nested dahil)
kisi.name = "Veli";
order.customer.address.city = "Ankara";
```
Planlı (Genişletme):
- Nested type alanları: `type Order { Person customer; }`
- Type içi metotlar: `func Person.fullName() { ... }`

### 3.2 Dosya İşlemleri - Düşük Öncelik
**Tahmini Süre**: 3-4 saat

```tulpar
str icerik = readFile("data.txt");
writeFile("output.txt", "Merhaba");
bool varMi = fileExists("test.txt");
```

### 3.3 Hata Yönetimi - Orta Öncelik
**Tahmini Süre**: 4-5 saat

```tulpar
try {
    int sonuc = 10 / 0;
} catch (err) {
    print("Hata:", err);
}
```

### 3.4 Lambda/Anonymous Functions - Düşük Öncelik
**Tahmini Süre**: 5-6 saat

```tulpar
func(int x) -> int adder = func(int a) {
    return a + x;
};

int sonuc = adder(5);
```

---

## 📋 FAZ 4 - Optimizasyon ve İyileştirmeler

### 4.1 Standard Library
- Math (mevcut): `abs()`, `sqrt()`, `pow()`, `floor()`, `ceil()`, `round()`, `cbrt()`, `trunc()`, `min()`, `max()`,
  `sin()`, `cos()`, `tan()`, `asin()`, `acos()`, `atan()`, `atan2()`, `exp()`, `log()`, `log10()`, `log2()`, `sinh()`, `cosh()`, `tanh()`, `hypot()`, `fmod()`
- Random (mevcut): `random()`, `randint(a, b)`
- Time (planlı): `now()`, `sleep()`

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
- `.tpr` dosya okumadan kod yazma

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
- Diziler: `array`, `arrayInt`, `arrayFloat`, `arrayStr`, `arrayBool` + `length()`, `push()`, `pop()`
- String metodları: `upper()`, `lower()`, `split()`, `contains()`, `substring()`, `replace()`, `trim()`, `indexOf()`, `startsWith()`, `endsWith()`, `repeat()`, `reverse()`, `isEmpty()`, `isDigit()`, `isAlpha()`
- Genişletilmiş Math: yukarıda listelenen fonksiyonlar

### ⏳ Eksik Özellikler:
- Struct/Object
- Dosya işlemleri
- Hata yönetimi (try/catch)
- Lambda fonksiyonlar
- REPL modu
- Standard library (math, random, time)

---

## 💡 Sonuç

TulparLang şu anda **temel bir programlama dili** olarak çalışıyor! 🎉

**Faz 1 tamamlandı** ve dil artık çok daha güçlü. Sıradaki en önemli özellik **diziler** olmalı çünkü:
- Koleksiyon işlemleri için kritik
- Birçok algoritma için gerekli
- Öğrenme eğrisinde doğal bir sonraki adım

**Bir sonraki adım**: Faz 2'yi başlatalım ve dizileri ekleyelim! 🚀

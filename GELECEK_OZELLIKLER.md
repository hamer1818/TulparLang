# OLang - Gelecek Özellikler Roadmap 🗺️

## 📊 ŞU ANDA OLAN ÖZELLIKLER ✅

### Veri Tipleri
- ✅ int, float, str, bool

### Kontrol Yapıları
- ✅ if/else
- ✅ while döngüsü
- ✅ for döngüsü (C-style)
- ✅ for..in döngüsü (foreach)

### Fonksiyonlar
- ✅ Kullanıcı tanımlı fonksiyonlar
- ✅ Recursive fonksiyonlar
- ✅ Parametreler ve return

### Built-in Fonksiyonlar
- ✅ print()
- ✅ input(), inputInt(), inputFloat()
- ✅ range()

### Operatörler
- ✅ Aritmetik: +, -, *, /
- ✅ Karşılaştırma: ==, !=, <, >, <=, >=
- ✅ Atama: =

### Diğer
- ✅ Yorumlar (tek satır //)
- ✅ Global ve local scope
- ✅ Modüler kod yapısı

---

## 🎯 EKSİK ÖZELLIKLER VE ÖNCELİK SEVİYELERİ

---

## 🔥 YÜ KSEK ÖNCELİK (Must-Have)

### 1. **Break ve Continue** ⭐⭐⭐⭐⭐
```olang
for (int i = 0; i < 10; i = i + 1) {
    if (i == 5) break;
    if (i == 3) continue;
    print(i);
}
```
**Neden önemli:** Döngü kontrolü için temel
**Zorluk:** Kolay
**Tahmini süre:** 2-3 saat

---

### 2. **Array (Dizi) Desteği** ⭐⭐⭐⭐⭐
```olang
// Array tanımlama
int[] sayilar = [1, 2, 3, 4, 5];
str[] isimler = ["Ali", "Veli", "Can"];

// Eleman erişimi
int ilk = sayilar[0];
sayilar[1] = 10;

// Array uzunluğu
int uzunluk = len(sayilar);

// Array metodları
sayilar.push(6);
int son = sayilar.pop();
```
**Neden önemli:** Veri yapıları için kritik
**Zorluk:** Orta-Zor
**Tahmini süre:** 1-2 gün

---

### 3. **Mantıksal Operatörler (&&, ||, !)** ⭐⭐⭐⭐⭐
```olang
if (x > 5 && y < 10) {
    print("Her ikisi de doğru");
}

if (a == 5 || b == 10) {
    print("Biri doğru");
}

bool tersYon = !aktif;
```
**Neden önemli:** Karmaşık koşullar için gerekli
**Zorluk:** Kolay
**Tahmini süre:** 2-3 saat

---

### 4. **Compound Assignment (+=, -=, *=, /=)** ⭐⭐⭐⭐
```olang
int x = 5;
x += 3;   // x = x + 3
x -= 2;   // x = x - 2
x *= 4;   // x = x * 4
x /= 2;   // x = x / 2
```
**Neden önemli:** Kod yazımını kolaylaştırır
**Zorluk:** Kolay
**Tahmini süre:** 1-2 saat

---

### 5. **Increment/Decrement (++, --)** ⭐⭐⭐⭐
```olang
int i = 0;
i++;  // i = i + 1
i--;  // i = i - 1

// For döngüsünde
for (int j = 0; j < 10; j++) {
    print(j);
}
```
**Neden önemli:** C-like syntax için standart
**Zorluk:** Kolay
**Tahmini süre:** 1-2 saat

---

### 6. **String Metodları** ⭐⭐⭐⭐
```olang
str metin = "Merhaba Dünya";

// Uzunluk
int uzunluk = len(metin);

// Substring
str alt = substring(metin, 0, 7);  // "Merhaba"

// Büyük/küçük harf
str buyuk = toUpper(metin);
str kucuk = toLower(metin);

// Split
str[] kelimeler = split(metin, " ");

// Replace
str yeni = replace(metin, "Dünya", "OLang");

// Contains
bool varMi = contains(metin, "Merhaba");
```
**Neden önemli:** String manipülasyonu için kritik
**Zorluk:** Orta
**Tahmini süre:** 4-6 saat

---

### 7. **Type Conversion Fonksiyonları** ⭐⭐⭐⭐
```olang
// String to Int
int sayi = parseInt("123");

// Int to String
str metin = toString(456);

// String to Float
float ondalik = parseFloat("3.14");

// Float to Int
int tamSayi = toInt(3.14);  // 3
```
**Neden önemli:** Tip dönüşümleri için gerekli
**Zorluk:** Kolay
**Tahmini süre:** 2-3 saat

---

## 🔷 ORTA ÖNCELİK (Nice-to-Have)

### 8. **Math Fonksiyonları** ⭐⭐⭐⭐
```olang
int mutlak = abs(-5);          // 5
int kare = pow(2, 3);          // 8
float karekok = sqrt(16.0);    // 4.0
int max = max(10, 20);         // 20
int min = min(10, 20);         // 10
float yuvarlama = round(3.7);  // 4.0
float asagi = floor(3.7);      // 3.0
float yukari = ceil(3.2);      // 4.0
```
**Neden önemli:** Matematiksel işlemler için
**Zorluk:** Kolay
**Tahmini süre:** 2-3 saat

---

### 9. **Switch/Case Statement** ⭐⭐⭐
```olang
int gun = 3;

switch (gun) {
    case 1:
        print("Pazartesi");
        break;
    case 2:
        print("Salı");
        break;
    case 3:
        print("Çarşamba");
        break;
    default:
        print("Diğer");
}
```
**Neden önemli:** if/else zincirleri yerine
**Zorluk:** Orta
**Tahmini süre:** 4-6 saat

---

### 10. **Do-While Döngüsü** ⭐⭐⭐
```olang
int i = 0;
do {
    print(i);
    i++;
} while (i < 5);
```
**Neden önemli:** En az bir kez çalışması gereken döngüler
**Zorluk:** Kolay
**Tahmini süre:** 1-2 saat

---

### 11. **Çok Satırlı Yorumlar** ⭐⭐⭐
```olang
/*
  Bu bir çok satırlı
  yorum bloğudur
*/
int x = 5;
```
**Neden önemli:** Daha iyi dokümantasyon
**Zorluk:** Kolay
**Tahmini süre:** 1 saat

---

### 12. **Null/None Değeri** ⭐⭐⭐
```olang
str? metin = null;

if (metin == null) {
    print("Değer yok");
}
```
**Neden önemli:** Opsiyonel değerler için
**Zorluk:** Orta
**Tahmini süre:** 3-4 saat

---

### 13. **Const Değişkenler** ⭐⭐⭐
```olang
const int PI = 3.14159;
const str MESAJ = "Sabit mesaj";

// PI = 5;  // HATA: Const değiştirilemez
```
**Neden önemli:** Sabit değerler için
**Zorluk:** Kolay-Orta
**Tahmini süre:** 2-3 saat

---

### 14. **Ternary Operator** ⭐⭐⭐
```olang
int max = (a > b) ? a : b;
str durum = (yas >= 18) ? "Yetişkin" : "Çocuk";
```
**Neden önemli:** Kısa if/else için
**Zorluk:** Kolay
**Tahmini süre:** 2 saat

---

### 15. **File I/O (Dosya İşlemleri)** ⭐⭐⭐
```olang
// Dosya yazma
writeFile("test.txt", "Merhaba Dünya");

// Dosya okuma
str icerik = readFile("test.txt");

// Dosyaya ekleme
appendFile("log.txt", "Yeni satır\n");

// Dosya var mı kontrolü
bool varMi = fileExists("test.txt");
```
**Neden önemli:** Dosya işlemleri için
**Zorluk:** Orta
**Tahmini süre:** 6-8 saat

---

## 🔵 DÜŞÜK ÖNCELİK (Future Ideas)

### 16. **Struct/Class Desteği** ⭐⭐
```olang
struct Kisi {
    str isim;
    int yas;
}

Kisi kisi1 = Kisi("Ali", 25);
print(kisi1.isim);
kisi1.yas = 26;
```
**Neden önemli:** OOP için
**Zorluk:** Zor
**Tahmini süre:** 3-5 gün

---

### 17. **Module/Import Sistemi** ⭐⭐
```olang
// math.olang
func kare(int x) {
    return x * x;
}

// main.olang
import "math.olang";

int sonuc = kare(5);
```
**Neden önemli:** Kod modülerliği
**Zorluk:** Zor
**Tahmini süre:** 4-6 gün

---

### 18. **Lambda/Anonymous Fonksiyonlar** ⭐⭐
```olang
func map(int[] arr, func f) {
    int[] sonuc = [];
    for (item in arr) {
        sonuc.push(f(item));
    }
    return sonuc;
}

int[] sayilar = [1, 2, 3, 4];
int[] kareler = map(sayilar, (x) => x * x);
```
**Neden önemli:** Functional programming
**Zorluk:** Zor
**Tahmini süre:** 5-7 gün

---

### 19. **Higher-Order Functions** ⭐⭐
```olang
// map
int[] kareler = map([1, 2, 3], (x) => x * x);

// filter
int[] ciftler = filter([1, 2, 3, 4], (x) => x % 2 == 0);

// reduce
int toplam = reduce([1, 2, 3, 4], (a, b) => a + b, 0);
```
**Neden önemli:** Functional programming
**Zorluk:** Zor
**Tahmini süre:** 4-5 gün

---

### 20. **Error Handling (Try/Catch)** ⭐⭐
```olang
try {
    int x = parseInt("abc");  // Hata!
} catch (error) {
    print("Hata:", error);
}
```
**Neden önemli:** Hata yönetimi
**Zorluk:** Zor
**Tahmini süre:** 5-7 gün

---

### 21. **String Interpolation** ⭐
```olang
str isim = "Ali";
int yas = 25;
str mesaj = `Merhaba ${isim}, yaşınız ${yas}`;
```
**Neden önemli:** Daha kolay string oluşturma
**Zorluk:** Orta
**Tahmini süre:** 3-4 saat

---

### 22. **Enum (Numaralandırma)** ⭐
```olang
enum Renk {
    KIRMIZI,
    YESIL,
    MAVI
}

Renk secim = Renk.KIRMIZI;
```
**Neden önemli:** Sabit değerler grubu
**Zorluk:** Orta
**Tahmini süre:** 4-6 saat

---

### 23. **Variadic Functions (Değişken sayıda parametre)** ⭐
```olang
func topla(...int sayilar) {
    int toplam = 0;
    for (s in sayilar) {
        toplam += s;
    }
    return toplam;
}

int sonuc = topla(1, 2, 3, 4, 5);
```
**Neden önemli:** Esneklik
**Zorluk:** Orta-Zor
**Tahmini süre:** 6-8 saat

---

## 📋 ÖNCELİK SIRASI ÖNERİSİ

### Faz 1 - Temel Eksiklikler (1-2 hafta)
1. ✅ Break/Continue
2. ✅ Mantıksal operatörler (&&, ||, !)
3. ✅ Compound assignment (+=, -=, etc)
4. ✅ Increment/Decrement (++, --)
5. ✅ Type conversion fonksiyonları

### Faz 2 - Veri Yapıları (2-3 hafta)
6. ✅ Array desteği
7. ✅ String metodları
8. ✅ Math fonksiyonları

### Faz 3 - Gelişmiş Özellikler (3-4 hafta)
9. ✅ Switch/case
10. ✅ Do-while
11. ✅ Çok satırlı yorumlar
12. ✅ Null değer
13. ✅ Const değişkenler
14. ✅ Ternary operator

### Faz 4 - İleri Seviye (1-2 ay)
15. ✅ File I/O
16. ✅ Struct/Class
17. ✅ Module/Import
18. ✅ Error handling

---

## 🎯 İLK ADIM ÖNERİLERİ

**En kolay ve en faydalı olanlardan başla:**

1. **Break/Continue** (2-3 saat) - Döngü kontrolü
2. **Mantıksal Operatörler** (2-3 saat) - Karmaşık koşullar
3. **++ ve --** (1-2 saat) - C-like syntax
4. **+= -= *= /=** (1-2 saat) - Kod yazımı kolaylığı
5. **Type Conversion** (2-3 saat) - Temel ihtiyaç

**Bu 5 özellik yaklaşık 10-15 saatte eklenebilir ve OLang çok daha kullanışlı olur!**

---

## 💡 BONUS: STANDART KÜTÜPHANEBuilt-in Fonksiyonlar)

### String Fonksiyonları
- `len(str)` - Uzunluk
- `substring(str, start, end)`
- `toUpper(str)`, `toLower(str)`
- `trim(str)` - Boşlukları sil
- `split(str, delimiter)`
- `join(arr, delimiter)`

### Array Fonksiyonları
- `len(arr)` - Uzunluk
- `push(arr, item)` - Sona ekle
- `pop(arr)` - Sondan sil
- `slice(arr, start, end)`
- `indexOf(arr, item)`

### Math Fonksiyonları
- `abs(x)`, `pow(x, y)`, `sqrt(x)`
- `max(...)`, `min(...)`
- `round(x)`, `floor(x)`, `ceil(x)`
- `random()`, `randomInt(min, max)`

### Utility Fonksiyonları
- `sleep(ms)` - Bekle
- `time()` - Şu anki zaman
- `type(x)` - Tip kontrolü

---

## 📊 ZORLUK VE SÜRE TABLOSUkarşılaştırma)

| Özellik | Zorluk | Süre | Öncelik |
|---------|--------|------|---------|
| Break/Continue | ⭐ | 2-3h | 🔥🔥🔥🔥🔥 |
| &&, \|\|, ! | ⭐ | 2-3h | 🔥🔥🔥🔥🔥 |
| ++, -- | ⭐ | 1-2h | 🔥🔥🔥🔥 |
| +=, -=, *=, /= | ⭐ | 1-2h | 🔥🔥🔥🔥 |
| Type Conversion | ⭐ | 2-3h | 🔥🔥🔥🔥 |
| Array | ⭐⭐⭐ | 1-2d | 🔥🔥🔥🔥🔥 |
| String Methods | ⭐⭐ | 4-6h | 🔥🔥🔥🔥 |
| Math Functions | ⭐ | 2-3h | 🔥🔥🔥 |
| Switch/Case | ⭐⭐ | 4-6h | 🔥🔥🔥 |
| Do-While | ⭐ | 1-2h | 🔥🔥 |
| File I/O | ⭐⭐ | 6-8h | 🔥🔥🔥 |
| Struct/Class | ⭐⭐⭐⭐ | 3-5d | 🔥🔥 |
| Module/Import | ⭐⭐⭐⭐ | 4-6d | 🔥🔥 |
| Lambda/HOF | ⭐⭐⭐⭐⭐ | 5-7d | 🔥 |
| Try/Catch | ⭐⭐⭐⭐ | 5-7d | 🔥🔥 |

---

**Sonuç:** OLang zaten güçlü bir temel var! Bu özellikler eklendikçe tam teşekküllü bir programlama dili olacak! 🚀

**İlk adım olarak Faz 1'deki 5 özelliği öneririm - hızlı ve çok faydalı!** 💪


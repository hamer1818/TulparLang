# OLang - Matematik Fonksiyonları Kılavuzu 📐

OLang dilinde yerleşik olarak gelen matematik fonksiyonları.

## 📋 İçindekiler

1. [Temel Aritmetik](#temel-aritmetik)
2. [Yuvarlama Fonksiyonları](#yuvarlama-fonksiyonları)
3. [Üs ve Kök Alma](#üs-ve-kök-alma)
4. [Trigonometrik Fonksiyonlar](#trigonometrik-fonksiyonlar)
5. [Ters Trigonometrik](#ters-trigonometrik)
6. [Hiperbolik Fonksiyonlar](#hiperbolik-fonksiyonlar)
7. [Logaritma ve Üstel](#logaritma-ve-üstel)
8. [Min/Max ve İstatistik](#minmax-ve-istatistik)
9. [Rastgele Sayılar](#rastgele-sayılar)
10. [Özel Fonksiyonlar](#özel-fonksiyonlar)

---

## Temel Aritmetik

### `abs(x)`
Mutlak değer döndürür.

```olang
float sonuc = abs(-15.7);  // 15.7
```

---

## Yuvarlama Fonksiyonları

### `floor(x)`
Aşağı yuvarlar (en yakın küçük tam sayı).

```olang
int sonuc = floor(3.7);  // 3
int sonuc2 = floor(-3.7);  // -4
```

### `ceil(x)`
Yukarı yuvarlar (en yakın büyük tam sayı).

```olang
int sonuc = ceil(3.2);  // 4
int sonuc2 = ceil(-3.2);  // -3
```

### `round(x)`
En yakın tam sayıya yuvarlar.

```olang
int sonuc = round(3.5);  // 4
int sonuc2 = round(3.4);  // 3
```

### `trunc(x)`
Ondalık kısmı atar, tam sayı kısmını döndürür.

```olang
int sonuc = trunc(3.9);  // 3
int sonuc2 = trunc(-3.9);  // -3
```

---

## Üs ve Kök Alma

### `pow(x, y)`
x üzeri y hesaplar.

```olang
float sonuc = pow(2.0, 8.0);  // 256
float sonuc2 = pow(5.0, 3.0);  // 125
```

### `sqrt(x)`
Karekök hesaplar.

```olang
float sonuc = sqrt(25.0);  // 5
float sonuc2 = sqrt(2.0);  // 1.41421
```

### `cbrt(x)`
Küpkök hesaplar.

```olang
float sonuc = cbrt(27.0);  // 3
float sonuc2 = cbrt(8.0);  // 2
```

---

## Trigonometrik Fonksiyonlar

**NOT:** Tüm trigonometrik fonksiyonlar **radyan** cinsinden çalışır.

### `sin(x)`, `cos(x)`, `tan(x)`
Temel trigonometrik fonksiyonlar.

```olang
float pi = 3.14159;
float angle = pi / 4.0;  // 45 derece

float sinVal = sin(angle);  // 0.707
float cosVal = cos(angle);  // 0.707
float tanVal = tan(angle);  // 1.0
```

---

## Ters Trigonometrik

### `asin(x)`, `acos(x)`, `atan(x)`
Ters trigonometrik fonksiyonlar (arcsin, arccos, arctan).

```olang
float asinVal = asin(0.5);  // pi/6 radyan (30°)
float acosVal = acos(0.5);  // pi/3 radyan (60°)
float atanVal = atan(1.0);  // pi/4 radyan (45°)
```

### `atan2(y, x)`
İki argümanlı arctanjant. Koordinat sisteminde açı hesaplamak için.

```olang
// (1, 1) noktasının açısı
float angle = atan2(1.0, 1.0);  // pi/4 radyan (45°)

// (-1, 1) noktasının açısı
float angle2 = atan2(1.0, -1.0);  // 3*pi/4 radyan (135°)
```

**Avantaj:** `atan2()` tüm dört çeyreği doğru hesaplar ve sıfıra bölme hatasından kaçınır.

---

## Hiperbolik Fonksiyonlar

### `sinh(x)`, `cosh(x)`, `tanh(x)`
Hiperbolik sinüs, kosinüs ve tanjant.

```olang
float sinhVal = sinh(1.0);  // 1.175
float coshVal = cosh(1.0);  // 1.543
float tanhVal = tanh(1.0);  // 0.762
```

**Kullanım alanları:**
- Mühendislik hesaplamaları
- Fizik problemleri
- Makine öğrenmesi (aktivasyon fonksiyonları)

---

## Logaritma ve Üstel

### `exp(x)`
e üzeri x hesaplar (eˣ).

```olang
float e = exp(1.0);  // 2.71828 (Euler sayısı)
float sonuc = exp(2.0);  // 7.389
```

### `log(x)`
Doğal logaritma (ln x, e tabanında).

```olang
float sonuc = log(2.71828);  // 1.0
float sonuc2 = log(10.0);  // 2.302
```

### `log10(x)`
10 tabanında logaritma.

```olang
float sonuc = log10(100.0);  // 2.0
float sonuc2 = log10(1000.0);  // 3.0
```

### `log2(x)`
2 tabanında logaritma.

```olang
float sonuc = log2(8.0);  // 3.0
float sonuc2 = log2(1024.0);  // 10.0
```

---

## Min/Max ve İstatistik

### `min(a, b, ...)`
Verilen sayılardan en küçüğünü döndürür.

```olang
float sonuc = min(5.0, 3.0, 8.0, 1.0);  // 1.0
```

### `max(a, b, ...)`
Verilen sayılardan en büyüğünü döndürür.

```olang
float sonuc = max(5.0, 3.0, 8.0, 1.0);  // 8.0
```

**NOT:** Her iki fonksiyon da sınırsız sayıda argüman alabilir.

---

## Rastgele Sayılar

### `random()`
0 ile 1 arasında rastgele ondalık sayı üretir (0 dahil, 1 hariç).

```olang
float r1 = random();  // Örnek: 0.724
float r2 = random();  // Örnek: 0.182
float r3 = random();  // Örnek: 0.956
```

### `randint(a, b)`
a ile b arasında (her ikisi de dahil) rastgele tam sayı üretir.

```olang
int zar = randint(1, 6);  // 1-6 arası zar atışı
int yas = randint(18, 65);  // 18-65 arası yaş
int kart = randint(1, 52);  // Kart çekme
```

**Kullanım örnekleri:**
```olang
// Oyun: Zar atma
int zar1 = randint(1, 6);
int zar2 = randint(1, 6);
int toplam = zar1 + zar2;
print("Zarlar:", zar1, "+", zar2, "=", toplam);

// Rastgele ondalık sayı aralığı
float min = 10.0;
float max = 50.0;
float rastgele = min + random() * (max - min);
```

---

## Özel Fonksiyonlar

### `hypot(x, y)`
Hipotenüs hesaplar: √(x² + y²)

```olang
// Pisagor teoremi
float c = hypot(3.0, 4.0);  // 5.0

// İki nokta arası mesafe
float mesafe = hypot(6.0, 8.0);  // 10.0
```

**Avantaj:** Sayısal kararlılık açısından `sqrt(x*x + y*y)` yazımından daha güvenlidir.

### `fmod(x, y)`
Kayan noktalı sayılarda mod alma.

```olang
float sonuc = fmod(7.5, 2.3);  // 0.6
float sonuc2 = fmod(10.5, 3.0);  // 1.5
```

**Fark:** Normal `%` operatörü sadece tam sayılarda çalışır, `fmod()` ondalık sayılarda da çalışır.

---

## 🧮 Pratik Örnekler

### Daire Hesaplamaları
```olang
float pi = 3.14159;
float r = 5.0;

// Alan
float alan = pi * pow(r, 2.0);
print("Daire alanı:", alan);  // 78.54

// Çevre
float cevre = 2.0 * pi * r;
print("Çevre:", cevre);  // 31.42
```

### Pisagor Teoremi
```olang
float a = 3.0;
float b = 4.0;
float c = sqrt(pow(a, 2.0) + pow(b, 2.0));
// veya daha iyi:
float c2 = hypot(a, b);
print("Hipotenüs:", c2);  // 5.0
```

### Açı Dönüşümü (Derece ↔ Radyan)
```olang
// Derece -> Radyan
float pi = 3.14159;
float derece = 45.0;
float radyan = derece * (pi / 180.0);
print("45° =", radyan, "radyan");  // 0.785

// Radyan -> Derece
float rad = 1.57;
float deg = rad * (180.0 / pi);
print("1.57 rad =", deg, "derece");  // 90°
```

### Üstel Büyüme (Compound Interest)
```olang
// Yıllık %5 faiz, 10 yıl
float anaParа = 1000.0;
float oran = 0.05;
int yil = 10;

float sonuc = anaParа * pow(1.0 + oran, toFloat(yil));
print("10 yıl sonra:", sonuc);  // 1628.89
```

### Rastgele Şifre Üretimi
```olang
// 4 haneli PIN kodu
int pin = randint(1000, 9999);
print("PIN:", pin);

// Rastgele boolean
int rastgeleBool = randint(0, 1);
bool yazıTura = rastgeleBool == 1;
```

---

## 📊 Fonksiyon Listesi (Alfabetik)

| Fonksiyon | Açıklama | Örnek |
|-----------|----------|-------|
| `abs(x)` | Mutlak değer | `abs(-5.0)` → 5.0 |
| `acos(x)` | Arckosinüs | `acos(1.0)` → 0.0 |
| `asin(x)` | Arcsinüs | `asin(0.5)` → 0.524 |
| `atan(x)` | Arctanjant | `atan(1.0)` → 0.785 |
| `atan2(y, x)` | İki argümanlı arctan | `atan2(1, 1)` → 0.785 |
| `cbrt(x)` | Küpkök | `cbrt(27.0)` → 3.0 |
| `ceil(x)` | Yukarı yuvarla | `ceil(3.2)` → 4 |
| `cos(x)` | Kosinüs | `cos(0.0)` → 1.0 |
| `cosh(x)` | Hiperbolik kosinüs | `cosh(0.0)` → 1.0 |
| `exp(x)` | e üzeri x | `exp(1.0)` → 2.718 |
| `floor(x)` | Aşağı yuvarla | `floor(3.7)` → 3 |
| `fmod(x, y)` | Kayan nokta mod | `fmod(7.5, 2.0)` → 1.5 |
| `hypot(x, y)` | Hipotenüs | `hypot(3, 4)` → 5.0 |
| `log(x)` | Doğal logaritma | `log(2.718)` → 1.0 |
| `log10(x)` | 10 tabanında log | `log10(100)` → 2.0 |
| `log2(x)` | 2 tabanında log | `log2(8)` → 3.0 |
| `max(...)` | Maximum değer | `max(1, 5, 3)` → 5 |
| `min(...)` | Minimum değer | `min(1, 5, 3)` → 1 |
| `pow(x, y)` | x üzeri y | `pow(2, 8)` → 256 |
| `random()` | 0-1 arası rastgele | `random()` → 0.724 |
| `randint(a, b)` | a-b arası rastgele int | `randint(1, 6)` → 4 |
| `round(x)` | Yuvarla | `round(3.5)` → 4 |
| `sin(x)` | Sinüs | `sin(1.57)` → 1.0 |
| `sinh(x)` | Hiperbolik sinüs | `sinh(0.0)` → 0.0 |
| `sqrt(x)` | Karekök | `sqrt(25)` → 5.0 |
| `tan(x)` | Tanjant | `tan(0.785)` → 1.0 |
| `tanh(x)` | Hiperbolik tanjant | `tanh(0.0)` → 0.0 |
| `trunc(x)` | Ondalık kısmı at | `trunc(3.9)` → 3 |

---

## 🎯 Toplam Fonksiyon Sayısı

**27 matematik fonksiyonu** yerleşik olarak kullanıma hazır!

---

## 📝 Notlar

1. **Radyan vs Derece:** Tüm trigonometrik fonksiyonlar radyan cinsinden çalışır.
2. **Tip Dönüşümü:** Fonksiyonlar hem int hem float değerlerle çalışır.
3. **Hata Kontrolü:** Negatif sayının karekökü gibi geçersiz işlemler tanımsız davranışa yol açabilir.

---

**OLang v1.3.0** - Matematik Kütüphanesi ✨

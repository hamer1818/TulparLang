# TulparLang - Hızlı Başlangıç 🚀

## 5 Dakikada TulparLang!

### 1️⃣ Derleme

```bash
# Windows (WSL ile)
wsl bash build.sh

# Linux/Mac
make
```

### 2️⃣ İlk Programınız

`hello.tpr` dosyası oluşturun:
```tulpar
print("Merhaba TulparLang!");

int x = 10;
int y = 20;
int toplam = x + y;

print("Toplam:", toplam);
```

Çalıştırın:
```bash
wsl ./tulpar hello.tpr    # Windows
./tulpar hello.tpr         # Linux/Mac
```

### 3️⃣ Fonksiyonlar

```tulpar
func topla(int a, int b) {
    return a + b;
}

func faktoryel(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * faktoryel(n - 1);
}

int sonuc = topla(5, 3);
int fak = faktoryel(5);

print("5 + 3 =", sonuc);
print("5! =", fak);
```

### 4️⃣ Döngüler

```tulpar
// While döngüsü
int i = 0;
while (i < 5) {
    print("i =", i);
    i++;  // Faz 1!
}

// For döngüsü
for (int j = 0; j < 5; j++) {  // Faz 1!
    if (j == 2) continue;  // Faz 1!
    print("j =", j);
}

// Foreach döngüsü
for (k in range(5)) {
    print("k =", k);
}
```

### 5️⃣ Kullanıcı Girişi

```tulpar
str isim = input("Adınız: ");
int yas = inputInt("Yaşınız: ");

print("Merhaba", isim);
print("Yaşınız:", yas);

if (yas >= 18) {
    print("Reşitsiniz!");
} else {
    print("Reşit değilsiniz.");
}
```

### 6️⃣ Yeni Özellikler (Faz 1) ✨

```tulpar
// Mantıksal operatörler
int x = 5;
int y = 10;

if (x > 0 && y > 0) {
    print("İkisi de pozitif!");
}

if (x == 0 || y == 0) {
    print("En az biri sıfır");
}

bool tersYon = !true;  // false

// Increment/Decrement
int sayac = 0;
sayac++;  // 1
sayac++;  // 2
sayac--;  // 1

// Compound Assignment
int toplam = 100;
toplam += 50;   // 150
toplam -= 30;   // 120
toplam *= 2;    // 240
toplam /= 4;    // 60

// Break ve Continue
for (int i = 0; i < 10; i++) {
    if (i == 3) continue;  // 3'ü atla
    if (i == 7) break;     // 7'de dur
    print(i);
}

// Type Conversion
int sayi = toInt("123");
float ondalik = toFloat("3.14");
str metin = toString(42);
bool deger = toBool(1);

print("Çevrilen sayı:", sayi);
print("Çevrilen metin:", metin);

// Diziler (PHP tarzı)
array karma = [1, "Ali", 3.14];  // Karışık tip
arrayInt sayilar = [1, 2, 3, 4, 5];  // Sadece int
arrayStr isimler = ["Ali", "Veli"];   // Sadece string

print("Karma:", karma);
print("Sayılar:", sayilar);

int ilk = sayilar[0];
print("İlk eleman:", ilk);

push(sayilar, 6);  // ✅ OK
print("Push sonrası:", sayilar);

// push(sayilar, "hata");  // ❌ Tip hatası!

int uzunluk = length(sayilar);
print("Uzunluk:", uzunluk);
```

## 📚 Daha Fazla Bilgi

- **Detaylı kullanım**: `KULLANIM.md`
- **Tam dokümantasyon**: `README.md`
- **Gelecek özellikler**: `GELECEK_OZELLIKLER.md`
- **Örnek kodlar**: `examples/` klasörü

## 🎯 Örnek Programlar

### Fibonacci
```tulpar
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

for (i in range(10)) {
    print("fibonacci(", i, ") =", fibonacci(i));
}
```

### Çarpım Tablosu
```tulpar
for (i in range(1, 11)) {
    for (j in range(1, 11)) {
        int sonuc = i * j;
        print(i, "x", j, "=", sonuc);
    }
    print("");  // Boş satır
}
```

### Hesap Makinesi
```tulpar
print("=== Basit Hesap Makinesi ===");

int a = inputInt("Birinci sayı: ");
int b = inputInt("İkinci sayı: ");

print("Toplam:", a + b);
print("Fark:", a - b);
print("Çarpım:", a * b);
print("Bölüm:", a / b);
```

## ✅ Başarıyla Tamamladınız!

Artık TulparLang ile kod yazmaya hazırsınız! 🎉

**Sonraki adımlar:**
1. `examples/` klasöründeki örnekleri inceleyin
2. Kendi programlarınızı yazın
3. `GELECEK_OZELLIKLER.md` dosyasına bakarak neler geleceğini görün

İyi kodlamalar! 💪

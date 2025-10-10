# OLang - Hızlı Başlangıç 🚀

**OLang**, C tabanlı, basit ve güçlü bir programlama dilidir. Lexer, Parser ve Interpreter ile tam çalışan bir dil implementasyonu.

## 🎯 Özellikler

### Veri Tipleri
- `int` - Tam sayılar
- `float` - Ondalıklı sayılar
- `str` - String (metinler)
- `bool` - Boolean (true/false)
- `array` - Karışık tipli diziler (PHP tarzı) ✨ Faz 2
- `arrayInt` - Sadece integer dizileri (tip güvenlikli) ✨
- `arrayFloat` - Sadece float dizileri (tip güvenlikli) ✨
- `arrayStr` - Sadece string dizileri (tip güvenlikli) ✨
- `arrayBool` - Sadece boolean dizileri (tip güvenlikli) ✨
- `arrayJson` - JSON-like karma diziler (nested destekli) ✨ YENİ!

### Operatörler

#### Aritmetik
- `+`, `-`, `*`, `/` - Temel işlemler
- `++`, `--` - Increment/Decrement (Faz 1 ✨)
- `+=`, `-=`, `*=`, `/=` - Compound Assignment (Faz 1 ✨)

#### Karşılaştırma
- `==`, `!=` - Eşitlik kontrolü
- `<`, `>`, `<=`, `>=` - Büyük-küçük karşılaştırma

#### Mantıksal
- `&&` - AND (Faz 1 ✨)
- `||` - OR (Faz 1 ✨)
- `!` - NOT (Faz 1 ✨)

### Söz Dizimi (Syntax)

#### Değişken Tanımlama
```olang
int x = 5;
float pi = 3.14;
str isim = "Ahmet";
bool aktif = true;

// Increment/Decrement (Faz 1)
x++;     // x = 6
x--;     // x = 5

// Compound Assignment (Faz 1)
x += 10;  // x = 15
x -= 3;   // x = 12
x *= 2;   // x = 24
x /= 4;   // x = 6
```

#### Fonksiyon Tanımlama
```olang
func topla(int a, int b) {
    int sonuc = a + b;
    return sonuc;
}
```

#### Fonksiyon Çağırma
```olang
int toplam = topla(5, 3);  // toplam = 8
```

#### If/Else Yapısı
```olang
// Basit if/else
if (x > 5) {
    int y = 10;
} else {
    int y = 0;
}

// Mantıksal operatörler ile (Faz 1)
if (x > 5 && y < 10) {
    print("Hem x > 5 hem y < 10");
}

if (x == 0 || y == 0) {
    print("En az biri sıfır");
}

bool tersYon = !aktif;  // NOT operatörü
```

#### While Döngüsü
```olang
int i = 0;
while (i < 10) {
    i++;  // Increment ile (Faz 1)
    
    if (i == 5) continue;  // Continue (Faz 1)
    if (i == 8) break;     // Break (Faz 1)
}
```

#### For Döngüsü (C-style)
```olang
// Klasik for döngüsü
for (int i = 0; i < 10; i++) {  // i++ kullanımı (Faz 1)
    if (i == 3) continue;  // 3'ü atla
    if (i == 7) break;     // 7'de dur
    print("i =", i);
}

// Compound assignment ile
for (int i = 0; i < 100; i += 10) {  // 10'ar 10'ar artır (Faz 1)
    print("i =", i);  // 0, 10, 20, ..., 90
}
```

#### Foreach Döngüsü (for..in)
```olang
// range() fonksiyonu ile
for (i in range(10)) {
    print("i =", i);  // 0'dan 9'a kadar
}
```

#### Recursive Fonksiyonlar
```olang
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    int a = fibonacci(n - 1);
    int b = fibonacci(n - 2);
    return a + b;
}

int fib5 = fibonacci(5);  // fib5 = 5
```

#### Diziler (Arrays) - Faz 2 ✨
```olang
// 1. Karışık tipli diziler (mixed)
array karma = [1, "Ali", 3.14, true];
print(karma);  // [1, "Ali", 3.14, true]

// 2. Tip güvenlikli diziler (type-safe)
arrayInt sayilar = [1, 2, 3, 4, 5];
arrayStr isimler = ["Ali", "Veli", "Ayşe"];
arrayFloat floats = [1.5, 2.5, 3.14];
arrayBool flags = [true, false, true];

// 3. JSON-like diziler (nested destekli) ✨ YENİ!
arrayJson kullanici = ["Ali", 25, true, "Mühendis"];
arrayJson apiResponse = [200, "Success", true];
arrayJson nested = [["user1", 25], ["user2", 30]];  // İç içe!

// 4. Erişim ve değiştirme
int ilk = sayilar[0];  // 1
sayilar[2] = 100;      // OK
str isim = kullanici[0];  // "Ali"

// 5. Tip güvenliği
push(sayilar, 6);      // ✅ OK (int)
push(sayilar, "hata"); // ❌ HATA! Sadece int kabul eder
push(kullanici, "yeni");  // ✅ OK (json mixed)

// 6. Built-in fonksiyonlar
int uzunluk = length(sayilar);  // 5
push(sayilar, 6);               // Eleman ekle
int son = pop(sayilar);         // Son elemanı çıkar

// 7. Döngü ile
for (int i = 0; i < length(sayilar); i++) {
    print(sayilar[i]);
}
```

## 🌍 Platform Desteği

OLang **tüm platformlarda** çalışır:
- ✅ **Linux** (Ubuntu, Fedora, Arch, etc.)
- ✅ **macOS** (Intel & Apple Silicon)
- ✅ **Windows** (MinGW, Visual Studio, WSL)

**Detaylı kurulum**: `PLATFORM_SUPPORT.md` | **Hızlı kurulum**: `QUICK_INSTALL.md`

## 🔧 Derleme ve Çalıştırma

### 1. Projeyi Derleyin

#### Windows (WSL ile)
```bash
wsl bash build.sh
```

#### Linux/Mac
```bash
chmod +x build.sh
./build.sh
```

#### Makefile ile
```bash
# Otomatik (CMake veya Makefile)
./build.sh          # Linux/macOS/WSL

# veya
build.bat           # Windows

# veya manuel
make                # Unix-like
```

### 2. OLang Dosyalarını Çalıştırın

#### Dosyadan çalıştırma:
```bash
# WSL ile (Windows)
wsl ./olang examples/fibonacci.olang
wsl ./olang examples/calculator.olang
wsl ./olang examples/hello.olang

# Linux/Mac
./olang examples/fibonacci.olang
./olang examples/calculator.olang
./olang examples/hello.olang
```

#### Demo kodu çalıştırma (argümansız):
```bash
wsl ./olang      # Windows
./olang          # Linux/Mac
```

### 3. Kendi Dosyanızı Oluşturun

`mycode.olang` adında bir dosya oluşturun:
```olang
int x = 10;
int y = 20;

func topla(int a, int b) {
    return a + b;
}

int sonuc = topla(x, y);
```

Çalıştırın:
```bash
wsl ./olang mycode.olang    # Windows
./olang mycode.olang         # Linux/Mac
```

## 📁 Proje Yapısı

```
OLang/
├── src/
│   ├── lexer/
│   │   ├── lexer.c     # Token'lara ayırma
│   │   └── lexer.h
│   ├── parser/
│   │   ├── parser.c    # Abstract Syntax Tree oluşturma
│   │   └── parser.h
│   ├── interpreter/
│   │   ├── interpreter.c   # Kodu çalıştırma motoru
│   │   └── interpreter.h
│   └── main.c          # Ana program
├── build/              # Derleme çıktıları
├── examples/           # Örnek kodlar
├── Makefile
├── build.sh
├── README.md
├── KULLANIM.md         # Detaylı kullanım kılavuzu
├── QUICKSTART.md       # Hızlı başlangıç
└── GELECEK_OZELLIKLER.md   # Roadmap
```

## 🏗️ Mimari

OLang üç ana bileşenden oluşur:

### 1. **LEXER** (Tokenization)
Kaynak kodu token'lara ayırır:
```
int x = 5; → [TOKEN_INT_TYPE, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INT_LITERAL, TOKEN_SEMICOLON]
```

### 2. **PARSER** (AST Oluşturma)
Token'ları Abstract Syntax Tree'ye dönüştürür:
```
VAR_DECL: x
  └── INT: 5
```

### 3. **INTERPRETER** (Çalıştırma)
AST'yi dolaşarak kodu çalıştırır:
- Symbol Table ile değişken yönetimi
- Function Table ile fonksiyon yönetimi
- Scope yönetimi (global ve local)
- Runtime değer hesaplama

## 🔧 Built-in Fonksiyonlar

### Input/Output Fonksiyonları
- `print(...)` - Ekrana değer yazdırır (birden fazla argüman alabilir)
- `input("prompt")` - Kullanıcıdan string okur
- `inputInt("prompt")` - Kullanıcıdan integer okur
- `inputFloat("prompt")` - Kullanıcıdan float okur

### Type Conversion Fonksiyonları (Faz 1 ✨)
- `toInt(value)` - Herhangi bir değeri integer'a çevirir
- `toFloat(value)` - Herhangi bir değeri float'a çevirir
- `toString(value)` - Herhangi bir değeri string'e çevirir
- `toBool(value)` - Herhangi bir değeri boolean'a çevirir

```olang
// Örnekler
int sayi = toInt("123");           // 123
float ondalik = toFloat("3.14");   // 3.14
str metin = toString(42);          // "42"
bool deger = toBool(1);            // true
```

### Array Fonksiyonları (Faz 2 ✨)
- `length(arr)` - Dizi uzunluğunu döner
- `push(arr, value)` - Diziye eleman ekler
- `pop(arr)` - Diziden son elemanı çıkarır ve döner

```olang
// Örnekler
array sayilar = [1, 2, 3];
int len = length(sayilar);    // 3
push(sayilar, 4);              // [1, 2, 3, 4]
int son = pop(sayilar);        // 4, dizi: [1, 2, 3]
```

### Yardımcı Fonksiyonlar
- `range(n)` - 0'dan n'e kadar sayı dizisi (foreach için)

### Örnek Kullanım
```olang
// Print kullanımı
print("Merhaba Dünya!");
int x = 10;
print("x =", x);

// Input kullanımı
str isim = input("Adınız: ");
int yas = inputInt("Yaşınız: ");
print("Merhaba", isim, "! Yaşınız:", yas);
```

## 🎓 Desteklenen Operatörler

### Aritmetik Operatörler
- `+` Toplama
- `-` Çıkarma
- `*` Çarpma
- `/` Bölme

### Karşılaştırma Operatörleri
- `==` Eşittir
- `!=` Eşit değildir
- `<` Küçüktür
- `>` Büyüktür
- `<=` Küçük veya eşittir
- `>=` Büyük veya eşittir

### Atama Operatörü
- `=` Atama

## 📝 Örnek Programlar

### Basit Program
```olang
// Değişkenler
int x = 5;
float pi = 3.14;
str mesaj = "Merhaba OLang!";
bool basarili = true;

// Fonksiyon
func kare(int n) {
    return n * n;
}

// Kullanım
int sonuc = kare(10);  // sonuc = 100

// If yapısı
if (sonuc > 50) {
    str durum = "Büyük";
}

// While döngüsü
int i = 0;
while (i < 5) {
    i = i + 1;
}
```

### İnteraktif Program
```olang
// Kullanıcıdan input alma
print("=== OLang Hesap Makinesi ===");

int a = inputInt("Birinci sayi: ");
int b = inputInt("Ikinci sayi: ");

int toplam = a + b;
int carpim = a * b;

print("Toplam:", toplam);
print("Carpim:", carpim);
```

## 🚀 Gelecek Özellikler

- [ ] For döngüsü
- [ ] Array desteği
- [ ] String metodları
- [ ] Standart kütüphane fonksiyonları (print, input, vb.)
- [ ] Class/Struct desteği
- [ ] Import/Module sistemi
- [ ] Hata mesajları iyileştirme
- [ ] Optimizasyon

## 📄 Lisans

Bu proje eğitim amaçlı geliştirilmiştir. Özgürce kullanabilir, değiştirebilir ve dağıtabilirsiniz.

## 👨‍💻 Geliştirici

**Hamza** - OLang yaratıcısı

---

**OLang** - OLang dilini kullanın! 🎉


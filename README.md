# OLang - Kendi Programlama Diliniz! 🚀

**OLang**, C tabanlı, basit ve güçlü bir programlama dilidir. Lexer, Parser ve Interpreter ile tam çalışan bir dil implementasyonu.

## 🎯 Özellikler

### Veri Tipleri
- `int` - Tam sayılar
- `float` - Ondalıklı sayılar
- `str` - String (metinler)
- `bool` - Boolean (true/false)

### Söz Dizimi (Syntax)

#### Değişken Tanımlama
```olang
int x = 5;
float pi = 3.14;
str isim = "Ahmet";
bool aktif = true;
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
if (x > 5) {
    int y = 10;
} else {
    int y = 0;
}
```

#### While Döngüsü
```olang
int i = 0;
while (i < 10) {
    i = i + 1;
}
```

#### For Döngüsü (C-style)
```olang
for (int i = 0; i < 10; i = i + 1) {
    print("i =", i);
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
make
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
│   ├── lexer.c         # Token'lara ayırma
│   ├── lexer.h
│   ├── parser.c        # Abstract Syntax Tree oluşturma
│   ├── parser.h
│   ├── interpreter.c   # Kodu çalıştırma motoru
│   ├── interpreter.h
│   └── main.c          # Ana program
├── build/              # Derleme çıktıları
├── examples/           # Örnek kodlar
├── Makefile
├── build.sh            # Linux/Mac/WSL build script
├── build.bat           # Windows build script
└── README.md
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

**OLang** - Kendi dilinizi yaratın! 🎉


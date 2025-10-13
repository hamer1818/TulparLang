# OLang - Hızlı Başlangıç 🚀

**OLang**, C tabanlı, basit ve güçlü bir programlama dilidir. Lexer, Parser ve Interpreter ile tam çalışan bir dil implementasyonu.

## 🎯 Özellikler

### Veri Tipleri

- `int` - Tam sayılar
- `float` - Ondalıklı sayılar
- `str` - String (metinler) - **UTF-8 destekli** ✨
- `bool` - Boolean (true/false)
- `array` - Karışık tipli diziler (PHP tarzı) ✨ Faz 2
- `arrayInt` - Sadece integer dizileri (tip güvenlikli) ✨
- `arrayFloat` - Sadece float dizileri (tip güvenlikli) ✨
- `arrayStr` - Sadece string dizileri (tip güvenlikli) ✨
- `arrayBool` - Sadece boolean dizileri (tip güvenlikli) ✨
- `arrayJson` - JSON-like karma diziler (nested destekli, **object literal desteği** ✨) ✨

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
str şehir = "İstanbul";  // UTF-8 destekli! ✨
bool aktif = true;

// Increment/Decrement (Faz 1)
x++;     // x = 6
x--;     // x = 5

// Compound Assignment (Faz 1)
x += 10;  // x = 15
x -= 3;   // x = 12
x *= 2;   // x = 24
x /= 4;   // x = 6

// Escape Sequences (String içinde) ✨ YENİ!
str mesaj = "Satır 1\nSatır 2";        // Yeni satır
str yol = "C:\\Users\\Desktop";        // Backslash
str json = "{\"ad\": \"Hamza\"}";      // Tırnak işareti
str tab = "Ad:\tHamza";                // Tab karakteri
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

// 3. JSON-like diziler (nested destekli)
arrayJson kullanici = ["Ali", 25, true, "Mühendis"];
arrayJson apiResponse = [200, "Success", true];
arrayJson nested = [["user1", 25], ["user2", 30]];  // İç içe!

// 4. JSON Object Literals (Hash Table) ✨ YENİ!
arrayJson user = {
    "ad": "Hamza",
    "yas": 25,
    "sehir": "İstanbul",
    "aktif": true
};

// 5. Nested Objects (İç içe objeler) ✨ YENİ!
arrayJson firma = {
    "isim": "Tech Corp",
    "ceo": {
        "ad": "Hamza",
        "iletisim": {
            "email": "hamza@techcorp.com",
            "telefon": "555-0123"
        }
    }
};

// 6. Chained Access (Zincirleme erişim) ✨ YENİ!
str email = firma["ceo"]["iletisim"]["email"];  // "hamza@techcorp.com"

// 7. Array içinde Object
arrayJson ekip = {
    "isim": "Dev Team",
    "uyeler": [
        {"ad": "Ahmet", "rol": "Backend"},
        {"ad": "Mehmet", "rol": "Frontend"}
    ]
};
str lider = ekip["uyeler"][0]["ad"];  // "Ahmet"

// 8. Sınırsız derinlik! ✨
arrayJson proje = {
    "ekipler": [
        {
            "lider": {
                "yetenekler": ["C", "Python", "Go"]
            }
        }
    ]
};
str yetenek = proje["ekipler"][0]["lider"]["yetenekler"][0];  // "C"

// 9. Tip güvenliği (type-safe arrays)
push(sayilar, 6);      // ✅ OK (int)
push(sayilar, "hata"); // ❌ HATA! Sadece int kabul eder

// 10. Built-in fonksiyonlar
int uzunluk = length(sayilar);  // 5
push(sayilar, 6);               // Eleman ekle
int son = pop(sayilar);         // Son elemanı çıkar

// 11. Döngü ile
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

#### Dosyadan çalıştırma

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

#### Demo kodu çalıştırma (argümansız)

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

```plaintext
OLang/
├── src/
│   ├── lexer/
│   │   ├── lexer.c     # Token'lara ayırma (UTF-8 + Escape sequences)
│   │   └── lexer.h
│   ├── parser/
│   │   ├── parser.c    # Abstract Syntax Tree (Object literals + Chained access)
│   │   └── parser.h
│   ├── interpreter/
│   │   ├── interpreter.c   # Runtime (Hash table + Nested access)
│   │   └── interpreter.h
│   └── main.c          # Ana program (UTF-8 console setup)
├── build/              # Derleme çıktıları
├── examples/           # 16 örnek kod dosyası
│   ├── 01-13_*.olang   # Temel örnekler
│   ├── 14_json_objects.olang    # JSON object örnekleri ✨
│   ├── 15_nested_access.olang   # Zincirleme erişim ✨
│   └── 16_escape_sequences.olang # Escape sequence örnekleri ✨
├── Makefile
├── build.sh / build.bat
├── README.md
├── README_EN.md        # English documentation
├── KULLANIM.md         # Detaylı kullanım kılavuzu
├── QUICKSTART.md       # Hızlı başlangıç
└── GELECEK_OZELLIKLER.md   # Roadmap
```

## 🏗️ Mimari

OLang üç ana bileşenden oluşur:

### 1. **LEXER** (Tokenization)

Kaynak kodu token'lara ayırır:

```C
int x = 5; → [TOKEN_INT_TYPE, TOKEN_IDENTIFIER, TOKEN_ASSIGN, TOKEN_INT_LITERAL, TOKEN_SEMICOLON]
```

**Yeni Özellikler:**

- ✅ UTF-8 karakter desteği (Türkçe: ş, ğ, ü, ö, ç, ı)
- ✅ Escape sequence desteği (`\"`, `\n`, `\t`, `\\`, `\r`, `\0`)
- ✅ Object literal tokenization (`{`, `}`, `:`)

### 2. **PARSER** (AST Oluşturma)

Token'ları Abstract Syntax Tree'ye dönüştürür:

```plaintext
VAR_DECL: x
  └── INT: 5
```

**Yeni Özellikler:**

- ✅ Object literal parsing: `{ "key": value }`
- ✅ Chained array access: `arr[0]["key"][1]` (sınırsız derinlik)
- ✅ Nested AST nodes with `left` field

### 3. **INTERPRETER** (Çalıştırma)

AST'yi dolaşarak kodu çalıştırır:

- Symbol Table ile değişken yönetimi
- Function Table ile fonksiyon yönetimi
- Scope yönetimi (global ve local)
- Runtime değer hesaplama

**Yeni Özellikler:**

- ✅ Hash Table (djb2 algorithm, 16 buckets)
- ✅ Object value type (`VAL_OBJECT`)
- ✅ Recursive nested access evaluation
- ✅ Deep copy support for objects

## 🔧 Built-in Fonksiyonlar

### Input/Output Fonksiyonları

- `print(...)` - Ekrana değer yazdırır (birden fazla argüman alabilir, UTF-8 destekli ✨)
- `input("prompt")` - Kullanıcıdan string okur (UTF-8 destekli ✨)
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

- `length(arr)` - Dizi/object uzunluğunu döner
- `push(arr, value)` - Diziye eleman ekler
- `pop(arr)` - Diziden son elemanı çıkarır ve döner

```olang
// Örnekler
array sayilar = [1, 2, 3];
int len = length(sayilar);    // 3
push(sayilar, 4);              // [1, 2, 3, 4]
int son = pop(sayilar);        // 4, dizi: [1, 2, 3]

// Object ile
arrayJson kullanici = {"ad": "Hamza", "yas": 25};
print(kullanici["ad"]);        // "Hamza"
```

### String Escape Sequences ✨ **YENİ!**

String içinde özel karakterler:

- `\n` - Yeni satır (newline)
- `\t` - Tab karakteri
- `\r` - Carriage return
- `\\` - Backslash
- `\"` - Çift tırnak
- `\0` - Null karakter

```olang
// Örnekler
print("Satır 1\nSatır 2");           // İki satır
print("Ad:\tHamza");                 // Tab ile hizalı
print("Yol: C:\\Users\\Desktop");    // Windows yolu
print("JSON: {\"ad\": \"Hamza\"}");  // JSON string
```

### Matematik Fonksiyonları (Faz 3 ✨) **YENİ!**

**27 yerleşik matematik fonksiyonu!** Detaylar için: `MATH_FUNCTIONS.md`

#### Temel Fonksiyonlar
- `abs(x)` - Mutlak değer
- `sqrt(x)` - Karekök
- `cbrt(x)` - Küpkök
- `pow(x, y)` - x üzeri y
- `hypot(x, y)` - Hipotenüs

#### Yuvarlama
- `floor(x)` - Aşağı yuvarla
- `ceil(x)` - Yukarı yuvarla
- `round(x)` - Yuvarla
- `trunc(x)` - Ondalık kısmı at

#### Trigonometrik (Radyan)
- `sin(x)`, `cos(x)`, `tan(x)` - Temel trigonometri
- `asin(x)`, `acos(x)`, `atan(x)` - Ters trigonometrik
- `atan2(y, x)` - İki argümanlı arctan

#### Hiperbolik
- `sinh(x)`, `cosh(x)`, `tanh(x)` - Hiperbolik fonksiyonlar

#### Logaritma ve Üstel
- `exp(x)` - e üzeri x
- `log(x)` - Doğal logaritma (ln)
- `log10(x)` - 10 tabanında log
- `log2(x)` - 2 tabanında log

#### İstatistik
- `min(a, b, ...)` - Minimum değer
- `max(a, b, ...)` - Maximum değer

#### Rastgele
- `random()` - 0-1 arası rastgele float
- `randint(a, b)` - a-b arası rastgele int

#### Diğer
- `fmod(x, y)` - Kayan nokta mod

```olang
// Örnekler
float karekok = sqrt(25.0);           // 5.0
float ust = pow(2.0, 8.0);            // 256.0
int yuvarla = round(3.7);             // 4
float sinDeg = sin(1.57);             // 1.0 (90° in radians)
float minVal = min(5.0, 3.0, 8.0);    // 3.0
int zar = randint(1, 6);              // 1-6 arası
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
// Değişkenler (UTF-8 destekli ✨)
int x = 5;
float pi = 3.14;
str mesaj = "Merhaba OLang!";
str şehir = "İstanbul";  // Türkçe karakter!
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

### JSON Object Program ✨ **YENİ!**

```olang
// Object literal oluşturma
arrayJson kullanici = {
    "ad": "Hamza",
    "soyad": "Ortatepe",
    "yas": 25,
    "sehir": "İstanbul",
    "aktif": true
};

// Object erişimi
print("Kullanıcı:", kullanici["ad"], kullanici["soyad"]);
print("Yaş:", kullanici["yas"]);

// Nested object (İç içe)
arrayJson firma = {
    "isim": "Tech Corp",
    "ceo": {
        "ad": "Hamza",
        "iletisim": {
            "email": "hamza@techcorp.com"
        }
    }
};

// Zincirleme erişim (Chained access)
str email = firma["ceo"]["iletisim"]["email"];
print("CEO Email:", email);  // "hamza@techcorp.com"

// Array içinde object
arrayJson ekip = {
    "uyeler": [
        {"ad": "Ahmet", "rol": "Backend"},
        {"ad": "Mehmet", "rol": "Frontend"}
    ]
};

// Karmaşık zincirleme
str lider = ekip["uyeler"][0]["ad"];
print("Lider:", lider);  // "Ahmet"
```

### Escape Sequence Örneği ✨ **YENİ!**

```olang
// String içinde özel karakterler
print("Satır 1\nSatır 2\nSatır 3");        // Yeni satır
print("Ad:\tHamza\nYaş:\t25");             // Tab
print("Yol: C:\\Users\\Hamza\\Desktop");   // Backslash
print("JSON: {\"ad\": \"Hamza\"}");        // Tırnak

// Object içinde escape
arrayJson config = {
    "path": "C:\\Program Files\\OLang",
    "message": "\"Welcome\"\nto OLang!"
};
print(config["message"]);
// Çıktı:
// "Welcome"
// to OLang!
```

### İnteraktif Program

```olang
// Kullanıcıdan input alma (UTF-8 destekli ✨)
print("=== OLang Hesap Makinesi ===");

int a = inputInt("Birinci sayi: ");
int b = inputInt("Ikinci sayi: ");

int toplam = a + b;
int carpim = a * b;

print("Toplam:", toplam);
print("Carpim:", carpim);
```

## 🚀 Tamamlanan Özellikler ✅

### Faz 1

- ✅ Temel veri tipleri (int, float, str, bool)
- ✅ Fonksiyonlar ve recursive fonksiyonlar
- ✅ Control flow (if/else, while, for, for..in)
- ✅ Mantıksal operatörler (&&, ||, !)
- ✅ Increment/Decrement (++, --)
- ✅ Compound assignment (+=, -=, *=, /=)
- ✅ Break & Continue
- ✅ Type conversion (toInt, toFloat, toString, toBool)

### Faz 2

- ✅ Mixed arrays (array)
- ✅ Type-safe arrays (arrayInt, arrayFloat, arrayStr, arrayBool)
- ✅ JSON arrays (arrayJson)
- ✅ Array fonksiyonları (length, push, pop)

### Faz 3 (Yeni! ✨)

- ✅ **UTF-8 desteği** - Türkçe karakterler (şehir, ülke, yaş)
- ✅ **JSON Object Literals** - Hash table ile `{ "key": value }`
- ✅ **Nested Objects** - Sınırsız derinlikte iç içe objeler
- ✅ **Chained Access** - `arr[0]["key"][1]` zincirleme erişim
- ✅ **Escape Sequences** - `\"`, `\n`, `\t`, `\\`, `\r`, `\0`

## 🔮 Gelecek Özellikler

- [ ] Dot notation - `obj.key.nested` syntax
- [ ] Object methods - `keys()`, `values()`, `merge()`
- [ ] Spread operator - `...obj`, `...arr`
- [ ] String metodları (split, join, substring)
- [ ] Class/Struct desteği
- [ ] Import/Module sistemi
- [ ] Daha iyi hata mesajları
- [ ] Optimizasyon ve JIT compilation

## 📊 İstatistikler

- **Toplam Kod Satırı**: ~4200+ (yorumlar hariç)
- **Örnek Dosyalar**: 17 ✨
- **Veri Tipleri**: 9 (int, float, str, bool, array, arrayInt, arrayFloat, arrayStr, arrayBool, arrayJson)
- **Built-in Fonksiyonlar**: **39+** (12 yardımcı + **27 matematik** ✨)
- **Desteklenen Platformlar**: Linux, macOS, Windows
- **Encoding**: UTF-8 (Türkçe karakter desteği)
- **Hash Table Buckets**: 16 (djb2 algorithm)

## 🎯 Öne Çıkan Özellikler

1. **UTF-8 Desteği** 🌍 - Türkçe ve diğer dillerdeki karakterler
2. **JSON Objects** 📦 - Hash table ile hızlı key-value erişimi
3. **Nested Structures** 🔗 - Sınırsız derinlikte iç içe yapılar
4. **Chained Access** ⛓️ - `data["users"][0]["profile"]["email"]` gibi erişim
5. **Escape Sequences** 🔤 - Professional string formatting (`\n`, `\t`, `\"`, `\\`)
6. **Matematik Kütüphanesi** 📐 - **27 matematik fonksiyonu** (trigonometri, logaritma, rastgele sayılar) ✨ **YENİ!**
7. **Type Safety** 🛡️ - Type-safe arrays ile güvenli kod
8. **Cross-Platform** 💻 - Linux, macOS, Windows desteği

## �📄 Lisans

Bu proje eğitim amaçlı geliştirilmiştir. Özgürce kullanabilir, değiştirebilir ve dağıtabilirsiniz.

## 👨‍💻 Geliştirici

**Hamza Ortatepe** - OLang yaratıcısı  
GitHub: [@hamer1818](https://github.com/hamer1818)

## 🔗 Bağlantılar

- **GitHub Repository**: [https://github.com/hamer1818/OLang](https://github.com/hamer1818/OLang)
- **VS Code Extension**: [https://github.com/hamer1818/olan-ext](https://github.com/hamer1818/olan-ext)
- **Documentation**: [README.md](README.md), [README_EN.md](README_EN.md), [QUICKSTART.md](QUICKSTART.md), [MATH_FUNCTIONS.md](MATH_FUNCTIONS.md) ✨

---

**OLang v1.4.0** - Modern, UTF-8 destekli, JSON-native, matematik kütüphaneli programlama dili! 🎉  
**Son Güncelleme**: 13 Ekim 2025

# TulparLang - Hızlı Başlangıç 🚀

**TulparLang**, C tabanlı, basit ve güçlü bir programlama dilidir. Lexer, Parser ve Interpreter ile tam çalışan bir dil implementasyonu.

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

```tulpar
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

```tulpar
func topla(int a, int b) {
    int sonuc = a + b;
    return sonuc;
}
```

#### Fonksiyon Çağırma

```tulpar
int toplam = topla(5, 3);  // toplam = 8
```

#### If/Else Yapısı

```tulpar
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

```tulpar
int i = 0;
while (i < 10) {
    i++;  // Increment ile (Faz 1)
    
    if (i == 5) continue;  // Continue (Faz 1)
    if (i == 8) break;     // Break (Faz 1)
}
```

#### For Döngüsü (C-style)

```tulpar
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

```tulpar
// range() fonksiyonu ile
for (i in range(10)) {
    print("i =", i);  // 0'dan 9'a kadar
}
```

#### Recursive Fonksiyonlar

```tulpar
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

```tulpar
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

TulparLang **tüm platformlarda** çalışır:

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

### 2. TulparLang Dosyalarını Çalıştırın

#### Dosyadan çalıştırma

```bash
# WSL ile (Windows)
wsl ./tulpar examples/fibonacci.tpr
wsl ./tulpar examples/calculator.tpr
wsl ./tulpar examples/hello.tpr

# Linux/Mac
./tulpar examples/fibonacci.tpr
./tulpar examples/calculator.tpr
./tulpar examples/hello.tpr
```

#### Demo kodu çalıştırma (argümansız)

```bash
wsl ./tulpar      # Windows
./tulpar          # Linux/Mac
```

### 3. Kendi Dosyanızı Oluşturun

`mycode.tpr` adında bir dosya oluşturun:

```tulpar
int x = 10;
int y = 20;

func topla(int a, int b) {
    return a + b;
}

int sonuc = topla(x, y);
```

Çalıştırın:

```bash
wsl ./tulpar mycode.tpr    # Windows
./tulpar mycode.tpr         # Linux/Mac
```

## 📁 Proje Yapısı

```plaintext
TulparLang/
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
│   ├── 01-13_*.tpr   # Temel örnekler
│   ├── 14_json_objects.tpr    # JSON object örnekleri ✨
│   ├── 15_nested_access.tpr   # Zincirleme erişim ✨
│   └── 16_escape_sequences.tpr # Escape sequence örnekleri ✨
├── Makefile
├── build.sh / build.bat
├── README.md
├── README_EN.md        # English documentation
├── KULLANIM.md         # Detaylı kullanım kılavuzu
├── QUICKSTART.md       # Hızlı başlangıç
└── GELECEK_OZELLIKLER.md   # Roadmap
```

## 🏗️ Mimari

TulparLang üç ana bileşenden oluşur:

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
- ✅ Type deklarasyonu: `type Name { ... }` ve oluşturucu çağrısı `Name(...)` (named args + default alanlar) ✨

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
- ✅ Type registry ve constructor: `type` ile tanımlanan şemaların runtime oluşturulması ✨

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

```tulpar
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

```tulpar
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

```tulpar
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

```tulpar
// Örnekler
float karekok = sqrt(25.0);           // 5.0
float ust = pow(2.0, 8.0);            // 256.0
int yuvarla = round(3.7);             // 4
float sinDeg = sin(1.57);             // 1.0 (90° in radians)
float minVal = min(5.0, 3.0, 8.0);    // 3.0
int zar = randint(1, 6);              // 1-6 arası
```

### String İşlemleri (Faz 4 ✨) **YENİ!**

#### String Indexing - Character-Level Access

Stringler karakter dizisi gibi işlem görür ve index ile erişilebilir:

```tulpar
str isim = "Ahmet";
print(isim[0]);      // "A"
print(isim[1]);      // "h"
print(isim[4]);      // "t"

// JSON'dan string çıkarıp index ile erişim
arrayJson kisi = {
    "isim": "Mehmet",
    "yas": 25
};

str ad = kisi["isim"];
print(ad[0]);        // "M"

// Direkt JSON'dan string index
print(kisi["isim"][0]);  // "M"

// İç içe JSON + String indexing
arrayJson data = {
    "users": [
        {"name": "Alice", "role": "admin"}
    ]
};

print(data["users"][0]["name"]);      // "Alice"
print(data["users"][0]["name"][0]);   // "A"
```

**Özellikler:**

- ✅ String'lere index ile erişim: `str[0]`, `str[1]`, vb.
- ✅ Her karakter tek karakterlik string olarak döner
- ✅ Index sınır kontrolü (0 ile uzunluk-1 arası)
- ✅ JSON/Array zincirleri ile birlikte kullanılabilir
- ✅ Hata mesajı: "String index sınırların dışında"

### String İşleme Fonksiyonları (Faz 5 ✨) **YENİ!**

TulparLang, **16 yerleşik string fonksiyonu** ile güçlü metin işleme yetenekleri sunar:

#### Dönüşüm Fonksiyonları

```tulpar
str text = "Hello World";
upper(text)         // "HELLO WORLD"
lower(text)         // "hello world"
capitalize(text)    // "Hello world"
reverse(text)       // "dlroW olleH"
```

#### Temizleme ve Düzenleme

```tulpar
trim("  Hamza  ")              // "Hamza"
replace("Hi Hi", "Hi", "Bye")  // "Bye Bye"
```

#### Arama ve Kontrol

```tulpar
contains("Hello", "ell")          // true
startsWith("Hello", "He")         // true
endsWith("Hello", "lo")           // true
indexOf("abcabc", "abc")          // 0 (ilk konum)
count("banana", "a")              // 3
```

#### Alt String ve Tekrarlama

```tulpar
substring("JavaScript", 0, 4)  // "Java"
repeat("Ha", 3)                // "HaHaHa"
```

#### Bölme ve Birleştirme

```tulpar
arrayStr parts = split("a,b,c", ",");  // ["a", "b", "c"]
str joined = join("-", parts);          // "a-b-c"
```

#### Kontrol Fonksiyonları

```tulpar
isEmpty("")          // true
isDigit("12345")     // true
isAlpha("abcdef")    // true
```

**Tüm String Fonksiyonları:**

- `upper(s)`, `lower(s)`, `capitalize(s)`, `reverse(s)`
- `trim(s)`, `replace(s, old, new)`
- `contains(s, sub)`, `startsWith(s, prefix)`, `endsWith(s, suffix)`
- `indexOf(s, sub)`, `count(s, sub)`
- `substring(s, start, end)`, `repeat(s, n)`
- `split(s, delimiter)`, `join(separator, array)`
- `isEmpty(s)`, `isDigit(s)`, `isAlpha(s)`

**Örnek Email İşleme**

```tulpar
str email = "  HAMZA@EXAMPLE.COM  ";
str clean = lower(trim(email));         // "hamza@example.com"
arrayStr parts = split(clean, "@");     // ["hamza", "example.com"]
str username = parts[0];                // "hamza"
str domain = parts[1];                  // "example.com"
```

### Type (Struct) Desteği ✨ YENİ!

```tulpar
// Type tanımı (default alan desteği)
type Person {
    str name;
    int age;
    str city = "İstanbul";
}

// Oluşturucu: positional veya named arg
Person p1 = Person("Ali", 25, "İstanbul");
Person p2 = Person(name: "Ayşe", age: 30);   // city → "İstanbul" (default)

// Erişim
print(p1.name, p1.age, p1.city);

// Dot ile atama
p1.name = "Veli";

// Nested dot-assign (object ile birlikte)
arrayJson order = { "customer": { "address": { "city": "Bursa" } } };
order.customer.address.city = "Ankara";
```

### Yardımcı Fonksiyonlar

- `range(n)` - 0'dan n'e kadar sayı dizisi (foreach için)

### Örnek Kullanım

```tulpar
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

```tulpar
// Değişkenler (UTF-8 destekli ✨)
int x = 5;
float pi = 3.14;
str mesaj = "Merhaba TulparLang!";
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

```tulpar
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

```tulpar
// String içinde özel karakterler
print("Satır 1\nSatır 2\nSatır 3");        // Yeni satır
print("Ad:\tHamza\nYaş:\t25");             // Tab
print("Yol: C:\\Users\\Hamza\\Desktop");   // Backslash
print("JSON: {\"ad\": \"Hamza\"}");        // Tırnak

// Object içinde escape
arrayJson config = {
    "path": "C:\\Program Files\\TulparLang",
    "message": "\"Welcome\"\nto TulparLang!"
};
print(config["message"]);
// Çıktı:
// "Welcome"
// to TulparLang!
```

### İnteraktif Program

```tulpar
// Kullanıcıdan input alma (UTF-8 destekli ✨)
print("=== TulparLang Hesap Makinesi ===");

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
- ✅ **Type (Struct)** - `type Name { ... }` + named arg + default alanlar ✨

## 🔮 Gelecek Özellikler

- [x] Dot notation - `obj.key.nested` (nested dot-assign) ✨
- [ ] Object methods - `keys()`, `values()`, `merge()`
- [ ] Spread operator - `...obj`, `...arr`
- [ ] String metodları (split, join, substring)
- [x] Type/Struct benzeri: `type Name { ... }` + named arg + default alanlar ✨
- [ ] Import/Module sistemi
- [ ] Daha iyi hata mesajları
- [ ] Optimizasyon ve JIT compilation

## 📊 İstatistikler

- **Toplam Kod Satırı**: ~4300+ (yorumlar hariç)
- **Örnek Dosyalar**: 19 ✨ (yeni: string indexing, math demo)
- **Veri Tipleri**: 9 (int, float, str, bool, array, arrayInt, arrayFloat, arrayStr, arrayBool, arrayJson)
- **Built-in Fonksiyonlar**: **39+** (12 yardımcı + **27 matematik** ✨)
- **Desteklenen Platformlar**: Linux, macOS, Windows
- **Encoding**: UTF-8 (Türkçe karakter desteği)
- **Hash Table Buckets**: 16 (djb2 algorithm)
- **String Indexing**: ✅ Strings as character arrays ✨ **YENİ!**

## 🎯 Öne Çıkan Özellikler

1. **UTF-8 Desteği** 🌍 - Türkçe ve diğer dillerdeki karakterler
2. **JSON Objects** 📦 - Hash table ile hızlı key-value erişimi
3. **Nested Structures** 🔗 - Sınırsız derinlikte iç içe yapılar
4. **Chained Access** ⛓️ - `data["users"][0]["profile"]["email"]` gibi erişim
5. **Escape Sequences** 🔤 - Professional string formatting (`\n`, `\t`, `\"`, `\\`)
6. **Matematik Kütüphanesi** 📐 - **27 matematik fonksiyonu** (trigonometri, logaritma, rastgele sayılar) ✨
7. **String Indexing** 🔤 - `"Merhaba"[0]` → `"M"` (character-level access) ✨ **YENİ!**
8. **Type Safety** 🛡️ - Type-safe arrays ile güvenli kod
9. **Cross-Platform** 💻 - Linux, macOS, Windows desteği

## 📄 Lisans

Bu proje eğitim amaçlı geliştirilmiştir. Özgürce kullanabilir, değiştirebilir ve dağıtabilirsiniz.

## 👨‍💻 Geliştirici

**Hamza Ortatepe** - TulparLang yaratıcısı  
GitHub: [@hamer1818](https://github.com/hamer1818)

## 🔗 Bağlantılar

- **GitHub Repository**: [https://github.com/hamer1818/TulparLang](https://github.com/hamer1818/TulparLang)
- **VS Code Extension**: [https://github.com/hamer1818/tulpar-ext](https://github.com/hamer1818/tulpar-ext)
- **Documentation**: [README.md](README.md), [README_EN.md](README_EN.md), [QUICKSTART.md](QUICKSTART.md), [MATH_FUNCTIONS.md](MATH_FUNCTIONS.md) ✨

---

**TulparLang v1.6.0** - Modern, UTF-8 destekli, JSON-native, matematik ve string kütüphaneli, type desteği ile! 🎉  
**Son Güncelleme**: 05 Kasım 2025

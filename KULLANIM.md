# TulparLang Kullanım Kılavuzu 📚

## Hızlı Başlangıç

### 1. Projeyi Derleyin
```bash
# Windows (WSL ile)
wsl bash build.sh

# Linux/Mac
bash build.sh
```

### 2. Örnek Dosyaları Çalıştırın

```bash
# Fibonacci örneği
wsl ./tulpar examples/fibonacci.tpr

# Calculator örneği
wsl ./tulpar examples/calculator.tpr

# Hello örneği
wsl ./tulpar examples/hello.tpr

# Functions örneği
wsl ./tulpar examples/functions.tpr

# Control flow örneği
wsl ./tulpar examples/control_flow.tpr
```

## Kendi TulparLang Programınızı Yazın

### Adım 1: Dosya Oluşturun
Herhangi bir metin editöründe `.tpr` uzantılı bir dosya oluşturun:

```bash
# Örnek: test.tpr
```

### Adım 2: Kod Yazın
```tulpar
// test.tpr

// Değişkenler
int sayi1 = 10;
int sayi2 = 20;
int toplam = sayi1 + sayi2;

// Fonksiyon
func kare(int n) {
    return n * n;
}

// Kullanım
int sonuc = kare(5);
```

### Adım 3: Çalıştırın
```bash
wsl ./tulpar test.tpr
```

## Söz Dizimi Örnekleri

### Değişken Tanımlama
```tulpar
int x = 5;
float pi = 3.14;
str mesaj = "Merhaba";
bool aktif = true;
```

### Matematik İşlemleri
```tulpar
int a = 10;
int b = 3;

int toplam = a + b;    // 13
int fark = a - b;      // 7
int carpim = a * b;    // 30
int bolum = a / b;     // 3
```

### Fonksiyon Tanımlama
```tulpar
// Basit fonksiyon
func topla(int a, int b) {
    return a + b;
}

// Çoklu satır fonksiyon
func faktoriyel(int n) {
    if (n <= 1) {
        return 1;
    }
    int onceki = faktoriyel(n - 1);
    return n * onceki;
}
```

### If/Else Yapısı
```tulpar
int x = 15;

if (x > 10) {
    int sonuc = 100;
} else {
    int sonuc = 50;
}
```

### While Döngüsü
```tulpar
int i = 0;
int toplam = 0;

while (i < 10) {
    toplam = toplam + i;
    i = i + 1;
}
```

### Karşılaştırma Operatörleri
```tulpar
int a = 10;
int b = 20;

bool esit = a == b;           // false
bool esit_degil = a != b;     // true
bool kucuk = a < b;           // true
bool buyuk = a > b;           // false
bool kucuk_esit = a <= b;     // true
bool buyuk_esit = a >= b;     // false
```

### Built-in Fonksiyonlar

#### Print (Ekrana Yazdırma)
```tulpar
print("Merhaba Dünya!");
print("x =", 10);
print("Toplam:", 5 + 3);

int x = 42;
print("Cevap:", x);
```

#### Input (Kullanıcıdan Veri Alma)
```tulpar
// String okuma
str isim = input("Adınız: ");
print("Merhaba", isim);

// Integer okuma
int yas = inputInt("Yaşınız: ");
print("Yaşınız:", yas);

// Float okuma
float boy = inputFloat("Boyunuz (m): ");
print("Boyunuz:", boy, "metre");
```

### İnteraktif Program Örneği
```tulpar
print("=== İnteraktif Hesap Makinesi ===");

int sayi1 = inputInt("Birinci sayı: ");
int sayi2 = inputInt("İkinci sayı: ");

int toplam = sayi1 + sayi2;
int carpim = sayi1 * sayi2;

print("Toplam:", toplam);
print("Çarpım:", carpim);
```

## Programı Anlamak

TulparLang üç aşamada çalışır:

### 1. LEXER (Tokenization)
Kaynak kodunuzu token'lara ayırır:
```
int x = 5; → [TOKEN_INT_TYPE, TOKEN_IDENTIFIER("x"), TOKEN_ASSIGN, TOKEN_INT_LITERAL(5), TOKEN_SEMICOLON]
```

### 2. PARSER (AST Oluşturma)
Token'ları Abstract Syntax Tree'ye dönüştürür:
```
VAR_DECL: x
  └── INT: 5
```

### 3. INTERPRETER (Çalıştırma)
AST'yi dolaşarak kodu çalıştırır ve sonuç üretir.

## Yaygın Hatalar ve Çözümleri

### Hata: Dosya Bulunamadı
```
Hata: 'test.tpr' dosyasi acilamadi!
```
**Çözüm:** Dosya yolunun doğru olduğundan emin olun.

### Hata: Tanımlanmamış Değişken
```
Hata: Tanımlanmamış değişken 'x'
```
**Çözüm:** Değişkeni kullanmadan önce tanımlayın.

### Hata: Tanımlanmamış Fonksiyon
```
Hata: Tanımlanmamış fonksiyon 'topla'
```
**Çözüm:** Fonksiyonu çağırmadan önce tanımlayın.

### Hata: Sıfıra Bölme
```
Hata: Sıfıra bölme!
```
**Çözüm:** Bölme işlemlerinde bölenin sıfır olmadığından emin olun.

## İpuçları

1. **Noktalı virgül (;) kullanmayı unutmayın!** Her statement noktalı virgülle bitmelidir.

2. **Fonksiyonları kullanmadan önce tanımlayın.** TulparLang yukarıdan aşağıya çalışır.

3. **Değişken tiplerini belirtin.** Her değişken tanımında tip belirtilmelidir (int, float, str, bool).

4. **Recursive fonksiyonlara dikkat edin.** Sonsuz döngülerden kaçının, base case ekleyin.

5. **Yorumlar ekleyin.** `//` ile satır yorumu ekleyebilirsiniz.

## Örnek Programlar

### 1. Fibonacci Hesaplama
```tulpar
func fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    int a = fibonacci(n - 1);
    int b = fibonacci(n - 2);
    return a + b;
}

int fib10 = fibonacci(10);  // 55
```

### 2. Faktöriyel Hesaplama
```tulpar
func faktoriyel(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * faktoriyel(n - 1);
}

int fakt5 = faktoriyel(5);  // 120
```

### 3. Max/Min Bulma
```tulpar
func max(int a, int b) {
    if (a > b) {
        return a;
    }
    return b;
}

func min(int a, int b) {
    if (a < b) {
        return a;
    }
    return b;
}

int buyuk = max(10, 20);   // 20
int kucuk = min(10, 20);   // 10
```

## Gelişmiş Konular

### Recursive Fonksiyonlar
TulparLang recursive fonksiyonları destekler. Her recursive fonksiyon için:
- Base case ekleyin (döngüden çıkış koşulu)
- Her çağrıda problemi küçültün

### Scope Yönetimi
- Global scope: Dosya seviyesinde tanımlanan değişkenler
- Local scope: Fonksiyon içinde tanımlanan değişkenler
- Fonksiyonlar yeni bir scope oluşturur

### Tip Dönüşümleri
- int + float → float
- int * float → float
- float / int → float

---

**İyi kodlamalar!** 🚀

Sorularınız için: [GitHub Issues](https://github.com/your-repo/TulparLang/issues)


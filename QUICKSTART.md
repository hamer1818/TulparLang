# OLang Hızlı Başlangıç 🚀

## 📦 Derleme (Bir Kez)
```bash
wsl bash build.sh
```

## ▶️ Çalıştırma

### Örnek dosyaları çalıştır:
```bash
wsl ./olang examples/fibonacci.olang
wsl ./olang examples/calculator.olang
```

### Kendi kodunuzu yazın:

**1. Dosya oluşturun:** `test.olang`

**2. Kod yazın:**
```olang
int x = 10;
int y = 20;

func topla(int a, int b) {
    return a + b;
}

int sonuc = topla(x, y);
```

**3. Çalıştırın:**
```bash
wsl ./olang test.olang
```

## 📝 Temel Söz Dizimi

```olang
// Değişkenler
int sayi = 5;
float ondalik = 3.14;
str metin = "Merhaba";
bool dogru = true;

// Print (Ekrana yazdır)
print("Merhaba OLang!");
print("Sayı:", sayi);

// Input (Kullanıcıdan al)
str isim = input("Adınız: ");
int yas = inputInt("Yaşınız: ");
print("Merhaba", isim);

// Fonksiyon
func kare(int n) {
    return n * n;
}

// If/Else
if (sayi > 3) {
    int x = 10;
} else {
    int x = 5;
}

// While Döngüsü
int i = 0;
while (i < 10) {
    i = i + 1;
}

// For Döngüsü
for (int j = 0; j < 10; j = j + 1) {
    print("j =", j);
}

// Foreach Döngüsü
for (k in range(10)) {
    print("k =", k);
}
```

## 🎯 Komutlar

| Komut | Açıklama |
|-------|----------|
| `wsl ./olang dosya.olang` | OLang dosyası çalıştır |
| `wsl ./olang` | Demo kodu çalıştır |
| `wsl bash build.sh` | Projeyi derle |

## 📚 Daha Fazlası

- Detaylı kullanım: `KULLANIM.md`
- Tam döküman: `README.md`
- Örnek kodlar: `examples/` klasörü

---
**OLang ile mutlu kodlamalar!** 💻✨


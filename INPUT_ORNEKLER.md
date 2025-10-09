# OLang Input/Output Örnekleri 🎮

OLang artık **kullanıcıdan input alabiliyor** ve **ekrana çıktı verebiliyor**! 🎉

## 🔧 Built-in Fonksiyonlar

| Fonksiyon | Açıklama | Örnek |
|-----------|----------|-------|
| `print(...)` | Ekrana değer yazdırır | `print("Merhaba", x)` |
| `input("prompt")` | Kullanıcıdan string okur | `str isim = input("İsim: ")` |
| `inputInt("prompt")` | Kullanıcıdan integer okur | `int yas = inputInt("Yaş: ")` |
| `inputFloat("prompt")` | Kullanıcıdan float okur | `float boy = inputFloat("Boy: ")` |

## 📚 Örnek Dosyalar

### 1. Print Testi
**Dosya:** `examples/print_test.olang`
```olang
int x = 10;
print("Merhaba OLang!");
print("x =", x);
print("Toplam:", x + 20);
```

**Çalıştırma:**
```bash
wsl ./olang examples/print_test.olang
```

---

### 2. İnteraktif Program
**Dosya:** `examples/interactive.olang`
```olang
str isim = input("Adiniz: ");
print("Merhaba", isim);

int yas = inputInt("Yasiniz: ");
int dogum_yili = 2025 - yas;
print("Dogum yiliniz yaklasik:", dogum_yili);
```

**Çalıştırma:**
```bash
wsl ./olang examples/interactive.olang
```

**Örnek Çıktı:**
```
OLang calistiriliyor: examples/interactive.olang

Adiniz: Ahmet
"Merhaba" "Ahmet"
Yasiniz: 25
"Dogum yiliniz yaklasik:" 2000
...
```

---

### 3. İnteraktif Hesap Makinesi
**Dosya:** `examples/calculator_interactive.olang`
```olang
print("=== OLang Hesap Makinesi ===");

int a = inputInt("Birinci sayi: ");
int b = inputInt("Ikinci sayi: ");

int toplam = a + b;
int carpim = a * b;

print("Toplam:", toplam);
print("Carpim:", carpim);
```

**Çalıştırma:**
```bash
wsl ./olang examples/calculator_interactive.olang
```

**Örnek Çıktı:**
```
OLang calistiriliyor: examples/calculator_interactive.olang

"=== OLang Hesap Makinesi ==="
Birinci sayi: 10
Ikinci sayi: 5
"Toplam:" 15
"Carpim:" 50
...
```

---

### 4. Sayı Tahmin Oyunu
**Dosya:** `examples/number_game.olang`
```olang
print("=== Sayi Tahmin Oyunu ===");
print("1 ile 10 arasinda bir sayi tuttum!");

int gizli_sayi = 7;
int tahmin = inputInt("Tahmininiz: ");

if (tahmin == gizli_sayi) {
    print("Tebrikler! Dogru bildiniz!");
} else {
    print("Yanlis! Dogru cevap:", gizli_sayi);
}
```

**Çalıştırma:**
```bash
wsl ./olang examples/number_game.olang
```

---

## 🎯 Kendi Programınızı Yazın

### Örnek: Kullanıcı Bilgileri
`user_info.olang` dosyası oluşturun:
```olang
print("=== Kullanici Bilgileri ===");

str isim = input("Isminiz: ");
str sehir = input("Sehir: ");
int yas = inputInt("Yas: ");

print("-----");
print("Isim:", isim);
print("Sehir:", sehir);
print("Yas:", yas);
print("-----");
```

Çalıştırın:
```bash
wsl ./olang user_info.olang
```

---

## 💡 İpuçları

1. **Print birden fazla argüman alabilir:**
   ```olang
   print("x =", x, "y =", y);
   ```

2. **Input her zaman prompt ile kullanılır:**
   ```olang
   str ad = input("Ad: ");  // ✓ Doğru
   str ad = input();        // ✗ Çalışır ama prompt yok
   ```

3. **Tip dönüşümü yok, doğru tip kullanın:**
   ```olang
   int sayi = inputInt("Sayi: ");   // ✓ Integer için
   str metin = input("Metin: ");    // ✓ String için
   ```

4. **Print statement değil, expression:**
   ```olang
   print("Toplam:", 5 + 3);  // ✓ İşlemler doğrudan yapılabilir
   ```

---

## 🚀 Test Komutları

```bash
# Tüm input örneklerini test et
wsl ./olang examples/interactive.olang
wsl ./olang examples/calculator_interactive.olang
wsl ./olang examples/number_game.olang

# Print testini çalıştır (input yok)
wsl ./olang examples/print_test.olang
```

---

**Artık OLang ile tam interaktif programlar yazabilirsiniz!** 🎮✨


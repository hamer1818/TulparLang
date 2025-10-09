# OLang Döngüler Rehberi 🔄

OLang **üç farklı döngü tipi** destekler: while, for ve foreach!

## 🔄 Döngü Tipleri

### 1. WHILE Döngüsü

**Syntax:**
```olang
while (koşul) {
    // kod
}
```

**Örnek:**
```olang
int i = 0;
while (i < 5) {
    print("i =", i);
    i = i + 1;
}
```

**Çıktı:**
```
i = 0
i = 1
i = 2
i = 3
i = 4
```

---

### 2. FOR Döngüsü (C-style)

**Syntax:**
```olang
for (başlangıç; koşul; artırma) {
    // kod
}
```

**Örnek:**
```olang
for (int i = 0; i < 5; i = i + 1) {
    print("i =", i);
}
```

**Çıktı:**
```
i = 0
i = 1
i = 2
i = 3
i = 4
```

---

### 3. FOREACH Döngüsü (for..in)

**Syntax:**
```olang
for (değişken in iterable) {
    // kod
}
```

**Örnek:**
```olang
for (i in range(5)) {
    print("i =", i);
}
```

**Çıktı:**
```
i = 0
i = 1
i = 2
i = 3
i = 4
```

---

## 📚 Detaylı Örnekler

### Örnek 1: Toplam Hesaplama (FOR)

```olang
int toplam = 0;
for (int i = 1; i <= 10; i = i + 1) {
    toplam = toplam + i;
}
print("1'den 10'a toplam:", toplam);  // 55
```

### Örnek 2: Çarpım Tablosu (FOREACH)

```olang
for (i in range(10)) {
    int sonuc = i * 7;
    print(i, "x 7 =", sonuc);
}
```

### Örnek 3: İç İçe Döngüler

```olang
// Çarpım tablosu matrisi
for (int x = 1; x <= 5; x = x + 1) {
    for (int y = 1; y <= 5; y = y + 1) {
        int sonuc = x * y;
        print(x, "x", y, "=", sonuc);
    }
}
```

### Örnek 4: While ile Fibonacci

```olang
int a = 0;
int b = 1;
int sayac = 0;

while (sayac < 10) {
    print(a);
    int temp = a + b;
    a = b;
    b = temp;
    sayac = sayac + 1;
}
```

---

## 🎯 range() Fonksiyonu

`range(n)` fonksiyonu foreach döngülerinde kullanılır.

**Kullanım:**
```olang
range(5)   // 0, 1, 2, 3, 4
range(10)  // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9
```

**Örnek:**
```olang
for (i in range(5)) {
    int kare = i * i;
    print(i, "karesi =", kare);
}
```

---

## 🔄 Döngü Karşılaştırması

| Döngü Tipi | Ne Zaman Kullanılır | Örnek |
|------------|---------------------|-------|
| **while** | Koşul bilindiğinde | `while (x < 100)` |
| **for** | Sayaç gerektiğinde | `for (int i = 0; i < 10; i = i + 1)` |
| **foreach** | Liste/range üzerinde | `for (i in range(10))` |

---

## 💡 İpuçları

1. **FOR döngüsünde artırma:**
   ```olang
   for (int i = 0; i < 10; i = i + 2)  // 2'şer atlar
   ```

2. **FOREACH her zaman 0'dan başlar:**
   ```olang
   for (i in range(10))  // i: 0, 1, 2, ..., 9
   ```

3. **İç içe döngüler:**
   ```olang
   for (int x = 0; x < 3; x = x + 1) {
       for (int y = 0; y < 3; y = y + 1) {
           print(x, y);
       }
   }
   ```

4. **Break yok (henüz):**
   - Döngüden çıkmak için koşulu kullanın
   - Veya fonksiyon içinde `return` kullanın

---

## 🚀 Test Dosyaları

```bash
# Tüm döngü tipleri
wsl ./olang examples/loops.olang

# FOR döngüsü
wsl ./olang examples/for_sum.olang

# FOREACH döngüsü
wsl ./olang examples/foreach_demo.olang
```

---

**OLang ile döngüler artık çok kolay!** 🔄✨


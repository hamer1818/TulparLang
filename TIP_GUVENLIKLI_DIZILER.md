# Tip Güvenlikli Diziler (Type-Safe Arrays)

## 🎯 Genel Bakış

OLang, hem **karışık tipli** hem de **tip güvenlikli** dizileri destekler!

## 📊 Dizi Tipleri

### 1. Mixed Type Array (Karışık Tip)
```olang
array karma = [1, "Ali", 3.14, true];
```
- ✅ Herhangi bir tip kabul eder
- ✅ Esnek kullanım
- ⚠️ Tip güvenliği yok

### 2. Type-Safe Arrays (Tip Güvenlikli)

#### arrayInt - Sadece Integer
```olang
arrayInt sayilar = [1, 2, 3, 4, 5];
push(sayilar, 6);       // ✅ OK
push(sayilar, "hata");  // ❌ HATA!
```

#### arrayFloat - Sadece Float
```olang
arrayFloat floats = [1.5, 2.5, 3.14];
push(floats, 4.5);      // ✅ OK
push(floats, 10);       // ❌ HATA!
```

#### arrayStr - Sadece String
```olang
arrayStr isimler = ["Ali", "Veli", "Ayşe"];
push(isimler, "Fatma"); // ✅ OK
push(isimler, 123);     // ❌ HATA!
```

#### arrayBool - Sadece Boolean
```olang
arrayBool flags = [true, false, true];
push(flags, false);     // ✅ OK
push(flags, 1);         // ❌ HATA!
```

## 🛡️ Tip Güvenliği

### Tanımlama Zamanı Kontrolü
```olang
arrayInt sayilar = [1, 2, 3];      // ✅ OK
arrayInt hatali = [1, "Ali", 3];   // ❌ HATA! Tüm elemanlar int olmalı
```

### Push Zamanı Kontrolü
```olang
arrayInt sayilar = [1, 2, 3];
push(sayilar, 4);       // ✅ OK
push(sayilar, "hata");  // ❌ HATA: Dizi sadece int tipinde eleman kabul eder!
```

### Set Zamanı Kontrolü
```olang
arrayInt sayilar = [1, 2, 3];
sayilar[0] = 10;        // ✅ OK
sayilar[0] = "hata";    // ❌ HATA: Dizi sadece int tipinde eleman kabul eder!
```

## 📝 Örnekler

### Örnek 1: Notlar Sistemi
```olang
arrayInt notlar = [85, 90, 78, 92, 88];

int toplam = 0;
for (int i = 0; i < length(notlar); i++) {
    toplam += notlar[i];
}

int ortalama = toplam / length(notlar);
print("Ortalama:", ortalama);
```

### Örnek 2: İsim Listesi
```olang
arrayStr ogrenciler = ["Ahmet", "Mehmet", "Ayşe"];

print("Öğrenci Listesi:");
for (int i = 0; i < length(ogrenciler); i++) {
    print(i + 1, "-", ogrenciler[i]);
}

// Yeni öğrenci ekle
str yeni = input("Yeni öğrenci adı: ");
push(ogrenciler, yeni);
```

### Örnek 3: Flag Sistemi
```olang
arrayBool durumlar = [true, false, true, true];

int aktifSayisi = 0;
for (int i = 0; i < length(durumlar); i++) {
    if (durumlar[i]) {
        aktifSayisi++;
    }
}

print("Aktif sayısı:", aktifSayisi);
```

## 🆚 Karşılaştırma

| Özellik | `array` | `arrayInt/arrayStr/etc` |
|---------|---------|-------------------------|
| Karışık tip | ✅ Evet | ❌ Hayır |
| Tip güvenliği | ❌ Yok | ✅ Var |
| Hata kontrolü | Runtime | Compile-time değil, runtime |
| Kullanım | Genel amaçlı | Spesifik tip |
| Performans | Normal | Aynı |

## 💡 Ne Zaman Hangi Tip?

### `array` Kullan:
- Farklı tipleri saklamak istediğinde
- Esneklik gerektiğinde
- JSON benzeri veri yapıları için

### `arrayInt/arrayStr/etc` Kullan:
- Tek tip veri saklayacaksan
- Tip güvenliği istediğinde
- Hata önlemek istediğinde
- Matematiksel işlemler yapacaksan (arrayInt, arrayFloat)

## 🎓 Best Practices

1. **Mümkünse tipli array kullan** - Hataları önler
2. **Mixed array sadece gerektiğinde** - Esneklik gerektiğinde
3. **Tip kontrolü yap** - `push()` ve `set()` kullanırken dikkatli ol
4. **İsimlendirme** - Değişken isimlerini açık seç

## ✨ Avantajlar

- ✅ **Tip Güvenliği**: Yanlış tip atamalarını engeller
- ✅ **Okunabilirlik**: Kodun amacı daha net
- ✅ **Hata Önleme**: Runtime hataları erken yakalar
- ✅ **Bakım Kolaylığı**: Kodun ne beklediği açık

---

**OLang Versiyonu**: 1.2.1 (Typed Arrays)  
**Tarih**: 9 Ekim 2025


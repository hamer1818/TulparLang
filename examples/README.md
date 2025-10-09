# 📚 OLang Örnek Dosyalar Rehberi

Bu klasörde OLang dilinin tüm özelliklerini gösteren kapsamlı örnekler bulunmaktadır.

## 📖 Öğrenme Sırası (Başlangıç → İleri)

### 🎯 Temel Seviye

| # | Dosya | Konu | Süre |
|---|-------|------|------|
| 01 | `01_hello_world.olang` | Değişkenler, Tipler, Operatörler | 5 dk |
| 02 | `02_control_flow.olang` | If/Else, Koşullar, Mantıksal İfadeler | 5 dk |
| 03 | `03_loops.olang` | While, For, For..in, Break, Continue | 7 dk |
| 04 | `04_functions.olang` | Fonksiyonlar, Parametreler, Return | 8 dk |

### 🚀 Orta Seviye

| # | Dosya | Konu | Süre |
|---|-------|------|------|
| 05 | `05_arrays.olang` | Diziler, Array İşlemleri, Push/Pop | 10 dk |
| 06 | `06_interactive.olang` | Kullanıcı Girişi, Input Fonksiyonları | 7 dk |
| 07 | `07_number_game.olang` | Oyun Örneği, Döngüler + Koşullar | 5 dk |
| 08 | `08_calculator.olang` | Gelişmiş Hesap Makinesi, Çoklu İşlemler | 10 dk |

### 🔥 İleri Seviye

| # | Dosya | Konu | Süre |
|---|-------|------|------|
| 09 | `09_advanced_functions.olang` | Rekursif Fonksiyonlar, Algoritmalar | 15 dk |
| 10 | `10_test_phase1.olang` | Faz 1 Özellikleri (&&, ||, !, ++, --, +=) | 8 dk |
| 11 | `11_test_phase2.olang` | Faz 2 Özellikleri (Tipli Diziler) | 10 dk |
| 12 | `12_calculator_interactive.olang` | Basit İnteraktif Hesap Makinesi | 3 dk |
| 13 | `13_json_arrays.olang` | JSON-Like Arrays (arrayJson) ✨ | 8 dk |

---

## 🎓 Kategorilere Göre Örnekler

### 📊 Veri Tipleri & Değişkenler
- **01_hello_world.olang** - Tüm veri tipleri (int, float, str, bool)
- **05_arrays.olang** - Diziler (array, arrayInt, arrayStr, etc.)

### 🔀 Kontrol Akışı
- **02_control_flow.olang** - If/Else, İç içe koşullar
- **03_loops.olang** - While, For, For..in, Break, Continue

### 🧩 Fonksiyonlar
- **04_functions.olang** - Temel fonksiyonlar, parametre, return
- **09_advanced_functions.olang** - Rekursif fonksiyonlar, algoritmalar

### 📦 Diziler (Arrays) - Faz 2
- **05_arrays.olang** - Tüm array işlemleri
- **11_test_phase2.olang** - Tip güvenlikli diziler testi
- **13_json_arrays.olang** - JSON-like diziler (arrayJson) ✨ YENİ!

### 💻 İnteraktif Programlar
- **06_interactive.olang** - Kullanıcı girişi, input()
- **07_number_game.olang** - Sayı tahmin oyunu
- **08_calculator.olang** - Gelişmiş hesap makinesi
- **12_calculator_interactive.olang** - Basit hesap makinesi

### 🧪 Test & Demo
- **10_test_phase1.olang** - Faz 1 özellikleri testi
- **11_test_phase2.olang** - Faz 2 özellikleri testi

---

## 🏃 Hızlı Başlangıç

### 1️⃣ İlk Programınızı Çalıştırın
```bash
./olang examples/01_hello_world.olang
```

### 2️⃣ İnteraktif Program Deneyin
```bash
./olang examples/06_interactive.olang
```

### 3️⃣ Oyun Oynayın
```bash
./olang examples/07_number_game.olang
```

---

## 📋 Özellik Referansı

### Faz 1 Özellikleri ✅
- **Mantıksal Operatörler**: `&&` (AND), `||` (OR), `!` (NOT)
- **Increment/Decrement**: `++`, `--`
- **Compound Assignment**: `+=`, `-=`, `*=`, `/=`
- **Break & Continue**: Döngü kontrolü
- **Tip Dönüşümleri**: `toInt()`, `toFloat()`, `toString()`, `toBool()`

**Örnek**: `10_test_phase1.olang`

### Faz 2 Özellikleri ✅
- **Karışık Diziler**: `array` - Herhangi bir tip
- **Tip Güvenlikli Diziler**: 
  - `arrayInt` - Sadece integer
  - `arrayFloat` - Sadece float
  - `arrayStr` - Sadece string
  - `arrayBool` - Sadece boolean
- **JSON-Like Diziler**: `arrayJson` - JSON formatı ✨ YENİ!
- **Dizi İşlemleri**: `push()`, `pop()`, `length()`

**Örnek**: `11_test_phase2.olang`, `13_json_arrays.olang`

---

## 🎯 Hangi Örneği Seçmeliyim?

### Yeni başlıyorsanız:
👉 `01_hello_world.olang` ile başlayın, sırayla ilerleyin

### Belirli bir özelliği öğrenmek istiyorsanız:

| Öğrenmek İstediğiniz | Dosya |
|---------------------|-------|
| Değişkenler ve tipler | 01_hello_world.olang |
| If/Else yapısı | 02_control_flow.olang |
| Döngüler | 03_loops.olang |
| Fonksiyonlar | 04_functions.olang |
| Diziler | 05_arrays.olang |
| Kullanıcı girişi | 06_interactive.olang |
| Rekursif fonksiyonlar | 09_advanced_functions.olang |
| Tipli diziler | 11_test_phase2.olang |
| JSON dizileri | 13_json_arrays.olang |

### Hızlı test yapmak istiyorsanız:
👉 `10_test_phase1.olang` ve `11_test_phase2.olang`

### Eğlenceli bir şey denemek istiyorsanız:
👉 `07_number_game.olang`

---

## 💡 İpuçları

1. **Sırayla ilerleyin**: Dosyalar 01'den 12'ye doğru zorluk sırasına göre düzenlenmiştir
2. **Kodları inceleyin**: Her dosya detaylı yorumlar içerir
3. **Kendiniz deneyin**: Örnekleri değiştirerek öğrenin
4. **Hata mesajlarını okuyun**: OLang açıklayıcı hata mesajları verir

---

## 📞 Yardım

- **Detaylı dokümantasyon**: `../README.md`
- **Hızlı başlangıç**: `../QUICKSTART.md`
- **Tip güvenlikli diziler**: `../TIP_GUVENLIKLI_DIZILER.md`
- **Gelecek özellikler**: `../GELECEK_OZELLIKLER.md`

---

**OLang Versiyonu**: 1.2.2 (arrayJson Support)  
**Örnek Sayısı**: 13  
**Toplam Satır**: ~2200+  
**Güncellenme**: 9 Ekim 2025

---

**İyi kodlamalar!** 🚀


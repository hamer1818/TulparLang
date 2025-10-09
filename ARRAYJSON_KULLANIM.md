# arrayJson - JSON-Like Diziler 📊

## 🎯 Genel Bakış

`arrayJson`, JSON formatına benzer veri yapıları oluşturmanıza olanak sağlayan özel bir dizi tipidir.

## 🆚 array vs arrayJson

| Özellik | `array` | `arrayJson` |
|---------|---------|-------------|
| Karışık tip | ✅ | ✅ |
| Amaç | Genel kullanım | JSON-like veri |
| Nested | ✅ | ✅ |
| Açıklayıcılık | Normal | Yüksek |
| Kullanım alanı | Her yerde | API, Config, Data |

## 📝 Temel Kullanım

### 1. Basit JSON Array

```olang
// Kullanıcı verisi
arrayJson kullanici = ["Ali", 25, true, "Mühendis"];

// Erişim
str isim = kullanici[0];      // "Ali"
int yas = kullanici[1];        // 25
bool aktif = kullanici[2];     // true
str meslek = kullanici[3];     // "Mühendis"
```

### 2. API Response

```olang
// HTTP Response benzeri
arrayJson response = [
    200,           // Status code
    "Success",     // Message
    true,          // Success flag
    "2025-10-09"  // Timestamp
];

int statusCode = response[0];
str message = response[1];
```

### 3. Config Data

```olang
// Uygulama ayarları
arrayJson config = [
    "OLang",       // App name
    "1.2.1",       // Version
    true,          // Debug mode
    8080,          // Port
    "localhost"    // Host
];
```

## 🔄 İç İçe Diziler (Nested Arrays)

```olang
// Kullanıcı listesi
arrayJson user1 = ["ahmet", "ahmet@mail.com", 25];
arrayJson user2 = ["mehmet", "mehmet@mail.com", 30];
arrayJson user3 = ["ayse", "ayse@mail.com", 28];

arrayJson users = [];
push(users, user1);
push(users, user2);
push(users, user3);

// Sonuç: [["ahmet", "ahmet@mail.com", 25], ["mehmet", ...], ...]
```

## 📊 Gerçek Dünya Örnekleri

### E-Ticaret: Ürün Verisi

```olang
// [ID, İsim, Fiyat, Stok, Aktif]
arrayJson urun1 = [101, "Laptop", 5000, 10, true];
arrayJson urun2 = [102, "Mouse", 150, 50, true];
arrayJson urun3 = [103, "Keyboard", 300, 30, false];

// Ürün bilgileri
int id = urun1[0];          // 101
str isim = urun1[1];        // "Laptop"
int fiyat = urun1[2];       // 5000
int stok = urun1[3];        // 10
bool aktif = urun1[4];      // true
```

### İstatistik: Aylık Veriler

```olang
// [Ay, Satış, Kar, Aktif]
arrayJson ocak = ["Ocak", 10000, 2000, true];
arrayJson subat = ["Şubat", 15000, 3000, true];
arrayJson mart = ["Mart", 12000, 2400, false];

// Toplam hesaplama
int toplamSatis = ocak[1] + subat[1] + mart[1];  // 37000
int toplamKar = ocak[2] + subat[2] + mart[2];    // 7400
```

### Geolocation: Konum Verileri

```olang
// [Şehir, Enlem, Boylam, Nüfus]
arrayJson istanbul = ["İstanbul", 41.0, 29.0, 15];
arrayJson ankara = ["Ankara", 39.9, 32.8, 5];
arrayJson izmir = ["İzmir", 38.4, 27.1, 4];

str sehir = istanbul[0];     // "İstanbul"
float lat = istanbul[1];     // 41.0
float lng = istanbul[2];     // 29.0
int nufus = istanbul[3];     // 15
```

### Event Log: Olay Kaydı

```olang
// [ID, Event, Timestamp, User, Success]
arrayJson event1 = [1, "login", "10:30:00", "Ali", true];
arrayJson event2 = [2, "logout", "11:45:00", "Ali", true];
arrayJson event3 = [3, "error", "12:00:00", "System", false];

// Başarılı olay filtresi
int basarili = 0;
if (event1[4]) { basarili++; }
if (event2[4]) { basarili++; }
if (event3[4]) { basarili++; }
```

### Skor Tablosu

```olang
// [Oyuncu, Puan, Seviye, Aktif]
arrayJson player1 = ["Ali", 1500, 10, true];
arrayJson player2 = ["Veli", 2000, 15, true];
arrayJson player3 = ["Ayşe", 1800, 12, false];

// En yüksek puanı bul
int maxPuan = player1[1];
str champion = player1[0];

if (player2[1] > maxPuan) {
    maxPuan = player2[1];
    champion = player2[0];
}
```

## 🎨 Best Practices

### ✅ Doğru Kullanım

```olang
// Açıklayıcı: JSON-like veri için arrayJson kullan
arrayJson kullanici = ["Ali", 25, true];
arrayJson apiData = [200, "OK", true];
arrayJson config = ["app", "1.0", 8080];
```

### ❌ Yanlış Kullanım

```olang
// Sadece tek tip varsa typed array kullan
arrayJson sayilar = [1, 2, 3, 4, 5];  // ❌ arrayInt kullan
arrayJson isimler = ["Ali", "Veli"];   // ❌ arrayStr kullan
```

## 📋 Kullanım Senaryoları

| Senaryo | arrayJson Kullanımı |
|---------|---------------------|
| API Response | ✅ Mükemmel |
| Config/Settings | ✅ Mükemmel |
| Database Row | ✅ İyi |
| User Profile | ✅ Mükemmel |
| Product Data | ✅ İyi |
| Event Logging | ✅ İyi |
| Matematik işlemleri | ❌ arrayInt/arrayFloat kullan |
| Sadece string listesi | ❌ arrayStr kullan |

## 💡 İpuçları

1. **Açıklayıcı değişken isimleri kullanın**
   ```olang
   arrayJson userProfile = ["Ali", 25, "dev"];  // ✅ İyi
   arrayJson data = ["Ali", 25, "dev"];         // ⚠️ Az açıklayıcı
   ```

2. **Index anlamını yorumlayın**
   ```olang
   // Yorumla açıkla
   arrayJson product = [
       101,        // ID
       "Laptop",   // Name
       5000,       // Price
       10          // Stock
   ];
   ```

3. **Nested kullanımda dikkatli olun**
   ```olang
   // OK: 1-2 seviye
   arrayJson nested = [["user1", 25], ["user2", 30]];
   
   // Dikkat: Çok derin nested karmaşık olabilir
   ```

4. **Tip kontrolü yapın**
   ```olang
   arrayJson response = [200, "OK"];
   
   // Doğru tip ile erişin
   int status = response[0];    // ✅ int
   str message = response[1];   // ✅ str
   ```

## 🆚 Ne Zaman Hangi Array Tipi?

```olang
// Tek tip veri → Typed array
arrayInt notlar = [85, 90, 78];           // ✅
arrayStr isimler = ["Ali", "Veli"];       // ✅

// JSON-like veri → arrayJson
arrayJson kullanici = ["Ali", 25, true];  // ✅
arrayJson config = ["app", 1.0, 8080];    // ✅

// Karma genel veri → array
array karma = [1, "test", 3.14, true];    // ✅
```

## 📚 Tam Örnek

```olang
// E-ticaret sistemi
arrayJson urun = [101, "Laptop", 5000, true];
arrayJson kullanici = ["Ali", "ali@mail.com", 25];
arrayJson siparis = [
    urun[0],        // Ürün ID
    kullanici[0],   // Kullanıcı adı
    2,              // Adet
    10000           // Toplam
];

print("Sipariş Detayları:");
print("Ürün:", urun[1]);
print("Müşteri:", kullanici[0]);
print("Email:", kullanici[1]);
print("Adet:", siparis[2]);
print("Toplam:", siparis[3], "TL");
```

## 🎯 Özet

- ✅ JSON-like veri yapıları için ideal
- ✅ API response, config, user data için mükemmel
- ✅ İç içe diziler destekleniyor
- ✅ Okunabilir ve açıklayıcı
- ✅ Karışık tip desteği
- ⚠️ Tek tip veri için typed array tercih edin
- ⚠️ Çok derin nested yapılardan kaçının

---

**OLang Versiyonu**: 1.2.2 (arrayJson Support)  
**Tarih**: 9 Ekim 2025


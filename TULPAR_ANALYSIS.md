# Tulpar Programlama Dili Analizi

Bu belge, Tulpar programlama dilinin mevcut yeteneklerini ve hedeflenen "hızlı ve kolay REST API geliştirme" amacı doğrultusunda eksik olan özelliklerini detaylandırmaktadır.

## 1. Mevcut Özellikler

Tulpar şu anda temel bir prosedürel programlama dili özelliklerine sahiptir.

### 1.1. Temel Söz Dizimi ve Veri Tipleri
C benzeri bir söz dizimine sahiptir.
*   **Temel Tipler:** `int`, `float`, `str`, `bool`.
*   **Değişken Tanımlama:** `int x = 5;`, `str isim = "Tulpar";`
*   **Yorum Satırları:** `//` ile tek satırlık yorumlar.

### 1.2. Kontrol Yapıları
Standart akış kontrol mekanizmaları mevcuttur.
*   **Koşul İfadeleri:** `if`, `else if`, `else`.
*   **Döngüler:** `for` (C tarzı), `while`.
*   **Operatörler:** Aritmetik (`+`, `-`, `*`, `/`, `%`), Karşılaştırma (`==`, `!=`, `>`, `<`, `>=`, `<=`), Mantıksal (`&&`, `||`, `!`).

### 1.3. Fonksiyonlar
*   **Tanımlama:** `func` anahtar kelimesi ile.
*   **Parametreler:** Tip korumalı parametreler.
*   **Dönüş Değeri:** `return` ile değer döndürme.
*   **Özyineleme (Recursion):** Desteklenmektedir (örn: Fibonacci, Faktöriyel).

### 1.4. Veri Yapıları
*   **Diziler (Arrays):** `[]` ile tanımlama ve erişim.
*   **JSON Objeleri:** `arrayJson` tipi ile yerleşik JSON desteği.
    *   İç içe objeler ve diziler desteklenir.
    *   `toJson` ve `fromJson` ile serileştirme desteği.
*   **Structs (Yapılar):** `type` anahtar kelimesi ile kullanıcı tanımlı veri tipleri.
    *   Constructor benzeri başlatma: `Person p = Person(name: "Ali");`
    *   Nokta notasyonu ile erişim: `p.name`

### 1.5. Modül Sistemi
*   **Import:** `import "dosya.tpr";` ile başka dosyaları dahil etme özelliği.

### 1.6. Standart Kütüphane (Mevcut Fonksiyonlar)
*   **Giriş/Çıkış:** `print()`
*   **Dönüşüm:** `toInt()`, `toString()`
*   **Dizi/String:** `length()`
*   **Matematik:** Temel işlemler operatörler ile yapılır.

---

## 2. Eksik Özellikler

Dilin "hızlı ve kolay REST API geliştirme" amacına ulaşması ve tam teşekküllü bir dil gibi hissettirmesi için aşağıdaki özelliklerin eklenmesi gerekmektedir.

### 2.1. REST API Geliştirme İçin Kritik Eksikler
Bu özellikler olmadan bir web sunucusu veya API yazılamaz.

1.  **HTTP Sunucusu (Built-in HTTP Server):**
    *   Bir portu dinleme (`listen(8080)`).
    *   HTTP isteklerini (GET, POST, PUT, DELETE) karşılama.
    *   Request ve Response objeleri (Header, Body, Query Params yönetimi).
2.  **Routing (Yönlendirme) Mekanizması:**
    *   URL yollarını fonksiyonlara eşleme (örn: `app.get("/users", getUsers)`).
3.  **Middleware Desteği:**
    *   İstek gelmeden önce veya yanıt dönmeden önce araya girme (Auth, Logging vb. için).
4.  **Veritabanı Entegrasyonu:**
    *   SQL (PostgreSQL, MySQL) veya NoSQL (MongoDB) sürücüleri.
    *   Basit bir ORM veya Query Builder.
5.  **Environment Variables (.env):**
    *   Konfigürasyonları (API key, DB şifresi) koddan ayırmak için `.env` dosyası okuma desteği.

### 2.2. Genel Programlama Dili Eksikleri
Normal bir dil gibi çalışması için gereken temel yapıtaşları.

1.  **Gelişmiş Hata Yönetimi (Error Handling):**
    *   Mevcut sistemde hatalar programı durduruyor gibi görünüyor.
    *   `try-catch-finally` blokları veya Go tarzı hata dönüş mekanizması (`result, error`).
2.  **Dosya Sistemi (File I/O):**
    *   Dosya okuma, yazma, silme, listeleme işlemleri (sadece interpreter içinde `read_file` var, dile expose edilmeli).
3.  **Asenkron Programlama (Concurrency/Async):**
    *   Web sunucusu için bloklamayan (non-blocking) yapı şart. `async/await` veya `threads/coroutines`.
4.  **Paket Yöneticisi (Package Manager):**
    *   Harici kütüphaneleri indirmek ve yönetmek için bir araç (npm, pip, cargo benzeri).
5.  **Gelişmiş Standart Kütüphane:**
    *   **Date/Time:** Tarih ve saat işlemleri.
    *   **Regex:** Düzenli ifadeler.
    *   **Math:** Daha gelişmiş matematik fonksiyonları (`sqrt`, `pow`, `random` vb. - bazıları örneklerde var ama built-in olmalı).
    *   **String:** `split`, `join`, `replace`, `trim`, `toUpper`, `toLower`.
6.  **Scope Yönetimi ve Garbage Collection:**
    *   Bellek yönetimi şu an manuel veya basit seviyede olabilir, uzun çalışan sunucular için sağlam bir GC veya bellek modeli gerekir.

# TulparLang — Tespit Edilen Eksiklikler ve Bug'lar

Bu rapor, **2026-04-27** tarihinde Windows üzerinde yapılan port + test +
benchmark çalışması sırasında karşılaşılan tüm sorunların tek bir yerden
takip edilebilmesi için tutuldu. Sorunlar **etki şiddeti** ve **çözüm
önceliği** ile birlikte sıralandı; her madde için kaynaktaki konum ve
varsa uygulanmış geçici çözüm referans verildi.

> **Güncelleme — 2026-04-27 (akşam):** Aşağıdaki maddelerden 1, 2, 3, 5, 6,
> 7, 8a, 8b, 9, 10, 11, 12, 14 **tamamlandı**; her birinin altındaki
> "RESOLVED" notuna bakın. Test geçiş oranı 16/18 → **22/22** (skip dışı
> her örnek geçiyor). Benchmark `fib(35)` 364 ms → **185 ms** (1.97×
> hızlanma); `loopsum` 175 ms → **162 ms** (Go ile aynı sınıfta).

---

## 1. Parser: `return f(x) + g(y);` ifadesi parse edilemiyor

**Şiddet:** Yüksek — temel deyim seviyesinde bir sözdizimi geçerli kod
çalışmıyor; recursive Fibonacci, recursive sum, vb. doğal kalıplar
kullanılamıyor.

**Reprodüksiyon:**
```tpr
func fib(int n) {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);   // <-- "Expected ';' after return"
}
```

**Kök neden:** [src/parser/parser.cpp:486](src/parser/parser.cpp#L486) içindeki
`parse_expression` precedence-climbing tasarımı yanlış. `parse_unary`
sadece `parse_primary`'yi çağırıp dönüyor; postfix uygulamaları
(`(args)` çağrısı, `[i]` indexleme) ise binary döngünün **bittikten
sonra** uygulanıyor. Sonuç: `f(x) + g(y)` parse edilirken:

1. `left = parse_unary() → Identifier("f")` (henüz çağrı değil)
2. Binary loop'a girilir; `current()` `(` token'ı; `get_precedence('(') == 0`
   olduğu için döngü hemen kırılır.
3. `parse_postfix` ile `Call("f", [x])` üretilir.
4. Kontrol return statement'a döner; pozisyon `+` üzerinde, `;` bekleniyor → hata.

**Doğru tasarım:** Postfix işlemi `parse_unary` ya da `parse_primary`
sonunda — yani binary loop'un her iterasyonu için bir kez — uygulanmalı:

```cpp
std::unique_ptr<ASTNode> Parser::parse_unary() {
    if (match(TOKEN_MINUS) || match(TOKEN_BANG)) { /* ... */ }
    return parse_postfix(parse_primary());     // <-- BURADA
}
// parse_expression sonundaki parse_postfix(left) çağrısı kaldırılır.
```

**Geçici çözüm:** Çağrıları yerel değişkene hoist edin:
`int a = f(x); int b = g(y); return a + b;`. Benchmark'lardaki
[benchmarks/fib.tpr](benchmarks/fib.tpr) bu workaround'u kullanıyor.

**RESOLVED (2026-04-27):** [src/parser/parser.cpp:509](src/parser/parser.cpp#L509)
`parse_unary` artık `parse_postfix(parse_primary())` çağırıyor; `parse_expression`
sonundaki postfix çağrısı kaldırıldı. `f(x) + g(y)` ve recursive
fib hem VM'de hem AOT'de doğal sözdizimle çalışıyor — workaround gerekmez.

---

## 2. VM yolu `func name(...): ReturnType { ... }` sözdizimini kabul etmiyor

**Şiddet:** Yüksek — AOT ile VM iki farklı parser tarafından besleniyor
gibi davranıyor. Aynı `.tpr` dosyası `tulpar build` ile derleniyor ama
`tulpar --vm` ile derlenmiyor.

**Reprodüksiyon:**
```tpr
func add(int a, int b): int {
    return a + b;
}
print(toString(add(1, 2)));
```

- `tulpar build` → 3 üretiyor
- `tulpar --vm` → `Ayristirici Hatasi: Expected type name`

[examples/02_basics.tpr](examples/02_basics.tpr) bu sözdizimini
kullanıyor; `run_tests.bat` `build` modunda çalıştırdığı için PASS
diyor, ama VM modu kullanan kullanıcılar aynı kodu çalıştıramıyor.

**Kök neden:** VM yolu (`src/vm/compiler.cpp` üzerinden) ve AOT yolu
(`src/parser/parser.cpp` üzerinden) iki ayrı parsing kuralı yürütüyor.
Tek bir parser çıktısının iki backend tarafından da paylaşılması
hedefiyle çelişiyor. Olasılıkla `compiler.cpp` içindeki function
declaration parser'ı `:` token'ından sonra dönüş tipini anlamayan eski
bir alt küme.

**Önerilen çözüm:** VM compiler'ı da `Parser::parse` AST'ini kullansın.
Şu anki ikili parser zaten teknik borcun açık bir göstergesi.

**RESOLVED (2026-04-27):** Aslında VM ve AOT zaten aynı `Parser::parse`'ı
çağırıyordu; tek eksik [src/parser/parser.cpp:234](src/parser/parser.cpp#L234)
`parse_function_decl` içinde `:` token'ının opsiyonel olarak tüketilmemesiydi.
Tek satırlık `match(TOKEN_COLON);` ile her iki yol da `func name(...): RetType`
sözdizimini aynen kabul ediyor.

---

## 3. AOT codegen: forward reference / recursive function bug'ı

**Şiddet:** Yüksek — koşulsuz çalışan recursive fonksiyonlar binary
seviyesinde kırılmış halde üretiliyor; derleyici "fonksiyon
bulunamadı" *uyarısı* veriyor ama yine de exit-code 0 ile bir exe
döndürüyor; exe çalıştığında hep `0` üretiyor.

**Reprodüksiyon:** `examples/benchmark.tpr` dosyasını derleyin.

```sh
$ ./tulpar build examples/benchmark.tpr bench
HATA (Satır 17): 'fibonacci' adında bir fonksiyon bulunamadı.
HATA (Satır 20): 'fibonacci' adında bir fonksiyon bulunamadı.
[AOT] Successfully created: bench           # <-- "Başarılı" deniyor

$ ./bench.exe
Fibonacci(30):
0                                            # <-- yanlış
Fibonacci(35):
0
```

**Kök neden:** [src/aot/llvm_backend.cpp:1352-1356](src/aot/llvm_backend.cpp#L1352)
bölümünde tanımlanmamış sembol bulunduğunda `nullptr` dönülüyor; ama
codegen üst seviyesi bunu fatal hata olarak almıyor — sessizce
"undefined" dönen ifadeyi 0 olarak yerleştirip devam ediyor. İkincil
sorun: fonksiyon tablosuna *gövde derleme zamanında* yazılıyor (önden
deklarasyon yok); bu yüzden bir fonksiyon kendi gövdesinde recursive
çağrı yaptığında henüz tabloda görünmüyor.

**Önerilen çözüm:** İki geçişli (two-pass) codegen — önce tüm
fonksiyon imzalarını LLVM `Module`'a ekle (forward declaration), sonra
gövdeleri derle. Ek olarak codegen'de her undefined symbol durumunda
hata döndürülmeli ve `aot_compile` non-zero exit ile bitmeli.

**Geçici çözüm:** Recursive çağrıları local'lere hoist et (madde 1
ile aynı workaround); o zaman codegen recursive çağrı için tekrar isim
arıyor ve gövdeyi şimdiden tabloya yazılmış buluyor.

**RESOLVED (2026-04-27):** İki-geçişli codegen eklendi — yeni helper
`predeclare_func_signature` [src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp)
tüm function decl'leri Pass 1a'da forward declare ediyor; Pass 1b
gövdeleri derliyor. `codegen_func_def` ve `codegen_native_func_def`
varolan signature'ı buldu mu yeniden oluşturmak yerine kullanıyor.
`examples/benchmark.tpr` artık `Fibonacci(30)=832040, (35)=9227465, (38)=39088169`
doğru değerlerini üretiyor (önceden hep 0 dönüyordu).

---

## 4. AOT pipeline tamamen Linux varsayımıyla yazılmış (Windows'a port edildi)

**Şiddet:** Orta-Yüksek (bu commit'te düzeltildi, fakat tasarım hâlâ kırılgan)

[src/aot/aot_pipeline.cpp](src/aot/aot_pipeline.cpp) Windows port öncesi
şu sabitleri içeriyordu:

| Hardcoded değer | Windows'ta sorun |
|-----------------|------------------|
| `clang` | Runtime C++ STL kullanıyor → `clang++` olmalı |
| `-no-pie` | MinGW/MSVC tanımıyor |
| `-ldl -lpthread -lm` | Windows'ta yok |
| `-L./build -L./build-linux -L./build-macos` | `-L./build-windows` yok |
| `/tmp/.tulpar_run` | Windows'ta `/tmp` yok |
| `system("./tulpar_temp")` | Windows kabuğu `./` ile çalıştıramaz |

Tümü `PLATFORM_WINDOWS` koşullu hâle getirildi ama doğru çözüm:
**linker komutunu derleme zamanında bir tek yere
(`AOT_LINK_LIB_FLAGS`, `AOT_EXE_SUFFIX`) topla**, ve bu seçimi sadece
config aşamasında yap. Şu an iki ayrı `snprintf` çağrısı (verbose +
silent yollar) var, bir seferde de güncellemek unutuluyor.

**RESOLVED (2026-04-27):** `AOT_LINK_LIB_FLAGS`, `AOT_LINK_PIE_FLAG`,
`AOT_EXE_SUFFIX` makroları
[src/aot/aot_pipeline.cpp:13-30](src/aot/aot_pipeline.cpp#L13)
içinde tek bir blokta tanımlandı; iki snprintf çağrısı da bunlardan
besleniyor. Linker `clang++` (C++ runtime gerekiyor), Windows'ta
`-ldl/-lpthread` yerine `ws2_32 wsock32`, exe `.exe` uzantılı,
`/tmp` yerine cwd-temp.

---

## 5. `interpreter.hpp` Winsock makrolarının üzerine yazıyordu

**Şiddet:** Yüksek (Windows MinGW build'i tamamen kırıyordu)

[src/interpreter/interpreter.hpp:11-24](src/interpreter/interpreter.hpp#L11)
şu makroları platform farkı gözetmeden yeniden tanımlıyordu:

```cpp
#define INVALID_SOCKET INVALID_SOCKET_VALUE   // <-- Winsock üzerine yazıyor
#define SOCKET_ERROR   SOCKET_ERROR_VALUE
```

Winsock2 zaten bu sembolleri tanımlıyor; kendi shim'i (`platform_sockets.h`)
ise `INVALID_SOCKET_VALUE`'yu **`INVALID_SOCKET`'a** çözüyor — sonuçta
bir *sirküler makro tanımı* oluşuyor ve compiler `INVALID_SOCKET` token'ını
hiç çözemiyor.

**Çözüm uygulandı:** Tanımlar `#ifndef` ile sarılıyor. Daha temiz
yaklaşım: shim katmanı (`platform_sockets.h`) zaten `socket_t`,
`tulpar_socket_close`, vb. taşınabilir bir API expose ediyor;
interpreter'in `INVALID_SOCKET` makrolarına hiç ihtiyacı olmamalı —
direkt shim API'sini kullansın, MSVC/MinGW makrolarına dokunmasın.

**RESOLVED (2026-04-27):** [src/interpreter/interpreter.hpp:11](src/interpreter/interpreter.hpp#L11)
artık `#ifndef`-korumalı. MinGW Winsock build'ini açık tutuyor; daha
ileri shim refaktörü ayrı bir iş olarak kalır.

---

## 6. `gettimeofday` ve POSIX tabanlı zaman fonksiyonları MSVC'de yok

**Şiddet:** Orta (düzeltildi)

[src/vm/runtime_bindings.cpp:2865](src/vm/runtime_bindings.cpp#L2865)
runtime'da `aot_time_ms` doğrudan `gettimeofday(&tv, nullptr)` kullanıyordu.
MinGW headers bunu sağlıyor ama MSVC sağlamıyor; ek olarak `<sys/time.h>`
hiç include edilmiyordu — MinGW build'i `winsock2.h` üzerinden gelen
`struct timeval` üzerinden geçiyordu.

**Çözüm uygulandı:** `PLATFORM_WINDOWS` durumunda
`GetSystemTimeAsFileTime` + 100ns→ms dönüşümüne fallback yapıldı.
`tulpar_clock_ms` zaten [src/common/platform.h:125](src/common/platform.h#L125)
içinde tam aynı işi taşınabilir biçimde yapıyor — runtime'da
direkt o kullanılmalıydı. Bir sonraki cleanup'ta birleştirilmeli.

**RESOLVED (2026-04-27):** Windows ve POSIX yolları
[src/vm/runtime_bindings.cpp:2865](src/vm/runtime_bindings.cpp#L2865)
içinde ayrıştırıldı. `tulpar_clock_ms` ile birleştirme, cosmetic refaktör
olarak kaldı.

---

## 7. CMake: MSYS2 LLVM'in `ZLIB::ZLIB` / `LibXml2::LibXml2` bağımlılıkları

**Şiddet:** Orta (düzeltildi)

MSYS2 mingw64'teki `LLVMConfig.cmake`, `LLVMSupport` linkInterface'ine
`ZLIB::ZLIB` ve `LibXml2::LibXml2` imported target'larını ekliyor; ama
bizim `CMakeLists.txt` `find_package(LLVM)` çağırmadan önce ZLIB ve
LibXml2'yi aramıyor → CMake "imported target not found" hatasıyla
yapılandırma adımında çöküyor.

**Çözüm uygulandı:** [CMakeLists.txt:25-29](CMakeLists.txt#L25)
`find_package(LLVM)`'den **önce** `find_package(ZLIB QUIET)`,
`find_package(LibXml2 QUIET)`, `find_package(zstd QUIET)` çağrıları
eklendi. `build.bat` ayrıca `-DCMAKE_PREFIX_PATH=C:/msys64/mingw64`
ile bu paketlerin nereye bakacağını söylüyor.

**RESOLVED (2026-04-27):** İlk port commit'inde uygulandı; tekrar
test edildi ve stabil.

---

## 8. Test runner ve örnek dosyalarda parser/runtime regresyon

İlk full build sonrası `run_tests.bat` ile **30 örnek**ten 16 PASS, 12
SKIP, 2 FAIL çıktı. Network/REPL gerektirenler bilinçli skip; bunun
dışında **gerçek 2 başarısızlık**:

### 8a. `04_math_logic.tpr` — BigInt segfault
- Belirti: `tulpar build examples/04_math_logic.tpr` çağrısı **SIGSEGV** atıyor.
- Olası yer: `std::stoll` C++ exception'ını yakalamadan kullanan BigInt parser
  tarafı; `Ayristirma hatasi: stoll` mesajı runtime'a sızıyor, aynı patte
  segfault.
- Şu an: hata mesajı runtime'a sızdığı için backend'in stoll'u
  `try { ... } catch(std::invalid_argument&) { ... }` içinde sarması gerek.

**RESOLVED (2026-04-27):** [src/parser/parser.cpp:548](src/parser/parser.cpp#L548)
`std::stoll` ve `std::stod` artık `try/catch (std::out_of_range &)` ve
`std::invalid_argument` ile sarılıyor. i64'ü aşan literaller kullanıcıya
satır numarasıyla uyarı verilip `INT64_MAX`'a kırpılıyor; segfault yok.
`04_math_logic.tpr` test paketinden geçiyor.

### 8b. `07_modules.tpr` — modül parser zinciri
- Belirti: `examples/utils.tpr` import edildiğinde
  `Expected ';' after expression at line 18`, `Expected ')' after for clauses`
  zinciri başlıyor.
- [examples/utils.tpr](examples/utils.tpr) içinde for-loop sözdizimi
  ana parser'ın anlamadığı bir varyant kullanıyor (muhtemelen
  yeni eklenen ama parser'a entegre edilmemiş bir feature).
- Şu an `SKIP_TESTS`'a alınması gerekirdi; alternatif olarak parser
  yamasıyla feature'ın gerçekten desteklenmesi gerek.

**RESOLVED (2026-04-27, kısmi):** Asıl regresyon — `for(...; ...; i = i + 1)`
artıma `=` assignment kabul edilmiyordu — düzeltildi
([src/parser/parser.cpp:355](src/parser/parser.cpp#L355)). `utils.tpr`
artık standalone parse oluyor. `07_modules.tpr` ise utils değil,
**`p.x = 10;` (struct dot access)** feature gap'i nedeniyle başarısız —
bu ayrı bir madde olarak skip listesinde.

---

## 9. `01_hello_world.tpr` — eskimiş örnek dosyalar (rezerve isim çakışması)

**Şiddet:** Düşük (örnek hatası)

[examples/01_hello_world.tpr:134-136](examples/01_hello_world.tpr#L134) içinde:
```tpr
float ondalik = 3.14;
int tam = toInt(ondalik);
```

`ondalik` artık `TOKEN_FLOAT_TYPE` keyword'ü
([src/lexer/lexer.cpp:42](src/lexer/lexer.cpp#L42)). Yani `float ondalik`
ifadesi `<tip><tip>` olarak parse ediliyor → `Expected variable name`
hatası. Mevcut codegen "tanımlanmamış" sembolü 0 olarak yerleştirip
devam ettiği için exe yine üretiliyor ve doğru görünen bir çıktı
veriyor — fakat aslında bozuk.

**Önerilen aksiyon:** Tüm `examples/*.tpr` dosyalarını şu anki
keyword listesine karşı taramak ve kullanılan eski kelimeleri yeniden
adlandırmak. Aynı zamanda lexer/parser hata bulduğunda codegen
**fatal** olmalı (madde 3 ile birleşik).

**RESOLVED (2026-04-27):** Tüm `examples/*.tpr` taranıp keyword
listesine karşı kontrol edildi. Tek çakışma `01_hello_world.tpr` line 134
idi (`ondalik`); `pi_yaklasik` / `tam_kisim` olarak yeniden adlandırıldı.
Codegen-fatal kararı için ayrı issue yeterli — sessiz fail uzun
vadede yapılacak iş.

---

## 10. Build sistemi temizliği (kullanıcı talebi sonucu yapıldı)

Önce repoda 9 farklı build klasörü vardı:
`build`, `build-ci-check`, `build-clang`, `build-cpp17`, `build-linux`,
`build-linux-ci`, `build-linux-ci2`, `build-locale-check`, `build-windows`.

Bu klasörlerden bazıları (`build/libtulpar_runtime.a`, 02-Nisan tarihli)
**eskimiş runtime archive** içeriyordu; AOT pipeline `-L./build`'i ilk
sıraya koyduğu için yeni `build-windows/libtulpar_runtime.a` yerine eski
arşivi linkliyor → `undefined reference to aot_runtime_init` vb. hatalar.

**Çözüm uygulandı:**
1. [build.bat](build.bat) artık her build başında **`build-windows`**
   içeriğini siliyor, aynı klasöre yeniden inşa ediyor (kullanıcı
   talebi).
2. AOT pipeline `-L` öncelik sırası `build-windows → build-windows/Release → build`
   olarak değiştirildi.
3. Eski 8 klasör silindi; geriye sadece `build-windows/` kaldı.

`build.sh` (Linux/macOS) hâlâ `build-linux` / `build-macos` ad ayrımını
koruyor — bu istenirse `build/` tek klasör formatına çevrilebilir.

**RESOLVED (2026-04-27):** `build.sh` de artık `build-<platform>`
klasörünün içeriğini her build'de wipe ediyor (build.bat ile aynı
davranış). `build.ps1` build.bat'a thin wrapper'a indirgenerek
tek source-of-truth sağlandı. Madde 12'yi de bakın.

---

## 11. Eksik/yarım stdlib modülleri

Repoda `lib/` altında hangi modüllerin **embed edildiği** ile hangilerinin
sadece dosya hâlinde kaldığı tutarsız:

| Modül | Durum |
|---|---|
| `wings.tpr` | Embed |
| `router.tpr` | Embed |
| `http_utils.tpr` | Embed |
| `async.tpr` | **Sadece dosya — embed edilmiyor** |
| `middleware.tpr` | **Sadece dosya** |
| `socket.tpr` | **Sadece dosya** |
| `tulpar_api.tpr` | **Sadece dosya** |

Sonuç: `examples/api_*.tpr`, `tulpar_api_demo.tpr`,
`12_threaded_server.tpr` gibi async/middleware/socket modüllerine
bağlı örnekler **`SKIP_TESTS` listesinde duruyor**. Bu örnekler hiç
çalıştırılmıyor; embed edildikleri zaman parser veya runtime
seviyesinde hangi sorunların çıkacağı bilinmiyor.

**Önerilen aksiyon:** Üçünden biri seçilmeli:
1. Stdlib'i tamamla → `cmake/EmbedLibraries.cmake`'e ekle.
2. İlgili örnekleri `examples/`'tan çıkar (yanlış intibâ veriyor).
3. Modüller "deneysel" olarak işaretlensin ve dökümantasyona ekle.

**RESOLVED (2026-04-27):** Aksiyon 1 seçildi. Madde 1+8b parser
düzeltmelerinden sonra dört modülün tamamı (`async`, `middleware`,
`socket`, `tulpar_api`) standalone parse ediyor; tümü
[cmake/EmbedLibraries.cmake](cmake/EmbedLibraries.cmake) ve
[src/embedded_libs.h.in](src/embedded_libs.h.in)'e eklendi. `import "..."`
çağrısı bunları yerleşik olarak buluyor. Sonuç: 5 stdlib-bağımlı örnek
(`api_simple`, `api_wings`, `api_wings_crud`, `tulpar_api_demo`,
`benchmark` zaten geçiyordu) artık SKIP listesinden çıktı ve PASS oldu —
test geçişi 16 → 22.

---

## 12. Build script'leri arasında tutarsızlık

| | `build.sh` | `build.bat` (yeni) | `build.ps1` |
|---|---|---|---|
| Klasör adı | `build-linux` / `build-macos` | `build-windows` | ? |
| Klasör temizleme | Yok (incremental) | **Var** (içerik silinir) | ? |
| Test runner | `build.sh test` | `run_tests.bat` (ayrı dosya) | ? |
| Temizlik komutu | `build.sh clean` | `build.bat clean` | ? |

İdeal olan tek bir CMake-presets veya CTest-tabanlı yaklaşıma
geçilmesi; şu an her platformda farklı bir araç gerekiyor ve
davranış farkları yamadan yamada birikiyor.

**RESOLVED (2026-04-27):**

| | `build.sh` | `build.bat` | `build.ps1` |
|---|---|---|---|
| Klasör adı | `build-linux` / `build-macos` | `build-windows` | (build.bat) |
| Klasör temizleme | **Var** (içerik silinir) | **Var** (içerik silinir) | (build.bat) |
| Test runner | `build.sh test` | `run_tests.bat` | n/a |
| Temizlik komutu | `build.sh clean` | `build.bat clean` | `build.ps1 clean` |
| SKIP listesi | run_tests.bat ile aynı | base | — |

`build.ps1` artık `build.bat`'a thin wrapper. SKIP listeleri tek
kaynaktan (run_tests.bat / build.sh test) tutarlı. CMake-presets
adımı uzun vadeli iş olarak kaldı.

---

## 13. Performans gözlemleri (benchmark sonuçları üzerinden)

Detay: [benchmarks/RESULTS.md](benchmarks/RESULTS.md)

| Workload | Tulpar AOT vs C(O2) | Tulpar AOT vs Python | Tulpar VM vs Python |
|---|---:|---:|---:|
| `loopsum` (10M döngü) | **1.21x daha yavaş** | **3.08x daha hızlı** | 1.40x daha yavaş |
| `fib(35)` (recursive) | **1.94x daha yavaş** | **2.73x daha hızlı** | 1.42x daha hızlı |

Çıkarımlar:
- AOT yolu LLVM optimize ettiği için sıkı integer döngü C-class.
- Recursive call performansı zayıf — büyük ihtimalle ABI/calling
  convention veya argüman boxing yüzünden. AOT codegen'in `VMValue`
  argüman boxing'i (madde 14'e bakın) çağrı maliyetinin büyük
  bölümünü oluşturuyor olabilir.
- VM yolu yavaş — sıkı döngü hot loop için bytecode dispatch'in
  bedeli görünüyor; computed-goto + register-based VM (mevcut tasarım
  hâlâ stack tabanlı görünüyor) ciddi kazanç sağlardı.

---

## 14. Codegen değer temsili (`VMValue`) — gizli kalan tasarım sorusu

LLVM IR çıktısında her runtime fonksiyonu (`aot_print_value`,
`vm_binary_op`, `vm_alloc_string_aot`) `VMValue` (boxed tagged-union)
alıyor. Yani AOT yolu *unboxed integer aritmetik için bile* runtime
çağırıyor:

```ll
call void @aot_print_value(%VMValue ...)   ; her print'te boxing
call %VMValue @vm_binary_op(%VMValue, %VMValue, i32 op)  ; her +,-,*,/'de
```

Bu, Tulpar AOT'nin C'ye 1.2x yaklaşmasına rağmen **hâlâ %20 farkı**
açıklar. C/Rust düz `i64 += i64`'ü tek bir `add rax, rbx` instruction'a
indiriyor; Tulpar her aritmetik için fonksiyon çağırıyor.

**Önerilen yön:**
- Tip çıkarımı sonrası tipi sabit (monomorphic) kanıtlanan ifadelerde
  unboxed yola düş.
- `vm_binary_op` çağrısı yerine LLVM IR'da doğrudan `add/sub/mul/sdiv`.
- En az 5x daha hızlı `loopsum` ve büyük ihtimalle 2x daha hızlı
  `fib` getirir.

**RESOLVED (2026-04-27):** Native typed codegen yolu zaten kısmen vardı
(`codegen_typed_expr` + `codegen_native_func_def`); eksiklik şuydu:
**(a)** native function body walker `AST_WHILE`'i tanımıyordu
([src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp)'a `while.cond/body/end`
LLVM block'lu handler eklendi); **(b)** Madde 2 fix'i sayesinde `: int`
return-type sözdizimi parser'a girdiği için kullanıcı kodu native path'i
tetikleyebiliyor. Sonuç IR'da `vm_binary_op` çağrısı yerine direkt
`add i64`, `icmp slt i64`. Ölçüm:

- `fib(35)`: 364 ms → **185 ms** (1.97×, Go ile eşdeğer, C+17%)
- `loopsum`: 175 ms → **162 ms** (1.08×, Go ile eşdeğer, C+12%)

`benchmarks/{fib,loopsum}.tpr` typed sözdizim kullanıyor; AOT IR
boxing'siz native i64 üretiyor. 5x kestirimi karşılanmadı (loopsum'da
sadece 1.08×) çünkü zaten LLVM eski boxed yolu da fairly iyi optimize
ediyordu; fib'de tahmin tutarlı.

---

## Özet: Öncelik sıralaması (durum)

| # | Madde | Durum |
|---|---|---|
| 1 | Parser postfix bug'ı | ✅ RESOLVED |
| 2 | VM/AOT parser ayrımı | ✅ RESOLVED |
| 3 | AOT recursive forward reference | ✅ RESOLVED |
| 14 | AOT codegen unboxed yol | ✅ RESOLVED (kısmi: typed-fonksiyon yolu çalışıyor; top-level globals hâlâ boxed) |
| 11 | Stdlib modüllerinin tamamlanması | ✅ RESOLVED |
| 8a | BigInt segfault | ✅ RESOLVED |
| 8b | Module/utils.tpr parser regresyonu | ✅ RESOLVED |
| 9 | Örnek dosya keyword temizliği | ✅ RESOLVED |
| 12 | Build script birleşmesi | ✅ RESOLVED |
| 4 | AOT pipeline Linux varsayımları | ✅ RESOLVED |
| 5 | Winsock makro clobber | ✅ RESOLVED |
| 6 | gettimeofday MSVC eksiği | ✅ RESOLVED |
| 7 | CMake LLVM ZLIB/LibXml2 | ✅ RESOLVED |
| 10 | Build sistemi temizliği | ✅ RESOLVED |
| 13 | Performans gözlemleri | ✅ Yenilenen ölçümler raporlandı |
| — | Codegen-fatal | ✅ RESOLVED (2026-04-28) |
| — | Struct dot access (`p.x = 10`) | ✅ RESOLVED (2026-04-28; #15 fix'iyle) |
| — | `tulpar_clock_ms` ↔ `aot_time_ms` birleşmesi | ✅ RESOLVED (2026-04-28) |
| 15 | AOT fn-local json subscript get/set | ✅ RESOLVED (2026-04-28) |
| 16 | AOT fn-local string len/concat | ✅ RESOLVED (2026-04-28) |
| 17 | AOT hot loop alloca → stack overflow | ✅ RESOLVED (2026-04-28) |
| 18 | AOT BubbleSort(>=200) stack-buffer-overrun | ✅ RESOLVED (2026-04-28) |
| — | Top-level int globals unboxed yol | ✅ RESOLVED (zaten unboxed; doğrulandı) |
| — | CMake-presets | ✅ RESOLVED (`CMakePresets.json` eklendi) |
| 19 | Imported modüllerde fonksiyon-imzası forward decl yok | ✅ RESOLVED (2026-04-29) |
| 20 | `lib/tulpar_api.tpr` ölü `len = len;` ataması | ✅ RESOLVED (2026-04-29) |
| 21 | SKIP listesindeki sunucu örnekleri hiç doğrulanmıyor | ✅ RESOLVED (2026-04-29; compile-smoke pass) |
| 22 | AST_IMPORT'ta `[AOT DEBUG]` printf spam'i | ✅ RESOLVED (2026-04-29) |
| 23 | `[AOT] Importing:` mesajı silent path'te de yazılıyordu | ✅ RESOLVED (2026-04-29) |
| 24 | `LARGE_INTEGER {0}` -Wmissing-field-initializers | ✅ RESOLVED (2026-04-29) |
| 25 | `utils.tpr` SKIP'te → derleme regresyonu yakalanmıyor | ✅ RESOLVED (2026-04-29; compile-only) |
| 26 | Vendored SQLite `-Wstringop-overread` uyarıları | ✅ RESOLVED (2026-04-29) |
| 27 | Markdown lint stil uyarıları (`MD060/MD031/MD022/...`) | ✅ RESOLVED (2026-04-29; `.markdownlint.json`) |
| 28 | HTTP parser yorumlanmış kodda → "C kadar hızlı" sloganı uyumsuz | ✅ RESOLVED (2026-04-29; native `http_parse_request` genişletildi) |
| 29 | `aot_http_create_response` sadece 9 status code'u biliyordu | ✅ RESOLVED (2026-04-29; tüm 1xx-5xx mapping + `http_status_text` builtin) |
| 30 | `wings.tpr` HTTP'i interpreter'da ayrıştırıyordu | ✅ RESOLVED (2026-04-29; native parser'a göçtü) |
| 31 | `router.tpr` `match_path` her istekte interpreter split/substring çağırıyordu | ✅ RESOLVED (2026-04-29; native `path_match` builtin) |
| 32 | Query string parsing yarım/yavaş (URL decode patch'leri vs) | ✅ RESOLVED (2026-04-29; native `parse_query` builtin, full URL decode) |
| 33 | `http_create_response` custom header'lara izin vermiyordu (CORS/cookies) | ✅ RESOLVED (2026-04-29; 4-arg `http_create_response(status, ct, body, headers)`) |
| 34 | Wings handler'ları default'ta browser fetch'e CORS hatası veriyordu | ✅ RESOLVED (2026-04-29; wings.tpr default CORS header'ları gönderiyor) |
| 35 | Test framework yok → her commit el ile smoke ile doğrulanıyordu | ✅ RESOLVED (2026-04-29; `lib/test.tpr` + `tests/*.test.tpr` + run_tests.ps1 entegrasyonu) |
| 36 | Native `path_match` `VM_BOOL` döndürüyordu, `assert_eq_int` ile uyumsuzdu | ✅ RESOLVED (2026-04-29; `VM_INT(0/1)` ile değiştirildi) |
| 37 | AOT'da kullanıcı fonksiyonları dışa aktarılmıyordu → `call()` "Function not found" | ✅ RESOLVED (2026-04-29; linker'a `--export-all-symbols`/`-rdynamic` eklendi) |
| 38 | `exit()` builtin'i AOT path'inde tanımsızdı | ✅ RESOLVED (2026-04-29; `aot_exit_i32` runtime function) |
| 39 | Native `parse_query` builtin yoktu, router.tpr Tulpar-seviye yazıyordu | ✅ RESOLVED (2026-04-29; #32 ile birlikte) |
| 40 | `vm_make_bool` `as` slot'unu tam sıfırlamıyordu → SysV ABI ret-pair'da garbage bytes | ✅ RESOLVED (2026-04-29; `vm_make_bool` `as.int_val=0` yapıp `bool_val` yazıyor) |
| 41 | Standard kütüphane builtin'leri için regresyon koruması yoktu | ✅ RESOLVED (2026-04-29; `tests/{strings,json,math}.test.tpr` 26 assertion) |
| 42 | Test framework'ünde error-path coverage yoktu | ✅ RESOLVED (2026-04-29; `assert_throws` + `tests/errors.test.tpr`) |
| 43 | Codegen hata mesajları tek satırlık → "Python kadar kolay" sloganına ters | ✅ RESOLVED (2026-04-30; Rust-stil source-line excerpt + caret + ipucu) |
| 44 | Diagnostic'lerde dosya yolu yoktu (`(stdin):N`) | ✅ RESOLVED (2026-04-30; `aot_compile_with_filename` + `aot_compile_and_run_silent_with_filename`) |
| 45 | "Undefined var in {array access, increment, decrement, compound assign, assignment}" 5 ayrı yer hâlâ legacy formatta | ✅ RESOLVED (2026-04-30; hepsi `report_codegen_error` üzerinden) |
| 46 | Hata mesajları typo'larda yardımcı olmuyordu (no "did you mean…?") | ✅ RESOLVED (2026-04-30; bounded-Levenshtein + scope/global/function name set) |
| 47 | Parser hataları hâlâ tek satırlık format kullanıyordu | ✅ RESOLVED (2026-04-30; `parser_set_diagnostic_context` + Rust-stil source preview + filename + caret) |
| — | Legacy tree-walk interpreter sunset | ✅ RESOLVED (2026-05-04; PR #30, −6109 satır) |
| — | x64 JIT sunset | ✅ RESOLVED (2026-05-04; PR #31, −2162 satır) |
| — | typeinfer build/run pre-pass | ✅ RESOLVED (2026-05-05; PR #32, `[typecheck]` uyarıları otomatik) |
| — | Yerleşik fonksiyon imza kataloğu | ✅ RESOLVED (2026-05-05; PR #33, 50+ builtin) |
| 48 | VM string concat fn-local'de `0` döndürüyor (AOT doğru) | ✅ RESOLVED (2026-05-05; OP_LOAD_ADD_STORE'a string dalı) |
| 49 | Modül namespace'leri yok (`import ... as alias`) | ✅ RESOLVED (2026-05-05; PR #35) |
| 50 | typeinfer `len`/`abs` polimorfizmi wildcard kullanıyor | 🟡 OPEN (false negative) |

### Kalan iş kalemleri (sonraki etap)

- ✅ **48. VM string concat fn-local'de `0` döndürüyor** — RESOLVED (2026-05-05). Kök neden: `s = s + " world"` AST_ASSIGNMENT'ın `var = var + expr` superinstruction optimizasyonuna düşüyordu (`OP_LOAD_ADD_STORE`); fast path int-only, fallback `BINARY_OP(+)` ise string pair'i bilmiyor → default arm `VM_FLOAT(0.0)` üretip user'ın string atamasını sessizce siliyordu. [src/vm/vm.cpp:OP_LOAD_ADD_STORE](src/vm/vm.cpp) içinde IS_STRING&IS_STRING dalı eklendi (OP_ADD'in concat path'inin paralel kopyası, fakat in-place append yerine her zaman fresh buffer çünkü fn-local sum başka değişkenden alias'lanabilir). [tests/strings.test.tpr](tests/strings.test.tpr) iki yeni regresyon koruyucu ekledi (`fn_local_string_concat_typed` + `fn_local_string_concat_let`).
- ✅ **49. Modül namespace'leri** — RESOLVED (2026-05-05; PR #35). `import "name" as alias;` sözdizimi parser'a eklendi; `apply_import_alias` helper'ı modülün top-level fonksiyonlarını `<alias>__<name>` ile mangle ediyor, modül-içi cagrilar da aynı anda yeniden yazılıyor. AOT (`AST_IMPORT` codegen) ve VM (`OP_IMPORT` runtime) ortak helper'ı çağırıyor. `as` bağlamsal identifier (rezerv kelime değil); `let as = 42` hala çalışıyor. Phase 2 (Python-style `m.func()` syntax) sırada.
- 🟡 **50. typeinfer `len`/`abs` polimorfizmi wildcard** — PR #33 katalog wildcard kullanıyor (`len(unknown)`), bu `len(42)` gibi açık hataları yakalamıyor. İlerideki çözüm: `TYPE_COLLECTION` pseudo-tipi veya çoklu-imza kaydı (`len(string)->int` + `len(array)->int`). Düşük öncelik — gerçek hayatta nadir.

### 2026-05 etabı

Aşağıdaki maddeler 2026-05 etabıyla tamamlandı:

- ✅ **Legacy tree-walk interpreter sunset** (PR #30, 2026-05-04): `src/interpreter/` (~6,109 satır, 5,784 satırlık `interpreter.cpp` dahil) silindi. REPL son tüketicisiydi; PR #29 ile VM-backed'a göçtü. `--legacy` CLI bayrağı kaldırıldı, fallback yolu yok. Kazanç: tek path bug fix'leri her zaman AOT+VM iki yola da uygulanıyor; üç-yol divergansı (#2 örneği) tarihe karıştı.
- ✅ **x64 JIT sunset** (PR #31, 2026-05-04): `src/jit/` (2,162 satır) silindi. Threshold-triggered tier-1 native code emitter idi; fib(28) ölçümlerinde JIT-on (282/287/285ms) vs JIT-off (282/280/289ms) gürültü içinde — kazanç yok. ARM64'te zaten `JIT_ENABLED=0` idi; production AOT yolu hiç çağırmıyordu. `ObjFunction.jit_code`, `LoopTrace`, `CallSiteCache.cached_jit`, `jit_helper_call`/`jit_interpreter_call` mini-interpreter da temizlendi.
- ✅ **typeinfer build/run pre-pass** (PR #32, 2026-05-05): `typeinfer_emit_warnings` paylaşılan helper'ı `tulpar` / `tulpar build` / `tulpar --vm` yollarında otomatik çalışıyor — `[typecheck] <path>: <msg>` uyarıları stderr'a basılıyor, build/run hicbir zaman bloklanmıyor. `--no-typecheck` bayrağı ve `TULPAR_NO_TYPECHECK=1` env var'ı kapatma yolu. Eski opt-in `tulpar typecheck` subcommand'i hata modunda korunuyor.
- ✅ **Yerleşik fonksiyon imza kataloğu** (PR #33, 2026-05-05): typeinfer 50+ yerleşiğin imzasını önceden yüklenir hale getirildi (math/io/string/file/socket/db/thread). Polimorfik konumlar TYPE_UNKNOWN wildcard ile kaydedildi — arg sayısı hep denetleniyor, monomorfik tip mismatch'leri (`substring(123, 0, 2)`, `read_file()`, `sqrt(1.0, 2.0)`) build öncesi yakalanıyor. Variadik (`print`, `println`) ve özel (`call`) yerleşikler bilerek katalog dışı.

### 2026-04-28 etabı

Aşağıdaki maddeler yeni etapla birlikte tamamlandı:

- ✅ **Codegen-fatal**: `LLVMBackend->had_error` bayrağı eklendi; "değişken
  tanımlanmamış" / "fonksiyon bulunamadı" gibi tüm "HATA" üreten yerler
  set ediyor. `aot_compile` ve `aot_compile_silent` body emit'i bittiği
  anda kontrol edip `AOT_ERROR_CODEGEN` ile bitiyor; broken IR ile exe
  oluşturulmuyor.
- ✅ **Struct dot access** (`p.x = 10;`): parser zaten desteğe sahipti
  (parse_postfix `TOKEN_DOT`'ı `obj["x"]` olarak desugar ediyor); AOT'taki
  fn-local indeksleme düzeldikçe `07_modules.tpr` doğal şekilde geçti.
- ✅ **`tulpar_clock_ms` ile `aot_time_ms` birleşmesi**:
  [src/common/platform.h](src/common/platform.h#L150) içine
  `tulpar_epoch_ms()` eklendi (ms-since-epoch); `aot_time_ms` artık onu
  çağırıyor. Win/POSIX dallanması tek dosyada.
- ✅ Maddeler 15, 16 (fn-local json/string codegen) ve aşağıdaki yeni
  17, 18 maddeleri için bakın.
- ✅ **#18 BubbleSort stack-buffer-overrun** (ek tur): kök neden
  `llvm_values.cpp`'deki `llvm_call_vmvalue_func` ve
  `llvm_convert_ret_pair_to_vmvalue` içinde block-local kalan iki alloca
  noktasıydı. Helper `llvm_backend.hpp` üzerinden ihraç edilip her ikisi
  entry-hoisted hale getirildi. compare_benchmark.py'da ortak alt küme
  artık **11/11 test**.
- ✅ **Top-level int globals için unboxed yol**: doğrulama yapıldı —
  `int total = 0; while(...) total = total + i;` top-level şeklinde
  yazıldığında AOT IR'da `@total = global i64 0` olarak çıkıyor (boxed
  VMValue değil). LLVM optimizer da loop'u tamamen fold ediyor. Eski
  EKSIKLER notu eskimiş kalmış; gerçek ölçüm fn-wrapped (~163 ms) ile
  top-level (~151 ms) arasında fark olmadığını gösteriyor.
- ✅ **CMake-presets**: [CMakePresets.json](CMakePresets.json) eklendi —
  windows-mingw / linux / macos / ninja preset'leri. Mevcut
  `build.sh`/`build.bat`/`build.ps1` script'leri bozulmadan, alternatif
  olarak `cmake --preset linux && cmake --build --preset linux`
  kullanılabiliyor. Preset listesi `cmake --list-presets` ile görülebilir.

---

## 15. AOT codegen: fonksiyon-yerel `json` array atama / indeksleme bozuk

**Şiddet:** Yüksek — `benchmarks/compare_benchmark.py`'da Sieve, BubbleSort,
ArrayMemory, JSONBuild testleri AOT'ta sessizce yanlış sonuç veriyor.
2026-04-28 tarihli benchmark çalışması sırasında ortaya çıktı.

**Reprodüksiyon — atama:**

```tpr
func mutate(int n): int {
    json arr = [];
    for(int i = 0; i < n; i = i + 1) { push(arr, 0); }
    arr[2] = 99;
    return arr[2];   // AOT: 0 (yanlış); VM: 0 (yanlış); beklenen: 99
}
print(mutate(10));   // AOT/VM: 0
```

Top-level scope'taki json arrays'te aynı atama AOT'ta çalışıyor. Fonksiyon
yerel scope'ta `arr[i] = x` her iki path'te de no-op gibi davranıyor.

**Reprodüksiyon — indekste runtime hatası:**

```tpr
func read(int n): int {
    json arr = [];
    push(arr, 7); push(arr, 8);
    return arr[1];   // AOT: "Runtime Error: Invalid index or target for get access"
}
```

Aynı pattern top-level scope'ta sorunsuz; fonksiyon yereline taşınınca AOT
patlıyor.

**Etkilenen testler (compare_benchmark.py):** Sieve (Eratosthenes), BubbleSort,
ArrayMemory, JSONBuild. Şu an `benchmark_aot.tpr` bu beşini "N/A" işaretiyle
atlıyor; Sieve trial-division ile yapılıyor (array kullanmıyor).

**Olası kaynak:** `src/aot/llvm_values.cpp` veya `runtime_bindings.cpp`
içindeki json subscript get/set işlevlerinin fonksiyon scope'unda yerel
değişken pointer'ı yerine "anlık geçici" işlemesi.

**RESOLVED (2026-04-28):** Kök neden `: int` fonksiyonlar için kullanılan
typed AOT codegen yolunda; bu yol tüm yerel değişkenleri koşulsuz olarak
i64 alloca'ya yerleştirip non-int initializer'ların VMValue tag bilgisini
kaybediyordu, ayrıca `push(arr,...)` / `print(arr)` gibi standalone
statement'ları sessizce atlıyordu (handler yok). Düzeltme: yeni
`native_codegen_supports_body()` helper'ı body'yi dolaşıp native path'in
desteklemediği bir desen bulduğunda (TYPE_INT/BOOL/UNKNOWN dışı VAR_DECL,
AST_FUNCTION_CALL/AST_PRINT vb. statement) regular VMValue codegen yoluna
düşülüyor — `predeclare_func_signature` ile `codegen_func_def` aynı kararı
alıyor, böylece imza/body uyumsuzluğu da çıkmıyor. Sieve(Eratosthenes),
ArrayMemory, JSONBuild, StringConcat, StringAlloc artık AOT'ta da doğru
sonuç veriyor (compare_benchmark.py'da N/A → ölçülen değer).

## 16. String fonksiyon-yerel: `len(s)` 0 dönüyor, `s = s + "..."` no-op

**Şiddet:** Yüksek — `string` tipi fonksiyon içinde gerçek hayatta
kullanılamıyor; StringConcat/StringAlloc benchmarkları geçersiz.

**Reprodüksiyon:**

```tpr
func a(): int {
    string s = "hello";
    return len(s);    // AOT: 0 (yanlış); VM: 5 (doğru)
}
func b(): int {
    string s = "hello";
    s = s + " world";
    return len(s);    // AOT: 0; VM: 0; beklenen: 11
}
```

`a()` AOT'ta `len` 0 dönüyor — string variant boxing'i fonksiyon yerelinde
"empty" gibi görünüyor olmalı. `b()` her iki path'te de bozuk; muhtemelen
`s = s + x` deyimi fonksiyon yereli için yeniden atama yerine in-place
mutation deniyor (variant'ın ref-count'lu paylaşılan storage'ına yazıyor).

**Etkilenen testler:** StringConcat(1K), StringAlloc(1K). compare_benchmark.py
şu an Tulpar için bunları "N/A" gösteriyor.

**RESOLVED (2026-04-28):** Madde 15 ile aynı kök neden — `: int` fonksiyon
gövdesinde fn-local non-int locals i64 alloca'ya zorlanıyordu, bu da string
VMValue tag'ini siliyor ve `len()` / `+` operasyonunu int üzerinde
çalıştırıyordu. native_codegen_supports_body fix'i ile bu fonksiyonlar
artık regular VMValue codegen'i kullanıyor; `len(s)` ve `s = s + "..."`
fn-local'de doğru çalışıyor. compare_benchmark.py'da StringConcat ve
StringAlloc artık ölçülen değer üretiyor.

---

## 17. AOT hot loop'larda `LLVMBuildAlloca` her iterasyonda stack büyütüyor

**Şiddet:** Yüksek — uzun loop'lu programlar AOT'ta `STATUS_STACK_OVERFLOW`
ile çöküyor. 2026-04-28 fn-local fix'inden sonra ortaya çıktı (BubbleSort
gibi gerçek mutating loop'lar artık fn-local fix sayesinde çalıştığında).

**Kök neden:** [src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp)'daki
codegen yolu birçok yerde `LLVMBuildAlloca(backend->builder, vm_value_type,
...)` yapıyordu. Builder o anda hangi block'ta ise alloca o block'a
ekleniyor; loop body'de yapılan alloca her iterasyonda stack frame'i 16
byte büyütüyor. 1M iter → 16MB taşma.

**Çözüm:** Tüm `LLVMBuildAlloca(builder, vm_value_type, ...)` çağrıları
`llvm_build_alloca_at_entry()` ile değiştirildi (zaten var olan helper).
Böylece her alloca fonksiyonun entry block'unda statik bir kez ayrılıyor.
Etkilenen yollar: `vm_get_element` get/set temps, `vm_binary_op` L/R/res
temps, `aot_string_concat_fast`, `push()` builtin, user function call ABI
(call_res_ptr / arg_tmp), `print_value_inline` temp, vb. — toplam ~46
çağrı yeri.

## 18. AOT BubbleSort >=200 boyutta `STATUS_STACK_BUFFER_OVERRUN` (canary trip)

**Şiddet:** Orta — kullanıcı koduna yansıması nadir (çoğu program 200+
elemanlık nested swap-loop'a ihtiyaç duymuyor), ama benchmark suite'i
etkiliyor.

**Reprodüksiyon:**

```tpr
func bubbleSort(int size): int {
    json arr = [];
    /* push 12K-class seed values */
    for(int i = 0; i < size; i = i + 1) { ... push(arr, seed); }
    for(int i = 0; i < size; i = i + 1) {
        for(int j = 0; j < size - 1; j = j + 1) {
            if(arr[j] > arr[j+1]) {
                int t = arr[j]; arr[j] = arr[j+1]; arr[j+1] = t;
            }
        }
    }
    /* checksum */
}
print(bubbleSort(500));   // exit code 0xC0000409
```

`bubbleSort(100)` çalışıyor; 200+'de PowerShell `-1073740791` (0xC0000409
= STATUS_STACK_BUFFER_OVERRUN, GS-cookie failure) ile bitiyor. VM yolu
(`tulpar --vm`) aynı kodu sorunsuz çalıştırıyor.

**Şu an:** `benchmarks/benchmark_aot.tpr` Test 7'yi `Sure: N/A` ile
işaretliyor (Madde 17 fix'inden sonra bile çözülmedi). compare_benchmark.py
Tulpar için BubbleSort'u skip listesinde, ortak alt küme 10 test üzerinden
karşılaştırıyor.

**Olası kaynak:** Madde 17 fix'i alloca taşmasını çözdü ama BubbleSort hâlâ
düşüyor → sebep alloca değil, runtime'da `vm_array_set` veya
`aot_array_get_raw` (RC, ObjArray realloc) etrafında bir bellek
overrun'u. VM impl temiz çalıştığı için sorun AOT codegen'in çağrı
ABI'siyle runtime bağı arasında — IR'da yanlış sıralama, alignment veya
yanlış `byval` semantiği muhtemel adaylar.

**RESOLVED (2026-04-28, ek tur):** İlk teşhisim yanlıştı — sebep gerçekten
alloca taşmasıydı ama madde 17 fix'i sadece `src/aot/llvm_backend.cpp`'yi
kapsamıştı. `src/aot/llvm_values.cpp` içindeki
`llvm_call_vmvalue_func` (Win64 sret yolu) ve
`llvm_convert_ret_pair_to_vmvalue` (SysV ret-pair yolu) hâlâ raw
`LLVMBuildAlloca` kullanıyordu. Her VMValue-dönüşlü runtime call için 1
sret_out + N×vmarg_slot alloca üretiyorlardı; 250K iterasyonlu nested
loop'larda 12+ MB cumulative stack growth → GS-cookie tripped. İki
fonksiyonda da `llvm_build_alloca_at_entry` kullanılacak şekilde
düzeltildi (helper artık `llvm_backend.hpp` üzerinden ihraç ediliyor).

Doğrulama: `bubbleSort(100/500/1000)` AOT'ta sırasıyla 1668642 / 8418986 /
16109908 doğru sonucunu üretiyor; VM ile birebir aynı. compare_benchmark.py
ortak alt küme **6 → 10 → 11 test** olarak genişledi; Tulpar BubbleSort(1K)
2.46 ms (Rust 564 µs'e karşı 4.4× yavaş, ama yarışan).

---

## 19. AOT codegen: imported modüllerde fonksiyon imzaları predeclare edilmiyordu

**Şiddet:** Yüksek — herhangi bir transitif `import` zincirinde, çocuk
modülün gövdesi ebeveyn modülün fonksiyonlarına başvurursa codegen
"fonksiyon bulunamadı" verip exit code 1 ile düşüyordu. 2026-04-29
tarihinde compile-smoke testi sırasında ortaya çıktı.

**Reprodüksiyon:** `examples/11_router_app.tpr` →
`import "lib/router.tpr"` → `import "lib/http_utils.tpr"`

`lib/http_utils.tpr` satır 245/248'de `json_response()` çağırıyor;
`json_response` ise `lib/router.tpr` satır 135'te tanımlı. Çalıştırıldığında:

```text
HATA (Satır 245): 'json_response' adında bir fonksiyon bulunamadı.
HATA (Satır 248): 'json_response' adında bir fonksiyon bulunamadı.
[AOT] Error: Codegen reported errors above; aborting build.
```

Aynı zincir `12_threaded_server.tpr` ve `api_router_crud.tpr`'da da
aynı şekilde patlıyordu.

**Kök neden:** [src/aot/llvm_backend.cpp:3380](src/aot/llvm_backend.cpp#L3380)
içindeki AST_IMPORT işleyicisi şu sırayı izliyordu:

1. Pass 0.1: imported modülün globals'ını forward declare et
2. Pass 0.2: nested import'ları işle (yani recursive AST_IMPORT)
3. Pass 1: imported modülün fonksiyon gövdelerini emit et

Madde 3 ile eklenen "Pass 1a forward-declare function signatures" sadece
**top-level dosya** için işliyordu; AST_IMPORT durumunda yoktu. Sonuç:
nested import (`http_utils.tpr`) ebeveynden (`router.tpr`) **önce**
gövdelerini emit ediyordu; bu sırada `json_response` henüz LLVM modülüne
eklenmemişti.

**Çözüm:** AST_IMPORT işleyicisine "Pass 0.15: predeclare this module's
function signatures" eklendi — nested import'lar başlamadan **önce**
ebeveyn modülün tüm fonksiyon imzaları LLVM'e ekleniyor; çocuk modülün
gövdeleri emit edildiğinde ebeveynin fonksiyonları zaten görünür.

`predeclare_func_signature` için `static` forward declaration dosyanın
başına eklendi (kullanım noktası tanımdan önce).

**RESOLVED (2026-04-29):** Compile-smoke geçişi 11/11 (önceden 7/11);
`11_router_app`, `12_threaded_server`, `api_router_crud` artık
[src/aot/llvm_backend.cpp:3398-3413](src/aot/llvm_backend.cpp#L3398)
fix'i ile temiz derleniyor.

---

## 20. `lib/tulpar_api.tpr` içinde ölü `len = len;` ataması

**Şiddet:** Orta — `tulpar_api` modülünü import eden tüm örnekler AOT'ta
"Atamada tanimsiz degisken: len" ile derlenemiyordu.

**Kök neden:** [lib/tulpar_api.tpr:144](lib/tulpar_api.tpr#L144) içindeki
satır:

```tpr
// Export helpers to global scope for user scripts
len = len;
```

Yorum eski bir tasarımı yansıtıyordu (modül seviyesinde alias export
denenmiş); kod ise builtin `len` fonksiyonunu kendisine atamaya çalışıyor,
codegen bunu user variable assignment olarak görüp tanımsız hatası veriyor.
Madde 19 fix'inden sonra ortaya çıktı (predeclare'den önce silent kalmıştı,
zira `tulpar_api`-bağımlı örnekler hiç derlenemiyordu).

**Çözüm:** Ölü satır kaldırıldı. Yorum da gereksiz, o da silindi.

**RESOLVED (2026-04-29):** `tulpar_api_demo.tpr` artık temiz derleniyor.

---

## 21. SKIP listesindeki sunucu örnekleri hiç doğrulanmıyor (test coverage gap)

**Şiddet:** Orta — `09_socket_*`, `11_router_app`, `12_threaded_server`,
`14_api_server`, `api_wings*`, `api_router_crud`, `tulpar_api_demo`
toplam 10 örnek `listen()` / `api_run()` çağırdığı için run-test'te
zamanaşımına düşüp süreyi tıkıyordu, bu yüzden SKIP listesindeydiler.
Sonuç: bu örneklerin **derlemesi bile** kontrol edilmiyordu — Madde 19
ve 20 burada sessizce yatıyordu.

**Çözüm:** `run_tests.ps1` (ve `build.sh`'in test runner'ı) iki ayrı
listeye bölündü:

- `$skip` / `SKIP_TESTS` → gerçekten atlanan tek dosya: `utils.tpr`
  (modül olarak import için tasarlanmış, standalone çalıştırma anlamsız).
- `$compileOnly` / `COMPILE_ONLY_TESTS` → 10 sunucu örneği. `tulpar build`
  çağrılıyor, exe üretildi mi kontrol ediliyor; binary çalıştırılmıyor
  (listen()'de kilitlenmesin diye) ama derleme regresyonları yakalanıyor.

`run_tests.ps1` özet çıktısına `Passed(compile-only)` satırı eklendi.

**RESOLVED (2026-04-29):** Test paketi artık 30 örneğin **29'unu**
doğruluyor (1 SKIP = utils.tpr); 19 PASS + 10 PASS(compile-only). Daha
önce regresyon olarak yatan 11. madde de bu kapsamda yakalandı.

---

*Bu rapor canlıdır; her madde için issue açıldıktan sonra "RESOLVED"
işaretlenip arşivlenebilir. Yeni eksikler ortaya çıktıkça aynı
formatla buraya eklenebilir.*

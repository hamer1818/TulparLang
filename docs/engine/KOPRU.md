# Tulpar Engine Köprüsü — motor C++, oyun Tulpar (2026-09-15)

> Karar (kullanıcı, 2026-09-15): **motor C++ kalır, oyun betikleri Tulpar'da yazılır.** Bu belge o
> köprünün sözleşmesi, katmanları ve hata ayıklama yüzeyidir. Fizibilite tartışması: PLAN.md §11.

## 1. Neden köprü, neden bugün

PLAN §11 zaten "L5 gameplay Tulpar'dadır, aynı ikiliye linklenir, script sınırı yoktur" diyordu; eksik olan
onu taşıyan arayüzdü. Tulpar bugün kutusuz struct, işaretçi, atomik ve callback FFI vermiyor — bu yüzden
köprü **düz skalerlerle** konuşur: `int`, `double`, `const char*`. Struct yok, callback yok, sahiplik yok.
Motor tarafı bütün durumu kendi tutar; Tulpar tarafı **tamsayı tutamaçlarla** (varlık id'si) konuşur.

Ölçülen sonuç: Unity P/Invoke 20–100 ns, GDScript Variant 100–500 ns, burada **doğrudan çağrı** — Tulpar'ın
AOT kodu `aot_eng_*_ptr` sembolünü çağırır, o da `teng_*`i çağırır. İkisi de aynı ikilide.

## 2. Katmanlar (aşağıdan yukarı)

| katman | dosya | ne yapar |
|---|---|---|
| motor | `engine/` (L0–L6) | Vulkan, Jolt, renderer, içerik — değişmedi |
| C ABI | `engine/bridge/engine_api.h` | `teng_*`: düz skaler fonksiyonlar, tek global bağlam |
| çekirdek | `engine/bridge/engine_api.cpp` | durum, varlık tablosu, kare döngüsü, **log** |
| host | `engine/bridge/desktop_host.cpp`, `android_host.cpp` | pencere/yüzey/girdi; `BridgeHost` sözleşmesi |
| binding | `runtime/engine_bindings.cpp` (**üretilmiş**) | `aot_eng_*_ptr` (VMValue ABI) → `teng_*` |
| sarmalayıcı | `lib/engine.tpr` (gömülü, 386 satır) | `motor_ac`, `kutu`, `tus`, `yazi`, `dugme`, `kayit_*` … TR adlar, çoğunun EN ikizi (`engine_open`, `box`, `key`, `button`) |
| oyun | `examples/engine_ilk_oyun.tpr` (94), `engine_arena.tpr` (209), `engine_aksiyon.tpr` (1070) | saf Tulpar |

**Tek kaynak:** `engine/tools/gen_engine_bindings.py` içindeki `SPEC` tablosu. Bir komut dört dosya üretir:
binding (`runtime/engine_bindings.cpp`), backend tablosu (`src/aot/engine_builtins_table.inc`), typeinfer
imzaları (`src/typeinfer/engine_builtins_sigs.inc`), LSP girdileri (`src/lsp/engine_builtins.inc`).
CLAUDE.md'nin "5 noktada bağlama"sı burada **mekanik**: noktalar elle tutulmadığı için birbirinden kayamaz.
Yeni builtin = `SPEC`'e bir satır + `engine_api.h/.cpp`'de uygulama + `python3 engine/tools/gen_engine_bindings.py`.

## 3. Sözleşme

- **Yaşam döngüsü:** `eng_init` → `[eng_frame_begin … eng_frame_end]*` → `eng_shutdown`.
  `eng_frame_begin` false dönerse pencere yok (arka plan); **yine de** `eng_frame_end` çağrılır (sim sürer).
- **Varlık id'si** nesil etiketlidir: `(nesil<<16)|yuva`, 0 geçersiz. Silinmiş id ile çağrı **hata loglar**
  ve sessizce 0 döner — arcade'in öğrettiği kural (ham indeks asla dışarı sızmaz).
- **Renk** paketlenmiş int: `(r<<24)|(g<<16)|(b<<8)|a`. Tulpar'da bit kaydırma **yok**, `renk(r,g,b)`
  çarpmayla paketler (tame `rgb()` ile aynı).
- **Fizik:** kutu/küre gövdeleri Jolt'ta; `eng_set_pos` dinamik gövdede **gövdeyi yeniden kurar** (Jolt'ta
  konum yazma yok) ve hız sıfırlanır — log satırı bunu söyler.
- **HUD** kare içinde kuyruklanır (`eng_text`/`eng_rect`), `eng_frame_end` çizer. Kare dışında çağrı hata loglar.
- **Sahne:** `eng_scene_load("x.sahneb")` derlenmiş sahneyi (engine_sahnec / editörde **Derle**) yükler;
  modeller, ışıklar, gövdeler ve dünya ayarları oradan gelir. `eng_scene_unload` + yeniden yükleme =
  **bölüm geçişi**; `eng_scene_watch(1)` **sıcak yükleme** (dosya damgası izlenir, değişim görüldükten
  sonra bir kontrol daha beklenir ki yarım yazılmış blob yüklenmesin).
- **Çıktı parametresi yok:** Tulpar'da işaretçi/çıktı parametresi olmadığı için onay kutusu ve kaydırıcı
  **yeni değeri döndürür** ve betik geri yazar (`ses = kaydirici(..., ses, 0, 1)`).
- **Kalıcı kayıt motor kurulmadan da çalışır:** parlama gibi ayarlar `eng_init`'ten **önce** okunur.

## 4. Hata ayıklama yüzeyi (kullanıcı isteği: "olabildiğince her şeyi logla")

Her `teng_*` çağrısı log seviyesine göre basılır ve **her seviyede** 64 satırlık halkaya yazılır:

| seviye | `TULPAR_ENGINE_LOG` | ne çıkar |
|---|---|---|
| 0 | `0` | yalnız HATA (hata her zaman basılır) |
| 1 | `1` (varsayılan) | kurulum, sahne, kapanış raporu, betiğin `logla()`'ları |
| 2 | `2` | durum değişiklikleri: her spawn/despawn, kamera, ışık, swapchain, 120 karede bir kare raporu |
| 3 | `3` | **her API çağrısı** argümanlarıyla |

- Satır biçimi: `[engine_bridge] k<kare> <seviye> <mesaj>` — hangi karede olduğu her satırda var.
- Kapanışta hata varsa **halka dökülür** (`--- log halkasi (…; son cagri teng_x; hata 3) ---`): patlamadan
  önceki son 64 olay ve son API çağrısının adı. "Nerede patladı" sorusunun cevabı budur.
- Betikten: `logla("…")` → `[tpr]` önekiyle aynı akışa. (`log()` Tulpar'da **doğal logaritmadır**, ad çakışır.)
- Android'de aynı akış logcat'e (`tulpar` etiketi) ve `files/engine_log.txt`'ye gider.
- Pencere açılamazsa motor **headless'a düşer** ve bunu UYARI olarak yazar — betik yine koşar.

## 5. Doğrulama kipi (pencere açmadan)

```bash
TULPAR_ENGINE_HEADLESS=120 TULPAR_ENGINE_OUT=/tmp/oyun.ppm ./tulpar examples/engine_ilk_oyun.tpr
```

Pencere yerine offscreen render pass, N kare, son kare PPM. Kural aynı: **ben pencere açmam**, görsel
doğrulamayı kullanıcı ya da emülatör/cihaz ekran görüntüsü yapar (`no-raylib-windows-verify` ile aynı disiplin).

## 6. Android

`tulpar build --target=android oyun.tpr out --apk`. `import "engine"` gören AOT, tame yerine motor
arşivlerini bağlar (`engine_link_flags()` / android kolu). Giriş noktası: `android_host.cpp`'deki
`android_main` → Tulpar objesinin `main`i (raylib'in `rcore_android` deseni; sembol doğrudan bildirilir,
yoksa `-Wl,--no-undefined` **link'te** patlar, cihazda sessiz kalmaz).

Arşivler (gitignore'lu, bir kez üretilir):
```bash
android/build_tame_android.sh            # libtulpar_runtime_android.a (+tame)
engine/tools/build_bridge_android.sh     # libtulpar_engine_android.a + libengine_*.a
```
Font: APK varlığı yoksa `/system/fonts/Roboto-Regular.ttf` yedeği devreye girer; denenen her aday loglanır.

## 6.1 Editörde sahne, Tulpar'da oyun (döngünün kapanışı)

```bash
engine_editor --scene examples/assets/arena.sahne   # düzenle (kullanıcı açar)
engine_sahnec examples/assets/arena.sahne           # -> arena.sahneb (runtime blob)
./tulpar examples/engine_arena.tpr                  # oyna
```

`eng_scene_load` blob'u açar: modeller, ışıklar, gövdeler ve dünya ayarları (güneş, ortam, gölge hacmi)
sahneden gelir; **kamera betiğin** (oyun onu her kare sürer). Sahne varlıkları `eng_scene_find(ad)` ile
bulunur, konumları `eng_scene_x/y/z` ile okunur — dinamik gövdeler sim'den, sabitler yazar konumundan.
`examples/engine_arena.tpr` bunu kullanıyor: duvarlar, kasalar, ışıklar ve hedef şeridi sahneden, oyuncu
topu ile skor mantığı betikten.

`.sahneb` türetilmiş ve gitignore'lu olduğu için örnek, dosya yoksa aynı areni **kodda** kurar ve derleme
komutunu loglar. Böylece örnek her koşulda çalışır ve iki yolu da gösterir.

Android'de varlıklar APK'dan çıkarılır (alt dizinler dahil, JNI ile; Tuzaklar 8ag) ve çalışma dizini çıkarma
kökü yapılır: `examples/assets/arena.sahneb` yolu masaüstünde de cihazda da aynı şekilde çözülür.

## 7. Ölçülen (2026-09-15)

| ne | nerede | sonuç |
|---|---|---|
| ilk oyun, pencersiz | masaüstü RTX 5080 | 120 kare, 11 çizim, p50 13.1 ms, hata 0 |
| ilk oyun, cihaz | Android emülatörü (x86_64, GFXStream) | **60 fps** (p50 16.66 ms), 11 çizim, 11 gövde |
| dokunmatik | emülatör | sanal joystick yürüttü, sağ yarım tap `zipla` logladı |
| oyun mantığı | emülatör | kutu devrildi → **puan 1** (Tulpar tarafındaki skor döngüsü) |
| arena, cihaz | Android emülatörü | 59–60 fps, 13 çizim, 12 gövde, sahne APK'daki blob'dan (özet masaüstüyle aynı) |
| arena, oynanış | emülatör | kasa yeşil şeridi geçti, skor 1/6; 2880 karede **temiz kapanış**, 0 hata |
| C++ kapısı | `engine_tests` | `bridge_runs_a_scripted_game_headless` dahil; tam koşu **141/141, 0 atlandı** (2026-09-15 akşamı). Bu kapı kapanışta 14 hata logluyor ve yine de PASS: hepsi bilerek koşturulan hata yolları (ölü nesil, sabit gövdeye kuvvet, kare dışında çizim, bilinmeyen tuş, kapalı ses cihazı) |
| Tulpar kapısı | `tests/engine_bridge.test.tpr` | **17** test (kurulum, varlık, fizik, itme, ölçüm, hata sayacı, tuş adı, renk, ses, animasyon, sahne+bölüm, ışın, yakınlık, navmesh, arayüz, kalıcı kayıt, sıcak yükleme) |
| "Gölge Salonları" otopilotu | masaüstü, pencersiz | `kare=3200 bolum=2 gecis=1 oldurulen=9 kalan_dusman=0 can=26 skor=900 navmesh=true hata=0` (p50 12.9 ms) |
| ses, cihaz | Android emülatörü | AAudio açıldı, WAV klip APK'dan yüklendi, oyun 60 fps sürdü |
| parlama | masaüstü | açık/kapalı 26 857 piksel fark (arka plan eşitlendikten sonra) |

Kapıların kontrolleri: kutu **zeminli** y=0.480 / **zeminsiz** y=−15.13; sahne karesi ile boş kare arasında
37 402 piksel fark, **iki boş kare arasında 0** (yazıcı sabit çıktı vermiyor); ölü id çağrısı hata sayacını
**tam bir** artırır, canlı id **artırmaz**; geçersiz tuş adı hata, geçerli ad sessiz.

## 8. Kapsam: `SPEC` = `engine_api.h` = **156 builtin**

Sayı iki yerde birden durur ve birbirine karşı denetlenebilir: `engine/bridge/engine_api.h`'deki `teng_*`
bildirimleri ve `engine/tools/gen_engine_bindings.py`'deki `SPEC` satırları. Aile dağılımı (başlıktaki
bölüm yorumlarına göre):

| aile | adet | ne verir |
|---|---:|---|
| yaşam döngüsü | 21 | `eng_init` / `running` / `frame_begin` / `frame_end` / `shutdown`, dt, zaman, kare, fps, ölçü, pencersiz kip, **log ve log seviyesi**, ekran görüntüsü, GPU adı, son hata, **hata ve uyarı sayacı** |
| dünya / kamera | 11 | güneş, ortam, gölge hacmi, yerçekimi, **parlama (bloom)**, kamera (göz+hedef ya da yörünge), kamera konumu |
| derlenmiş sahne (`.sahneb`) | 15 | yükle / boşalt / yüklü mü (**bölüm geçişi**), sayı, ada göre bul, konum, ad, hız, dinamik mi, hız ver, dürtü |
| varlıklar (köprü sahibi) | 23 | kutu / küre / zemin / model / ışık üret, sil, canlı mı, konum, renk, ölçek, yaw, hız, dürtü, dinamik mi, uyanık mı |
| model animasyonu | 6 | klip sayısı / süresi / adı, varlığa klip ata (hız, döngü), klip zamanı, bitti mi |
| girdi | 12 | tuş basılı / bu karede basıldı, dokunmatik (sayı + konum), sanal joystick (x/y/eylem), bakış deltası, fare |
| 2B arayüz (HUD) | 3 | `eng_text`, `eng_rect`, `eng_text_width` — kare içinde kuyruklanır, `frame_end` çizer |
| anlık-kip arayüz | 12 | `ui_begin`/`ui_end`, tema, etkin/pasif, panel, etiket, **düğme**, **onay kutusu**, **kaydırıcı**, basılı mı, tıklama sayacı, pencersiz doğrulama için **enjekte tıklama** |
| kalıcı kayıt | 10 | dosya bağla, yol, sayı/metin yaz-oku, var mı, diske yaz, temizle, sayı |
| sahne sıcak yeniden yükleme | 6 | izlemeyi aç, yenilendi mi (bir kez), yenileme sayısı, yol, dosya kopyala, dosya değişim zamanı |
| ses | 13 | cihaz aç/kapat/durum/arka uç, klip yükle (WAV/FLAC/MP3), sentetik ton, çal, bip, durdur, hepsini durdur, ana seviye, çalan ses, tepe genlik |
| sorgular: ışın + yakınlık | 13 | `eng_raycast` (+ nokta / normal / çarpılan köprü varlığı / çarpılan sahne varlığı), **küre örtüşmesi** (yakından uzağa sıralı), en yakın |
| navmesh (blob'daki bake) | 7 | bake var mı, poligon sayısı, yol iste, yol kısmi mi, yol noktaları |
| ölçüm | 4 | çizim / gövde / ışık sayısı, son kare p50 |

**Ne verilmez (bilinçli):** struct, callback, işaretçi, çıktı parametresi — Tulpar'ın bugünkü FFI'si
taşımıyor. Bunun iki görünür sonucu var: (1) **çarpışma olayı yok** — savaş ve yapay zekâ ışın + küre
sorgusuyla yazılır; (2) onay kutusu ve kaydırıcı **yeni değeri döndürür**, betik geri yazar.

### 8.1 Bir oyunun tam yaşam döngüsü (gerçek çağrı adlarıyla)

`lib/engine.tpr` sarmalayıcıları ve `examples/engine_aksiyon.tpr`'nin akışı:

```tulpar
import "engine";

func main() {
    // 1) Kurulumdan ÖNCE: kalıcı ayarlar okunur, parlama burada açılır
    //    (iç HDR hedefi kurulumda kurulur; kare içinde yalnız eşik/yoğunluk değişir).
    kayit_ac("oyunum_kayit.txt");                  // boş yol HATA; hiç çağırmazsan varsayılan
    float ses_ayari = kayit_oku_sayi("ses", 0.7);  // (TULPAR_ENGINE_SAVE ya da tulpar_kayit.txt)
    parlama(true, 1.0, 0.55);
    if (!motor_ac("Oyunum", 1280, 720)) { print(eng_last_error()); return; }

    // 2) Dünya: ya kodda kurulur, ya editörde hazırlanmış blob'dan gelir.
    gunes(0.4, 1.0, 0.3, 0.9);
    ortam(renk(38, 42, 52));
    if (!sahne_yukle("examples/assets/salon1.sahneb")) { zemin_kur(20, ACIK_GRI); }
    sahne_izle(true);                              // editörde "Derle" -> oyun kendini yeniler
    int oyuncu = kure(0, 1, 0, 0.5, true, KIRMIZI);
    if (ses_var()) { ses_seviyesi(ses_ayari); }

    // 3) Kare döngüsü
    while (calisiyor()) {
        kare_basla();
        float d = dt();
        if (sahne_yenilendi()) { logla("sahne yenilendi"); }

        // girdi: klavye VEYA sanal joystick (yon_x/yon_y ikisini de okur)
        yuru(oyuncu, yon_x() * 5.0, -yon_y() * 5.0);
        if (eylem_basildi() && yerde(oyuncu)) { itme(oyuncu, 0, 6, 0); }

        // sorgu tabanlı oynanış: önde küre sorgusu = vuruş
        int n = yakinlar(konum_x(oyuncu), konum_y(oyuncu), konum_z(oyuncu), 2.0, oyuncu);
        int i = 0;
        while (i < n) { itme(yakin_id(i), 0, 3, 0); i = i + 1; }

        // kamera + HUD
        kamera_takip(konum_x(oyuncu), konum_y(oyuncu), konum_z(oyuncu), 9.0, 4.0, 0.0);
        yazi("skor 0", 16, 16, 2.0, BEYAZ);

        // menü/ayar: anlık-kip arayüz (aynı kare içinde)
        ui_basla();
        if (dugme("Duraklat", 16, 60, 140, 40)) { logla("duraklat"); }
        ses_ayari = kaydirici("Ses", 16, 110, 200, 28, ses_ayari, 0.0, 1.0);
        ui_bitir();

        kare_bitir();
    }

    // 4) Kapanış: ayar + rekor diske, sonra motor
    kayit_yaz_sayi("ses", ses_ayari);
    kayit_kaydet();
    motor_kapat();
}
```

Pencersiz doğrulama aynı betikle: `TULPAR_ENGINE_HEADLESS=600 TULPAR_ENGINE_OUT=/tmp/x.ppm DISPLAY= ./tulpar oyun.tpr`
(menü akışı da ölçülsün diye `ui_test_tikla` ile enjekte tıklama; "Gölge Salonları" böyle koşuyor).

### 8.2 Hâlâ olmayanlar
- **Çarpışma olayı yok** (konum/hız/ışın/küre sorgusu var); callback FFI gelmeden geri çağrı yok.
- **Ajan / yol takibi köprüde yok**: navmesh **sorgusu** var (`eng_nav_*`, blob'daki bake), ama motorun
  ajan sistemi (kalabalık, kaçınma) dışarı verilmiyor — yolu betik kendisi takip eder.
- Tek motor örneği (global bağlam): iki pencere / iki dünya yok, gerekmedi.
- Kaynak (model) 16, varlık 4096, ses klibi ve arayüz durumu sabit kapasiteli — diziler sabit, 0 ayırma.
- Parlama açıkken pencere yeniden boyutlanırsa iç hedef init ölçüsünde kalır (birleştirme gerilir).

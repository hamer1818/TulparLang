# TulparLang — Durum ve Yol Haritası

> **Motto:** Python kadar kolay, C kadar hızlı — Genel amaçlı dil.

Bu dosya, projenin tek "ne durumda?" referansıdır. Eski `EKSIKLER.md`
(60 madde, hepsi RESOLVED) ve `OZET.md` (faz-bazlı tarihçe) bu dosyada
toplandı. Yeni eksiklikler buradaki **Açık eksikler** bölümüne eklenir;
çözülen iş **Yapılanlar** altına özet bir satırla taşınır.

---

## 📊 Mevcut durum (özet)

- **Çekirdek sözdizimi tam:** statik tipler, fonksiyonlar (opsiyonel
  dönüş-tipi notasyonu, **eksik trailing arg → 0 ile padlenir**), kontrol akışı,
  modüller (`import "x" as alias`), **t-string interpolation** (`t"x={n}"`),
  **ternary** (`cond ? a : b` — tembel, sağ-asosiyatif, en gevşek öncelik),
  coerce-eden `+`, `let`/`var`, for-each (`for (x in coll)`), string escape
  `\e` (ESC, ANSI renkleri için) + `\0`.
- **Tek backend — AOT-only (2026-06-15):** LLVM 18 AOT tek yürütme yolu
  (C/Rust/Go modeli). Bytecode VM yorumlayıcısı (`vm_run`), bytecode
  derleyicisi (`compiler.cpp`) ve REPL **kaldırıldı**; `--vm`/`--run`
  yok sayılır (uyarı), `--repl` çıkış verir. AOT hatası = sert hata
  (VM yedeği yok). `src/vm/` artık yalnızca **paylaşılan runtime**:
  `runtime_bindings.cpp` (aot_* builtin'leri), `vm.cpp` (arena +
  allocator'lar), `vm.hpp` (VMValue/Obj tipleri). Tree-walk interpreter
  ve x64 JIT zaten 2026-05'te sunset edilmişti. **Yeni dil özelliği
  yalnızca AOT'ta yazılır — VM/bytecode paritesi aranmaz.**
- **Cross-platform:** Windows (Inno Setup installer), Linux, macOS,
  WASM. `libtulpar_runtime.a` AOT'a static link.
- **Stdlib gömülü:** wings, router, http_utils, http_client, async,
  middleware, socket, tulpar_api, orm, test, wings_tls, tame. SQLite
  ve raylib vendored.
- **Tame — 2D oyun kütüphanesi (v3.10.0, 2026-07-12, Faz 0-5):**
  `import "tame"` ile saf Tulpar'dan pencere + şekil/yazı çizimi +
  klavye/fare/**gamepad** girdisi + **sprite/texture + TTF font + ses/müzik +
  `run(update, draw)` yönetilen döngüsü** — Pong sınıfı oyun ~40 satır.
  Native katman vendored **raylib 5.5** (`lib/raylib/`) + 49 `aot_tm_*`
  builtin'i **ayrı** `libtulpar_tame.a` arşivinde; link satırına yalnız
  tame import edilince eklenir → sıradan binary'ler GL/pencere bağımlılığı
  almaz. Gömülü `lib/tame.tpr` kısa isimler, `rgb()/rgba()` + adlı raylib
  paleti, kaynak handle'ları (`load_texture/load_sound/load_music`,
  DB-handle deseni), `screenshot()` ve oyun yardımcıları sunar; `run()`
  kare belleğini arena bracket'iyle sabitler. Headless'ta zarif hata.
  **Pencereli canlı doğrulama yapıldı** (WSLg): tüm çizim türleri + sprite
  + ölçek/döndürme ekran görüntüsüyle piksel-kanıtlı, 60 FPS pacing,
  `run()` 480 kare stabil. **Web (WASM) hedefi de tarayıcıda canlı doğrulandı
  (2026-07-14): mini/sprite/run örnekleri 60 FPS; `run()`/`call()` web'de
  bozuktu — dlsym yerine `aot_register_func` + arity'li dağıtımla düzeltildi.**
  Örnekler: `tame_hello`, `tame_sprite_demo`, `tame_run_demo`, `tame_web_mini`.
  Detay: Stdlib + tooling bölümü.
- **Arcade — "preset" üst-seviye oyun motoru (v3.10.0, 2026-07-14):**
  `import "arcade"` — tame'in üstünde, Unreal-blueprint/blok mantığıyla **çok
  kısa kodla** oyun yaptıran katman (gömülü `lib/arcade.tpr`, transitively
  `import "tame"` → tame linki otomatik). Kullanıcı döngü/çarpışma/fizik/çizim
  YAZMAZ; hazır blokları birleştirir: `oyuncu()/player`, `dusman()/enemy`,
  `esya()/item`, `duvar()/wall`, `mermi()/bullet`, `oyuncu_p()` (platformer),
  hareket presetleri (topdown/platform/velocity), `carpisinca(tagA,tagB,fn)`
  tag tabanlı 0-arg çarpışma callback'i (`me()/other()` context'i), `yercekimi()`
  + zemin/platforma iniş, `baslangicta()/her_kare()/ciz_ustune()` kancaları,
  otomatik skor HUD + oyun-bitti/R-yeniden ekranı, `oyna()/play()` yönetilen
  döngü. **Her API çift dilli (TR+EN alias).** Ekrandan çıkan velocity
  entity'leri otomatik ölür. Motor **paralel dizi** tabanlı — bu iki dil
  kısıtından doğmuştu ama **ikisi de 2026-07-20'de düzeltildi:** artık dizideki
  int/bool struct'a alan geri-yazma çalışıyor (`box_native_struct_as_object`)
  ve `call(fn, a, b, …)` N-argüman geçiyor (`aot_call_dynamic_n`), yani
  callback'ler 0-arg olmak zorunda değil. (Arcade bu düzeltmelerden önce
  yazıldığından hâlâ paralel dizi kullanıyor; yeni bir motor `struct Ent`
  dizisi + arg'lı callback kullanabilir.) Çekirdek headless doğrulandı (`step()` API'siyle: velocity
  hareket + AABB çarpışma dispatch + kill/skor + yerçekimi+zemine iniş) ve
  pencereli render ekran görüntüsüyle kanıtlandı (WSLg). Örnekler:
  `arcade_topla` (toplama+kaç), `arcade_zipla` (platformer), `arcade_nisan`
  (atış) — hepsi ~15-25 satır oyun kodu, COMPILE_ONLY.
  Fizik + duvarlar (2026-07-16): `yercekimi/gravity` + `ziplama/jump_power` +
  **katı `duvar()` çarpışması** (AABB: duvara dayanma/yan-engelleme/platforma
  iniş; çözüm aşağıdaki MTV) + dünya-zemini/sınırları — hepsi
  deterministik çalışıyor (headless kanıt: p duvar tepesine y=80, q platforma
  y=180; çoklu-entity + on_frame poison + 3/3 tekrar). `arcade_zipla` gerçek
  platformer (zemin + 2 platform + zıplayıp hedefe ulaş). Hareket döngüsü global
  index kullanır (param-index de çalışır). Çarpışma çözümü **MTV (minimum
  translation vector)**: her çakışan duvar için en sığ eksende itilir — bu,
  tam-genişlik zemin platformunda yatay hareketin cismi platform kenarına
  ışınlaması bug'ını çözer (eksen-ayrık yaklaşımın klasik hatası). Büyük ilk-kare
  dt'si `_ar_update`'te 0.05'e sınırlanır (tünelleme koruması). HUD: skorun
  altında `bilgi()/controls()` ile kontrol bilgilendirme şeridi (3 örnekte de
  var). Not: oturum içinde önce bir "codegen bug'ı" sanılıp duvarlar geri
  çekilmişti — o iddia ÖYLE DEĞİLDİ (neden: bayat WSL build'i + repro fizik
  hatası); duvarlar geri getirildi.
  **Yatay hareket düzeltmesi (2026-07-17):** Kullanıcı "zıplama tamam ama sağa
  sola gidemiyorum" dedi. Neden arcade'de değil, **derleyicideydi**:
  `_ar_apply_movement`'ta MV_TOPDOWN ve MV_PLATFORM dalları aynı adlı yerelleri
  (`float s`, `float vx`) tanımlıyor; `if` blokları scope açmadığı için ikinci
  tanımın başlatıcısı düşüyor ve `s`/`vx` **yazılmamış yığın çöpü** okuyordu
  (zıplama globalleri kullandığı için çalışıyordu — semptom tam da buydu).
  Teşhis: XTest ile pencereye tuş enjekte edilip motor stdout'a döküldü
  (`s=140730080902816`, `evx=448`, oyuncu x=612'ye yapışıyor). Derleyici
  düzeltildi (aşağıya bak); arcade kodu değişmedi. Doğrulama: tuşsuz `evx=0` +
  y=420'de duruyor, SAĞ basılıyken `evx=200` ve x düzgün ilerliyor.
  **WASM (2026-07-17): arcade web'de ÇALIŞIYOR — kod değişikliği gerekmedi.**
  `tulpar build --target=web examples/arcade_zipla.tpr out` → `.html/.js/.wasm`;
  üç örnek de (zipla/topla/nişan) tarayıcıda doğru render ediyor (headless
  Chrome + swiftshader ekran görüntüsüyle kanıtlı: HUD + kontrol şeridi +
  platformlar + entity'ler). CDP ile konsol okunup tuş enjekte edilerek
  **davranış** da doğrulandı: boştayken `evx=0` ve oyuncu y=420'de duruyor;
  ArrowRight basılıyken `R=1`, `evx=200`, x 93→326 düzgün ilerliyor (yani
  `key_down` web'de çalışıyor); çarpışma callback'i tam bir kez tetikleniyor
  (`skor=100`, `oldur(oteki())` eşyayı öldürüyor → `me()/other()` context'i
  web'de de doğru); JS hatası yok. Yani `call()` tabanlı kanca + registry
  deseninin tamamı web'de sağlam.
  **Slot geri kazanımı + handle'lar (2026-07-17, son açık madde kapandı):** ölen
  entity'nin slot'u artık boş-slot yığınına dönüp yeniden kullanılıyor —
  sürekli mermi üreten oyunda paralel diziler sonsuza dek büyümüyor (kanıt:
  600 karede ~133 entity üretilen nişan oyununda `_n` 22'de sabitleniyor; 500
  üret+öldür sonrası `_n=2`). Naif index geri-kullanımının tuzağı (ölmüş bir
  id'yi tutan kodun sessizce BAŞKA entity'yi oynatması) **generation'lı handle**
  ile kapatıldı: dışarıya verilen id = `_egen[slot] * 2^20 + slot`; öldürmede
  `_egen` artar → o slot'a ait eski handle'lar anında bayatlar. Tüm public id
  API'si `_slot_of()` ile çözer; bayat handle sessizce yok sayılır (setter no-op,
  getter nötr, `yasiyor()` dürüstçe false). Kanıt: bayat handle'la `konumla`
  yeni entity'yi bozmuyor, bayat `oldur` onu öldürmüyor. `entity_sayisi()` =
  ayrılmış slot (canlı değil); yeni `canli_sayisi()/live_count()` canlıyı verir.
  Web'de de doğrulandı (360 karede `_n=9` sabit, bayat tespiti wasm'de çalışıyor).
  **Bölüm (level) sistemi (2026-07-17):** motora çift dilli bölüm API'si eklendi —
  `bolum(n, fn)/level`, `bolum_gec()/next_level`, `bolum_no()/level_no`,
  `bolum_sayisi()/level_count`, `kazandin_mi()/is_won` + tag bazlı sayım
  `tag_sayisi(tag)/tag_count` ve hız okuma `vx_of()/vy_of()`. Semantik: her oyunda
  yeniden yazılmaz, motorda; `_ar_state` **2 = kazandın** ile genişledi (0 oynanıyor,
  1 bitti); bölüm yüklemesi `clear_entities()` → global `baslangicta` fn'i → o bölümün
  fn'i sırasıyla; **skor bölümler arası korunur**, R ise skoru sıfırlayıp bölüm 1'e
  döner (en tahmin edilebilir davranış); HUD'da sağ üstte `Bolum n/N` göstergesi +
  geçişte ~1.2 sn "Bölüm N" afişi + KAZANDIN ekranı (toplam skor). `is_over()/bitti_mi()`
  artık `state != 0` (kazanmak da bitiştir) — bölüm kaydı olmayan oyunlarda state 2 hiç
  oluşmadığı için **geriye tam uyumlu**. **Kritik tasarım:** `bolum_gec()` geçişi hemen
  YAPMAZ, `_lvl_pending` ile işaretler ve geçiş kare sonunda (`_ar_update`/`step`)
  uygulanır — çünkü tipik çağrı yeri bir çarpışma callback'idir ve `_ar_collisions()`
  o sırada paralel dizilerin üstünde döner; hemen `clear_entities()` çağırmak altındaki
  diziyi değiştirirdi. Kayıtlar `temizle()` ile silinmez (kurallar gibi, oyun tanımının
  parçası). Kanıt: `tests/arcade_levels.test.tpr` (8/8, headless `adim()` ile: kayıt,
  1→2→3 ilerleme, her bölümün kendi düzeni, skor korunumu, son bölümde KAZANDIN +
  fizik durur, R sonrası skor 0/bölüm 1, 60 yükleme sonrası slot şişmemesi `_n=4`).
  Yan düzeltme: `arcade_topla` düşman sekmesi `_evx[enemy_id]` ile **handle**'ı slot
  indeksi sanıp dizi dışını okuyordu → yeni `vx_of()/vy_of()` getter'ları ile düzeltildi.
  **4 oyunun 3'er bölümü (2026-07-17):** `arcade_zipla` (kolay → zikzak tırmanış →
  dar platform + gidip gelen ölümcül engel; düzenler zıplama payına göre ≤80px dikey /
  ≤140px yatay açıklıkla kuruldu), `arcade_topla` (3 eşya/1 düşman → 5/2 → 6/3 hızlı;
  bölüm bitişi `tag_sayisi(TAG_ITEM)==0`), `arcade_nisan` (kümülatif hedef skor 30 →
  60 → 100, tempo/hız artar), `tame_snake` (arcade DEĞİL, doğrudan tame → bölüm akışı
  elle: bölüm başına 5 yem, `MOVE_FRAMES` 8→6→4, 3. bölümde engel duvarları + KAZANDIN).
  4'ü de web'e derlendi (`web_demo/`), headless Chrome + CDP ile render + HUD doğrulandı
  (yılanda tuş enjekte edilip oyunun gerçekten başladığı/hareket ettiği görüldü).
  **4 oyun daha (2026-07-17, kullanıcı isteği) — her biri motorun farklı bir yanını sürer:**
  `arcade_tugla` (Breakout: tuğlalar `esya()`, top TAG_BULLET+MV_VELOCITY, raket MV_NONE +
  `her_kare`'de yatay sürülür — MV_TOPDOWN 4 yöne giderdi; düz duvar → piramit → boşluklu
  duvar), `arcade_uzay` (Space Invaders: filo tek parça hareket ettiği için düşmanlar
  MV_NONE + elle sürülür; 3x5 → 4x6 → 4x7 + filo geri ateş eder, düşman mermisi için özel
  tag `6`), `arcade_labirent` (harita ASCII satırlar + `ord()` ile okunur: `#/P/A/E` →
  bölüm eklemek = 15 string yazmak; oyuncu MV_TOPDOWN → motorun MTV duvar çözümünü kullanır,
  devriyeler MV_VELOCITY duvar takmadığı için koridor sınırları kurulumda harita taranarak
  hesaplanır), `arcade_karsiya` (Frogger: MV_VELOCITY şeritler ±660'ta sarılır — motorun
  "dünyanın 64px dışında otomatik öldür" kuralından ÖNCE sarmak zorunlu). Dördü de
  COMPILE_ONLY listelerinde (build.sh + run_tests.ps1), web'e derli ve tarayıcıda
  doğrulandı. **Bu turda yakalanan iki tuzak:** (1) `don` REZERVE kelime (`return`'ün
  Türkçesi) — `bool don` parse hatası verdi (`move` gibi, ama bu sessiz değil sert hata);
  (2) tam-ekran oyun alanı HUD ile çakışıyor — labirent 32px hücreyle tüm ekranı kaplayıp
  oyuncuyu bilgi şeridinin altında bırakıyordu (28px hücre + OX=40/OY=60 ile çözüldü),
  karşıya'nın altın hedef bandı skor yazısının altında kalıyordu (y=10 → y=58).
  Labirent haritaları flood-fill ile doğrulandı (P'den tüm A'lara ulaşım + devriye
  koridoru ≥3 hücre); doğrulayıcı M1'de ulaşılamayan anahtar, M2/M3'te 2 ve 1 hücrelik
  sıkışık devriye koridoru yakaladı — üçü de düzeltildi.
  **İki dillilik (2026-07-17, kullanıcı isteği — tulparlang.dev hem TR hem EN yayın yapıyor):**
  (1) **Motor HUD'u çift dilli** — `dil()/language()` ile motorun çizdiği metinler (skor öneki,
  `Bolum n/N`/`Level n/N`, afiş, OYUN BITTI/GAME OVER, KAZANDIN/YOU WIN, R ipucu) TR ya da EN
  çizilir; varsayılan sistem dili (`sys_lang()`), TR eski çıktının bit-bit aynısı → geriye tam
  uyumlu. `bilgi()/controls()` şeridi oyuna ait. (2) **8 oyunun tam İngilizce ikizi** (`examples/en/`:
  jump/collect/shooter/snake/breakout/invaders/maze/crossing) — İngilizce API alias'ları + yorum +
  başlık + `language("en")`; `examples/*.tpr` koşucusu ikizleri saymasın diye alt dizinde,
  derleme+ekran görüntüsüyle doğrulandı. (3) **`web_demo` iki dilli** — `index.html` (oyun
  kaynaklarından bir üretici betikle üretilir → senkron kalır) sayfa düzeyinde TR/EN anahtarı,
  oyun başına Oyna(TR)/Oyna(EN) (iki dil sürümü de yayınlı: `zipla.html`+`jump.html` …) ve gömülü
  gerçek kaynağı gösteren TR/EN sekmeli "Kodu gör" paneli içerir. CDP ile etkileşim doğrulandı
  (EN'e geç → kartı aç → EN sekmesi `language("en")`li İngilizce kaynağı gösteriyor). Not: lokal
  işçiye (qwen2.5-coder:14b) 3 ikizin mekanik çevirisi verildi ama çıktısı bozuktu (`bolum_gec`
  düşmüş, `oyun_biti` yazım hatası, `start/frame/collision` uydurma API, string'ler çevrilmemiş)
  → doğrulama kapısında yakalanıp 8 ikiz de elle yazıldı.
- **AOT codegen doğruluk düzeltmeleri (v3.10.0, 2026-07-17):** Arcade'in yatay
  hareket hatası iki **gerçek** derleyici bug'ını ortaya çıkardı; ikisi de
  sıradan tipli Tulpar kodunu sessizce bozuyordu:
  - **Kardeş blokta yeniden tanım başlatıcıyı kaybediyordu.** `if`/`else`
    blokları scope AÇMAZ (yalnız fonksiyon/lambda/`for`/main açar) → aynı adlı
    iki tanım aynı scope'a düşüyor; `add_local` ikinci bir kayıt EKLİYOR, arama
    ise baştan tarayıp İLK kaydı buluyordu. Sonuç: ikinci tanımın store'u
    okunmayan bir alloca'ya gidiyor; okuma ilk dalın değerini (o dal hiç
    çalışmadıysa yığın çöpünü) görüyordu. Artık tanım mevcut slot'u yeniden
    kullanıyor → **en son tanım kazanır**. `tests/scope_redecl.test.tpr`.
  - **Native (tümü-int) hızlı yolu sessizce yanlış kod üretiyordu.**
    `func f(int p): int` fonksiyonları `codegen_native_func_def`'in elle yazılmış
    i64 alt-küme emitter'ına giriyor; bu emitter **case'i olmayan deyimi hatasız
    ATIYOR**. Kapısı (`native_codegen_supports_stmt`) emitter'dan çok daha
    genişti: (a) `if/else`'in **else dalı hiç emit edilmiyordu** → else'e düşen
    her girdi **0** dönüyordu; (b) `if (c) { int a = 1; return a; }` tanımı
    atıyor, `a` kayıtsız kalıyor ve **derleyici çöküyordu**; (c) `while`/`for`
    gövdesindeki iç içe `if` atılıyordu (gövde yalnız atama/tanım emit ediyor) →
    birikim döngüleri yanlış sonuç veriyordu. Kapı artık emitter ile birebir;
    karşılanmayan biçimler tam dili işleyen kutulu (boxed) yola düşer. Korumalı
    özyineleme (`fib`'deki `if (n < 2) { return n; }`) native kalır.
    `tests/native_fastpath.test.tpr`. **Ders:** bu yolda kapıyı emitter'dan
    geniş tutmak = teşhissiz yanlış kod; şüphede 0 döndür.
  - **Native döngü gövdesinde builtin çağrısı sessizce ATILIYORDU** (2026-07-03'ten
    beri açık olan bug — nihayet kapandı). Native yerel i64'tür ama
    `codegen_typed_expr` statik-int olmayan (tipik olarak çağrı içeren) ifade
    için **kutulu** VMValue döner; while/for gövde emitter'ı yalnız
    INFERRED_INT/BOOL durumunu store edip gerisini teşhissiz atıyordu → store
    hiç olmuyordu: `toplam = toplam + mod(n,10)` birikimi **0** kalıyor;
    `while (b != 0) { b = mod(a,b); ... }` (Euclid OBEB) b'yi güncellemeyip
    **segfault**; `n = toInt(n/10)` n'i güncellemeyip **sonsuz döngü**. Artık
    gövde de payload'ı (alan 2) açıyor — döngü **koşulunun** zaten yaptığı şey.
    Bu, "fonksiyon içinde while + builtin çağrısı bozuk, `for`/özyineleme kullan"
    diye taşınan eski workaround'un ta kendisiydi; **artık gerekmiyor**.
- **Wings/ORM DX turu (v3.7.0, 2026-07-03):** [WINGS_DX.md](WINGS_DX.md)
  çalışmasının tamamı uygulandı — **ORM v2** (model handle + UFCS:
  `Note.find(1)`, `Note.where("done = ?", [0])`, `Note.save(obj)`; her SQL
  parametreli; `bool` kolon cast'i; v1'in 0/""/false-düşürme bug'ı kapandı;
  `orm_*` eski adlar sarmalayıcı), **`resource("/notes", Note)`** (5 CRUD
  route + türetilmiş 422 şeması + /docs — kalıcı CRUD API **7 satır**,
  `examples/wings_orm_resource.tpr`), **`serve(port, workers)`** tek kapı,
  **kısa isimler** (`cookies/ws_*/sse_*/metrics_prom/gzip/delete/accepts/
  returns` — eskiler kırılmadı), LSP tablosuna DX + tüm ORM sembolleri.
  `tests/orm.test.tpr` 11/11, `tests/wings_dx.test.tpr` 10/10; canlı curl
  ile uçtan uca + restart-kalıcılık doğrulandı. Cheatsheet:
  [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md).
- **Wings tamamlık turu (v3.6.0, 2026-07-02):** HANDOFF §2'deki tüm boşluklar
  kapandı — çerez SET tarafı (`set_cookie`/`delete_cookie`), **imzalı çerezler**
  (`set_signed_cookie`/`get_signed_cookie`, HMAC name-bound), **sunucu-taraflı
  session** (`session_start/attach/get/set/destroy`), **yapılandırılabilir CORS**
  (`cors(origin, {credentials,…})` — credentialed origin artık mümkün),
  **rate-limit middleware** (`rate_limit(max, window)` → 429+Retry-After),
  **JWT guard** (`jwt_guard(secret)` + `jwt_public`, wings_jwt formatıyla
  birebir interop, claims → `req["jwt"]`), **html()/render()** template,
  **tipli path param** (`param`/`param_int`/`param_bool`), **ETag→304**
  (`cached_get` üstünde `If-None-Match`), **gzip yanıt sıkıştırma**
  (`enable_gzip` + yeni `gzip_compress` builtin'i — ağaç-içi DEFLATE,
  zlib'siz) ve **OpenAPI tamamlama** (response_model → 200 şeması,
  bearerAuth security scheme). `tests/wings_features.test.tpr` 13/13;
  `examples/wings_features_api.tpr` canlı curl ile uçtan uca doğrulandı.
- **Wings ergonomisi (FastAPI seviyesi):** fonksiyon-ref handler'lar
  (`get("/users", list_users)`, typo=derleme hatası), `req` parametresi
  (`req.params.id`, `req.json`), response helper'ları (`ok/created/not_found/…`),
  otomatik body parse, **görünmez auto-persist** (global'e yazım manuel
  `persist()`'siz kalıcı — array `push` **ve** global dict key-write dâhil,
  2026-06-23), **temiz cevap gövdesi** (envelope meta-anahtarları `_status`/
  `_raw`/… JSON gövdeye sızmaz; `created(x)`→`{"id":1}` + `201`), **şema
  doğrulama** (`body_schema({"name":"str"})` →
  geçersiz gövde handler'a girmeden otomatik 422), **otomatik /docs** (Swagger
  UI + /openapi.json, route'lardan + şemalardan üretilir) ve **markalı varsayılan
  port** — `serve()` (port'suz) → 8484 (ASCII 'T'=84, "Tulpar portu"), doluysa
  otomatik +1; `serve(8080)` açık port → doluysa kullanıcıyı bilgilendirir.
  Karşılaştırma: `tests/compare_{wings,fastapi}_users_api` +
  `benchmarks/WINGS_VS_FASTAPI.md`.
- **async/await v1 tam:** stackful coroutine + event loop, `sleep_async`
  (non-blocking timer), `gather(...)` (eşzamanlı), **gerçek async I/O** —
  non-blocking HTTP client (`http_request_async`, worker pool) — ve `reject/try`
  hata yayılımı (coroutine-aware exception context). 16 parametreye kadar async
  fn. Detay: Açık eksikler → Dil seviyesi.
- **Pkg manager:** `init/add/install/publish` + semver (`^`, `~`,
  `>=,<`) + lockfile + `.tpkg` multi-file bundle. Canlı registry
  `api.pkg.tulparlang.dev` (tulpar-be, Tulpar'da yazılmış). İlk gerçek
  paket **`wings_jwt` canlı yayınlandı (2026-07-04)** —
  `tulpar pkg add wings_jwt` ile kurulabilir.
- **Güvenlik primitifleri:** `sha256`, **`hmac_sha256`** (RFC 2104, v3.5.0),
  `password_hash`/`verify` (PBKDF2), `secure_token` (CSPRNG), parametreli SQL —
  hepsi ağaç-içi (OpenSSL'siz).
- **Tooling:** `tulpar fmt`, `tulpar --lsp`, `tulpar typecheck`,
  `tulpar update`. VS Code eklentisi (`vscode-tulpar` v0.3+) LSP
  client.
- **Performans:** AOT loopsum ~1.9 ms (C'nin 3.17×ı), fib(35) ~32 ms
  (C'nin 1.87×ı, Rust seviyesinde). HTTP Wings ~22-26k rps CI Linux,
  Node.js'i 1.7-2.1× geçiyor. Bench auto-refresh CI'da çalışıyor,
  README + RESULTS.md her commit'te güncelleniyor.

---

## ✅ Yapılanlar

### Çekirdek dil + derleme zinciri

- **Cila turu — diagnostik + CLI gürültüsü (2026-06-29):** iki "bilinen ufak
  konu" kapatıldı (detay → Notlar):
  - ✅ **Parse error satır no off-by-one (EOF).** Kapanmamış ifade EOF'a
    çarpınca raporlanan satır içerik taşıyan son satıra clamp ediliyor
    (`render_parse_error_pretty` + yeni `clamp_line_to_source`); lexer'a
    dokunulmadı, LSP sink de aynı clamp'i kullanıyor.
  - ✅ **`tulpar build` artık sessiz.** Ara `[AOT]` progress satırları
    `TULPAR_AOT_VERBOSE=1` arkasına alındı; varsayılan build yalnız son
    "Successfully created" + hataları basıyor.
  - 🔍 **String `==` incelendi → gerçek bug değil** (cross-type coerce yok,
    tutarlı). Regresyon yok: örnekler + `errors/ternary/match/strings`
    suite'leri yeşil. → [[AOT Backend]]
- **AOT fonksiyon tablosu 128 → 512 + sessiz taşma yerine sert hata (v3.2.1, 2026-06-24):**
  `LLVMBackend.functions[]` 128 slotluk sabit diziydi; büyük bir stdlib modülü
  (ör. `wings`) + kullanıcının kendi handler'ları bu sınırı aşınca
  `register_function` kaydı **sessizce düşürüyor**, sonra o fonksiyona yapılan
  çağrı codegen'de null-deref ile segfault veriyordu (teşhissiz). Kapasite 512'ye
  çıkarıldı ve taşmada artık `[AOT] Fatal: function table overflow` ile yüksek
  sesle çıkılıyor (yanlış derleme yerine). Gerçek dünya tetikleyicisi: iurl
  link-kısaltıcı API'si (`wings` + ~bir düzine route handler).
  - ✅ **Tam zincir eklendi** — lexer `TOKEN_QUESTION` (`?`), AST `TernaryOp`
    (+ variant + visitor + converter → C-tarafı `AST_TERNARY`, if-node'unun
    condition/then/else slotlarını yeniden kullanır), typeinfer (iki dalı unify
    eder), AOT codegen `AST_TERNARY` (değer üreten ifade: koşul → CondBr →
    iki blok res_slot'a yazar → merge load; AST_MATCH phi kalıbıyla aynı).
  - ✅ **Tembel (lazy) semantik:** yalnızca seçilen dal değerlendirilir —
    seçilmeyen dalın yan etkisi (fonksiyon çağrısı vb.) tetiklenmez. Birim
    testle kanıtlandı (global sayaç: untaken dal sayacı 0 kalır).
  - ✅ **Öncelik + asosiyatiflik:** en gevşek operatör (binary `||`'ın da
    altında) — yalnız `precedence==0` (üst seviye) çağrıda toplanır, böylece
    binary operandlar `?`'i yutmaz. Her iki dal `parse_expression(0)` ile
    ayrıştırılır → sağ-asosiyatif iç içe (`a?b:c?d:e` = `a?b:(c?d:e)`,
    `a?b?c:d:e` = `a?(b?c:d):e`). Eksik `:` → net iki dilli parse hatası.
  - ✅ **Test + örnek:** `tests/ternary.test.tpr` **19/19** (temel, truthiness,
    tip çeşitleri, öncelik, &&/|| koşul, iç içe, fonksiyon-arg/return,
    dizi/nesne/string bağlamı, döngü, tembellik). `examples/39_ternary.tpr`
    (Türkçe yorumlu kullanıcı örneği). `tulpar fmt` ternary'i bozmadan korur
    (metin-tabanlı). Doc sitesi: `guide/control-flow` (EN+TR) Ternary bölümü.
    Tüm örnekler + 29 focused suite yeşil (regresyon yok). → [[AOT Backend]]
- **`null` literal + codegen crash sertleştirmesi (2026-06-22):**
  - ✅ **`null` artık gerçek literal** — lexer `TOKEN_NULL` → AST `NullLiteral`
    / `AST_NULL_LITERAL` → codegen `VM_VAL_VOID` (`llvm_vm_val_void`). JSON'a
    `null` serialize eder (her iki serializer da hazırdı), falsy, obje/dizi/iç
    içe her yerde çalışır. `{"cursor": null}` artık `{"cursor":null}` üretir.
    `tests/null_literal.test.tpr` (5/5). → [[AOT Backend]]
  - ✅ **Tanımsız identifier artık çökmüyor** — codegen'in `AST_IDENTIFIER`
    hata yolu `nullptr` döndürüp tüketicilerde (obje-literal değeri, atama RHS)
    null-deref → segfault'a yol açıyordu. Artık güvenli placeholder
    (`VM_VAL_VOID`) dönüyor; `had_error` zaten set olduğundan derleme temiz
    hata mesajıyla durur. Bir typo asla derleyiciyi çökertmez. Bu, daha önce
    "non-deterministic" sanılan segfault'un asıl köküydü (`null`'mış).
    → [[AOT Backend]]
- **Ergonomi turu (2026-06-16) — "Python kadar kolay" sertleştirmesi:**
  - ✅ **t-string interpolation:** `t"total: {n} adet"` — her `{expr}`
    değerlendirilip birleştirilir (Python f-string tarzı). Lexer
    `TOKEN_TSTRING_LITERAL` (nesting-aware sonlandırma: iç `{req["path"]}`
    tırnakları/parantezleri doğru atlar), parser coerce-eden `+` zincirine
    desugar eder (`src/parser/parser.cpp build_tstring`). Her zaman `str`
    üretir; `\{`/`\}` literal parantez. `examples/38_tstring.tpr`,
    `tests/tstring.test.tpr` (13/13).
  - ✅ **`str + <non-string>` artık coerce ediyor** (önceden sessizce `0`
    dönen footgun). `"n = " + 5` → `"n = 5"`; her iki taraf da çevrilir
    (`aot_string_concat_fast` + `vm_binary_op` PLUS, `runtime_bindings.cpp`).
    string+string fast-path korundu.
  - ✅ **`let` = `var` aliası** — lexer'da `TOKEN_VAR`'a eklendi; artık
    `[typecheck] Unknown type 'let'` uyarısı yok.
  - ✅ **for-each doc bug** düzeltildi — reference (EN+TR) `for (x in coll)`'in
    çalıştığını yanlış "yok" diyordu; gerçek (dizi/string/`range(n)`, object
    hariç) belgelendi.
- **Parser/codegen sertleştirmesi (2026-04 turu, EKSIKLER #1-47):**
  postfix-after-unary, recursive forward reference, `func name(...): T`
  syntax, AOT cross-platform (Windows MSYS2, MSVC `gettimeofday`,
  Winsock makro çakışması, CMake LLVM `ZLIB::ZLIB`/`LibXml2`), fn-local
  json/string codegen, hot-loop alloca, BubbleSort stack-overrun, imported
  modüllerde fn signature forward-decl, Rust-stil hata mesajları (source
  preview + caret + "did you mean?" Levenshtein).
- **Modül namespace'leri (2026-05, PR #35):** `import "x" as y` her
  top-level fn'i `y__name` olarak yeniden adlandırır; intra-module
  çağrılar lockstep rewrite edilir. `m.func(args)` postfix syntax
  rewrite ile `m__func(args)`'a inilir.
- **Real method calls on objects (Plan 01):** `obj.method(args)`
  obj modül alias değilse `method(obj, args)` formuna çözünür ve
  mevcut serbest-fn dispatch'i üzerinden çalışır. Parser
  `parse_postfix` fallback'i ile sağlanıyor.
- **try / catch / finally + throw:** dil-seviyesi exception handling
  çalışıyor. `throw "oops"` → en yakın `catch (e)` clause'a sıçrar,
  `finally` her iki yolda da çalışır.
- **VM string concat fn-local fix (PR ~#35 öncesi):**
  `OP_LOAD_ADD_STORE`'a string dalı.
- **Type inference pre-pass (PR #32):** `tulpar typecheck` standalone,
  her `build`/`run`/`--vm` çağrısında `[typecheck]` uyarıları.
  `--no-typecheck` / `TULPAR_NO_TYPECHECK=1` opt-out.
- **Sunset turu (2026-05-04, PR #30+#31):** Tree-walk interpreter
  (−6109 satır), x64 JIT (−2162 satır). REPL artık VM compiler'a
  feed eder.
- **VM/AOT builtin paritesi (2026-05-13, PR'lar #243 → #264):** Sunset
  sonrası VM, `tulpar build`'in dispatch ettiği ~40 builtin için ya
  hiç bağlı değildi ya da paralel — ama tutarsız — bir
  implementasyon taşıyordu. Tüm yol şu şekle indirildi: compiler.cpp
  `OP_CALL_BUILTIN <slot>` çıkartır, vm.cpp ilgili `case`
  doğrudan `extern "C" aot_X(...)` çağırır, AOT impl'i tek doğruluk
  kaynağı olur. Kapsam: array `pop`, JSON (`fromJson`/`toJson`
  duplicate VM serializer'ın silinmesi + escape düzeltmesi), `keys`,
  HTTP (`http_parse_request`/`http_status_text`/`http_should_keepalive`/
  `path_match`/`http_create_response` 3/4/5-arity), regex (4), math
  (`pow`/`round`/`min`/`max`/`mod`/`random` + `abs` artık int
  korur), datetime (`format`/`parse_iso8601`, `now_iso8601`,
  `weekday`, `date_add_seconds`), string (`repeat`), csv, db
  (`db_execute`/`db_error`/`db_last_insert_id`), arena, time
  (`time_ms`/`timestamp`/`cpu_count`), input (`input_int`/
  `input_float`), `string_pin`, `parse_cookies`/`parse_query`. Aynı
  turda runtime_error stack trace'lerindeki `[line -1]` placeholder'ı
  `chunk_line_at()` binary search'üyle gerçek satır numarasına
  yükseldi (PR #251); `Chunk::line_offsets[]` yan dizisi RLE'ye
  yer-offset ekledi. Etki: `--vm` altında `tests/break/errors/http/
  json/json_edges/methods/router/datetime/strings/math/stdlib_extras/
  stdlib_phase_a` paketlerinin builtin'e dayalı failure'ları
  kapandı — geriye sadece `struct_native`'in by-value param
  semantiği kaldı, o native struct desteğini VM'ye getirmeyi
  gerektirir (ayrı plan).

### HTTP runtime (Wings/Router native)

- ✅ **Wings/ORM DX turu (v3.7.0, 2026-07-03)** — [WINGS_DX.md](WINGS_DX.md)
  tasarımının dört ayağı da uygulandı, hepsi geriye-uyumlu (rename = yeni ad +
  eski ad sarmalayıcı):
  - ✅ **ORM v2 (`lib/orm.tpr` yeniden yazım):** `database(path)` +
    `model(table, schema)` → sıradan-json model handle; UFCS ile
    `Note.create/find/all/where/first/count/update/save/remove/raw`.
    Tip kısayolları `body_schema` kelime hazinesiyle ortak (`"pk"`,
    `"str!"`, `"bool"`…; tanınmayan tanım ham SQL geçer), `bool` kolonlar
    okurken true/false'a cast'lenir, üretilen her SQL 3-arg
    `db_query/db_execute` ile bağlı parametreli. **v1 bug fix:** kolon seçimi
    truthiness yerine `keys(attrs)` — `{"done": 0}` / `""` / `false` artık
    gerçekten yazılıyor (v1 sessizce düşürüyordu; `orm_*` sarmalayıcılar da
    düzeltmeyi alır). Çoklu-db: her handle kendi bağlantısını taşır.
    `where()`/`all()` opsiyonel `order`/`limit` opts'u da destekler
    (2026-07-04, detay → Açık eksikler kapatılan madde).
    `tests/orm.test.tpr` **11/11**.
  - ✅ **`resource(path, Model[, opts])` (`lib/wings.tpr`):** modelden 5 REST
    route (index/show/create/update/destroy) + modelden türetilmiş
    `body_schema` (`"str!"` → zorunlu; otomatik 422) + /docs beslemesi.
    `opts {"only"/"except"}` filtreleri. wings orm'u **import etmez** —
    `{"table","db","schema"}` sözleşmesi + `_wings_res_db_*` iç helper'ları
    (db_* builtin'leri, parametreli SQL). Kalıcı CRUD API 175 satırdan
    **7 satıra** indi (`examples/wings_orm_resource.tpr`); canlı curl ile
    201/422/204/404 zarfları + restart-sonrası kalıcılık doğrulandı.
  - ✅ **Adlandırma standardı:** `serve(port, workers)` tek kapı (workers>1 →
    listen_pool; default-arg 0-pad sayesinde `serve()`/`serve(8080)` aynen);
    kısa adlar `cookies`, `ws_upgrade/ws_send/ws_close/ws_pong`,
    `sse_headers/sse_event`, `metrics_prom`, `gzip`, `delete`, `accepts`,
    `returns`. Kural seti [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md) sonunda.
    `tests/wings_dx.test.tpr` **10/10**.
  - ✅ **Keşfedilebilirlik:** LSP builtin tablosuna DX katmanı + eksik
    `patch/head/options`/`body_schema`/`response_model`/`param`/`query`/`form`
    + **tüm ORM v2 yüzeyi** eklendi (ORM'in LSP'de hiç kaydı yoktu).
    `wings_notes_db.tpr` modernize (yorumları v3.3.0'dan beri "parametreli
    sorgu YOK" diyordu — düzeltildi; bağlı parametre + funcref + accepts),
    `api_wings_crud.tpr` modernize (`_request` globali → `req` parametresi),
    README'ye 7-satır vitrin. → [[Wings]]
- ✅ **Wings tamamlık turu (v3.6.0, 2026-07-02)** — HANDOFF §2'nin P1/P2/P3
  listesi tek turda kapatıldı (WS-recv Windows ABI maddesi hariç; o Windows
  test erişimi bekliyor). Hepsi `lib/wings.tpr` (saf Tulpar) + bir C builtin:
  - ✅ **Çerez SET + imzalı çerez:** `_wings_cookie_str` builder
    (Path=/ + SameSite=Lax + HttpOnly varsayılan; path/domain/max_age/
    same_site/secure/http_only opsiyonları), `set_cookie`/`delete_cookie`,
    `set_signed_cookie`/`get_signed_cookie` — değer `hmac_sha256(secret,
    name+"."+value)` MAC taşır (name-bound: bir çerezin değeri diğerine
    takılamaz); MAC karşılaştırması çifte-HMAC (timing-safe). Yanıt başlığı
    dict olduğundan yanıt başına TEK Set-Cookie (son çağrı kazanır) —
    belgelendi.
  - ✅ **Sunucu-taraflı session:** `session_start(req, secret)` (geçerli
    imzalı `tsid` çerezi varsa onu, yoksa `secure_token(32)` üretir),
    `session_attach(res, sid, secret)`, `session_set/get/destroy`.
    Depo in-memory `_wings_sessions` global'i (auto-persist yazımları
    kalıcılaştırır); süreç ömürlü — kalıcı veri DB'ye.
  - ✅ **Yapılandırılabilir CORS:** `cors(origin, opts)` startup'ta
    `_default_headers`'ı değiştirir (write-once → tüm listen modlarında
    thread-safe, otomatik OPTIONS/preflight 204'ü de kapsar). Spesifik
    origin'de `Vary: Origin`; `credentials:true` →
    `Access-Control-Allow-Credentials` (registry'nin proxy workaround'ına
    neden olan boşluk buydu). methods/headers/expose/max_age opsiyonları.
  - ✅ **Rate-limit middleware:** `rate_limit(max, window_s)` — fixed-window,
    `use()` zinciri üstünden (3 serve modunu da kapsar). Anahtar:
    X-Forwarded-For/X-Real-IP ilk hop, yoksa global bucket. Pencere
    devrilince tablo sıfırlanır (bellek sınırlı). Aşımda 429 +
    `Retry-After`. Canlı: aynı IP'den tam 100×200 → 5×429, farklı IP 200.
  - ✅ **JWT guard:** `jwt_guard(secret)` — `wings_jwt` paketiyle birebir
    wire-interop (HS256, base64url segmentler, imza=base64url(hex MAC));
    doğrulama `_wings_jwt_verify` (çifte-HMAC imza karşılaştırma + exp).
    Geçerli token → `req["jwt"]` claims; yoksa/bozuk/expired → 401.
    `jwt_public(path)` muafiyeti: tam eşleşme veya sondaki-`*` prefix
    (`/items/*`); healthz/metrics/docs/openapi.json varsayılan muaf.
  - ✅ **html()/render():** `html(body)` text/html `_raw` yanıtı;
    `render(tpl, vars)` `{{key}}` ikamesi (t-string tek-ifade, render
    "şablon+dict" durumunu kapatır).
  - ✅ **Tipli path param:** `param`/`param_int`/`param_bool` — query
    helper'larının `req["params"]` aynası.
  - ✅ **ETag → 304:** `cached_get` cache doldururken gövdeden güçlü ETag
    üretir (`"sha256[:16]"`), header'a enjekte edip wire'a pinler
    (`_wings_cache_etags` paralel dizisi); cache hit'te `If-None-Match`
    eşleşirse boş 304 döner. Canlı: doğru etag → 304, yanlış → 200.
  - ✅ **gzip yanıt sıkıştırma:** yeni runtime builtin **`gzip_compress(s)`**
    — `runtime/tulpar_gzip.cpp` **ağaç-içi DEFLATE** (sabit Huffman +
    greedy LZ77 32K pencere + CRC-32, RFC 1951/1952). Bilinçli karar:
    zlib/miniz bağımlılığı YOK — AOT kullanıcı binary'leri self-contained
    kalır (miniz vendorlaması güvenlik sınıflandırıcısına takıldı; in-tree
    yazım hem o endişeyi hem -lz link zorunluluğunu çözdü). 5 noktada
    bağlandı (runtime `aot_gzip_compress`+`_ptr`, llvm hpp/cpp, typeinfer,
    lsp). Wings tarafı `enable_gzip(min_bytes)`: Accept-Encoding: gzip +
    gövde ≥ eşik → `Content-Encoding: gzip` + `Vary`; `cached_get` hariç
    (pinli wire tüm istemcilere ortak); büyümeyen çıktıda düz gönderim.
    **Doğrulama:** Python `gzip.decompress` + `curl --compressed` (CRC
    denetimli) ile **byte-exact** — UTF-8 metin (16.4 KB → 274 B, %1.7),
    tam bayt-aralığı + rastgele + run'lı 19 KB ikili girdi.
  - ✅ **OpenAPI tamamlama:** `response_model` şeması artık 200 yanıt
    gövdesini belgeler; `jwt_guard`/`docs_security("bearer")` →
    `components.securitySchemes.bearerAuth` + global `security` (Swagger
    Authorize butonu).
  - ✅ **Yan düzeltme:** `_wings_filter_obj` artık `_headers`'ı koruyor —
    önceden response_model, Set-Cookie/Location başlıklarını düşürüyordu.
  - **Testler:** `tests/wings_features.test.tpr` **13/13**;
    `examples/wings_features_api.tpr` (COMPILE_ONLY_TESTS + $compileOnly'ye
    eklendi) canlı curl ile uçtan uca: session visits 1→2, tamper→401,
    JWT 401/200, ETag 304, gzip byte-exact, rate-limit 429. Regresyon:
    tüm örnekler + 33 focused suite yeşil. → [[Wings]]

- **Global dict key-persist + temiz cevap gövdesi + 3 örnek uygulama + Wings
  Öğreticisi (2026-06-23):**
  - ✅ **Global obje key-write artık istekler arası kalıcı (runtime fix):**
    `_tokens[token] = uid` gibi bir **global json nesnesine key yazımı**
    `arena_restore` sonrası bozuluyordu — değer (write-barrier ile codegen'de)
    kalıcılaşıyor ama **key string'i** `vm_object_set`'te per-request arena'da
    ayrılıp reclaim ediliyordu → dangling key, sonraki istekte key adı çöpe
    dönüyor (`{"k":42}` → `{"obj":42}`). `src/vm/runtime_bindings.cpp` →
    `vm_object_set`: codegen değer-barrier'ının runtime ikizi — container kalıcı
    ve key transient ise key `aot_persist_string_obj` ile malloc'a kopyalanır;
    request-scoped objeler (params/cookies/json) `obj_transient=1` olduğundan
    sıcak yol dokunulmaz/bedava. **Etki:** global dict artık gerçek bir
    in-memory session/token/cache tablosu (auth akışı bunsuz çöküyordu). Array
    `push` zaten kalıcıydı; artık dict de simetrik. Minimal izole test + 3
    örnekte canlı curl + tam `build.sh test` yeşil ile doğrulandı.
  - ✅ **Cevap gövdesinden envelope meta-anahtarları ayıklandı:** `created(x)`
    artık `{"id":1}` (`201` status satırıyla) döndürüyor — eskiden gövdeye
    `_status:201` sızıyordu. `aot_wings_build_response` zarfı (`status`/`_raw`/
    `_content_type`) yine okuyor ama JSON gövdeyi `aot_wings_strip_meta` ile
    serialize ediyor: `_status`/`_stream`/`_raw`/`_content_type` atılır. Meta
    yoksa (sıradan `ok(data)`) **sıfır kopya** — sığ kopya yalnızca meta varken,
    arena-tracked. Cookbook'un gösterdiği temiz çıktıyla artık birebir uyumlu.
  - ✅ **3 tam örnek uygulama (`examples/`, baştan sona Türkçe yorumlu, hepsi
    derlenip canlı curl ile doğrulandı):** `wings_todo_api.tpr` (REST CRUD +
    `:id` param + `req.json` + `body_schema` 422 + `query_int/bool` filtre +
    otomatik /docs), `wings_auth_api.tpr` (login→token→korumalı uçlar:
    `depends`/`dep` DI, `group("/api",…)`, `response_model` ile password gizleme,
    401/403/422 + logout token-invalidation), `wings_notes_db.tpr` (SQLite
    kalıcılık: `db_open/db_execute/db_query/db_last_insert_id`, `sql_str` ile
    tek-tırnak escape/enjeksiyon önleme, restart-sonrası veri kalıcı). Üçü de
    `COMPILE_ONLY_TESTS` (build.sh) + `$compileOnly` (run_tests.ps1) listelerine
    eklendi. → [[Wings]]
  - ✅ **Wings Öğreticisi (doc sitesi, EN+TR):** `ecosystem/wings-tutorial` —
    üç örneği ilerlemeli bir öğrenme yolu olarak işleyen rehberli walkthrough
    (REST → auth/DI → SQLite). Sidebar'a HTTP Server'dan sonra eklendi; astro
    build temiz (72 sayfa). → [[Wings]]
- **Static root-mount + cookbook doğrulaması (2026-06-22):**
  - ✅ **`static("/", "./public")` artık çalışıyor** — `_wings_static_try`'da
    dir+rel birleştirmesi tam bir `/` ayracı garantiliyor (prefix `/` iken
    `"./public" + "index.html"` → bozuk `"./publicindex.html"` oluyordu).
    Ayrıca dizin-tarzı istek (mount kökü veya sonu `/`) `index.html` sunuyor →
    SPA kökten servis edilebiliyor. `/static` prefix formu da aynen çalışıyor
    (canlı curl ile doğrulandı). → [[Wings]]
  - ✅ **Wings Cookbook (12 tarif, EN+TR)** — her tarif derlenip canlı sunucuya
    curl ile doğrulandı; özel yanıtlar `_raw`/`_content_type` envelope'u ya da
    `wings_current_fd()` + `{_stream:1}` deseni kullanıyor (handler'dan ham
    `http_create_response` döndürmek dispatcher tarafından JSON'a sarılır).
    → [[Wings]]
- **Wings ergonomi turu (2026-06-16) — "az satır" API yazımı:**
  - ✅ **Response helper'ları** (`lib/wings.tpr`): `ok()`, `created()` (201),
    `no_content()` (204), `bad_request()` (400), `unauthorized()` (401),
    `forbidden()` (403), `not_found()` (404), `conflict()` (409),
    `server_error()` (500), `text()`, `with_status()`. Handler `{"_status":N}`
    envelope'unu elle yazmak yerine niyeti ifade ediyor.
  - ✅ **Otomatik JSON body parse:** dispatcher `_request["json"]`'ı handler'dan
    önce dolduruyor (gövde JSON ise) — `fromJson(_request["body"])` boilerplate'i
    kalktı. `serve(port)` = `listen(port)` aliası.
  - ✅ **`persist(value)` builtin'i** (`aot_persist`, runtime_bindings.cpp) — bir
    değeri kalıcı (malloc, `arena_allocated=0`, ARC) belleğe **recursive derin
    kopyalar**, böylece istek-handler'ında üretilen veri uzun-ömürlü bir global'e
    güvenle konabilir: `push(_users, persist(u))`. Bu olmadan per-request
    `arena_restore` objeyi geri alıp global'i dangling pointer'a düşürüyor →
    sonraki istekte **segfault** (in-memory CRUD'un klasik footgun'u). `clone`
    builtin'i aynalanarak codegen'e bağlandı. **ASan ile kanıtlandı:** gerçek
    POST×2→GET arena-reset senaryosu altında 0 hata; `tests/persist.test.tpr`
    4/4 (derin-kopya bağımsızlığı dahil). `persist()` artık explicit araç olarak
    kalıyor; gündelik kod için aşağıdaki **auto-persist** onu gereksizleştirdi.
  - ✅ **Görünmez auto-persist (2026-06-17) — write-barrier (codegen):** istek
    verisi bir **global'e** yazıldığında (`push(_users, u)`, `_users[i]["name"]=v`,
    `_g = v`) derleyici RHS'i otomatik `aot_persist`'ler — manuel `persist()`
    gerekmez. `src/aot/llvm_backend.cpp`: yeni `is_global_var()` (scope'ta yoksa
    + `LLVMGetNamedGlobal` varsa global) + `lvalue_root_name()` (atama hedefinin
    kök identifier'ı, `_users[i]["k"]`→`_users`); push/index-set/var-assign
    codegen'ine barrier enjekte edildi. **Yalnızca global-köklü yazımlar**
    persist'lenir → response objeleri / scratch dizileri (yerel) dokunulmaz,
    sıcak yol bedava ve sızıntısız; skaler global'ler de no-op (`aot_persist`
    scalar passthrough). Bilinen boşluk: global elemanını yerele alias'layıp
    yazmak (`let x=g[i]; x[k]=v`) barrier'ı atlar — o durumda `persist()` hâlâ
    var. `examples/demo_users_api.tpr` ve `tests/compare_wings_users_api.tpr`
    artık `persist()`'siz; canlı CRUD (PUT→DELETE→sonraki-GET) bozulmasız.
    `tests/autopersist.test.tpr` 4/4 (arena_save/restore döngüsüyle barrier'ı
    doğrudan sınar). Tüm örnekler + 24 focused suite yeşil.
  - ✅ **Şema doğrulama + otomatik /docs (2026-06-17) — FastAPI paritesi
    (lib/wings.tpr):**
    - **`body_schema(schema)`** route'a JSON-gövde şeması iliştirir
      (`{"name":"str","age?":"int"}` — `?` opsiyonel; tipler str/int/float/num/
      bool/array/object/json/any). Dispatcher handler'dan ÖNCE doğrular; uymazsa
      **otomatik 422** + tüm hatalı alanları tek seferde döner
      (`{"error":"validation failed","fields":{...}}`) — geçersiz gövde user
      koduna hiç girmez. `{"name":123}` → `422 "expected str, got int"` (tam da
      kıyaslamada Wings'in tek kaybettiği nokta, artık kapandı). Saf Tulpar,
      C değişikliği yok (`typeof`+`keys`). `tests/validation.test.tpr` 5/5.
    - ✅ **Zengin obje-spec kısıtları (2026-06-22, commit 0d268c7):** alan-bazlı
      kısıtlar — str `min`/`max` + `regex`, sayı `min`/`max`, dizi `items` (öğe
      tipi) + `minItems`/`maxItems`, iç içe `{"type":"object","schema":{…}}`
      (dotted-path hata anahtarı), opsiyonel-ama-kısıtlı alanlar (`age?`; mevcutsa
      yine doğrulanır), yanlış tip kısıtları atlar. Saf Tulpar (`_wings_validate`/
      `_wings_type_ok`). `tests/validation.test.tpr` artık **12/12**.
    - ✅ **Global middleware zinciri — `use()` (2026-06-22):** framework
      paritesi turu (Express/Gin/FastAPI kıyası) ilk adımı. `use("mw_name")`
      middleware'i isimle kaydeder; `_wings_dispatch_cached` başında (tek dispatch
      noktası → `listen`/`listen_pool`/`listen_async` hepsi) sırayla çalışır.
      Middleware `func mw(req)`: `_status`'lı response dict (örn. `unauthorized()`)
      → kısa-devre; `{}` → devam; `req`'i mutate edebilir (`req["user"]=…`).
      Boş zincir = sıfır per-request maliyet. Saf Tulpar, C değişikliği yok
      (`call(name, req)` by-name dispatch'i kullanır). Canlı doğrulandı:
      token yok→401, token var→200 + `req.user` handler'a yansıyor
      (`examples/wings_middleware_test.tpr`). Kalan paritesi boşlukları: route
      grupları/prefix (🔴 sıradaki), static servis, multipart/upload, param
      tip-coercion, DI.
    - ✅ **Route grupları / prefix — `group()` (2026-06-22):** framework
      paritesi turu 2. adım (Express Router / Gin RouterGroup / FastAPI APIRouter
      karşılığı). `group("/api/v1", "register_fn")` — `register_fn`'in kaydettiği
      tüm route path'leri `prefix` ile öneklenir; çağrı sonrası prefix restore
      edilir, böylece gruplar **nest** eder. `get/post/put/del/cached_get` artık
      `_route_prefix + path` ile kayıt yapar (boş prefix = değişiklik yok). Saf
      Tulpar (`call(register_fn, 0)` by-name). Canlı doğrulandı: `/api/v1/users`
      200 + path param, grup-dışı `/health` 200, prefix'siz `/users` 404
      (`examples/wings_groups_test.tpr`). Kalan paritesi boşlukları: static servis,
      multipart/upload, param tip-coercion, DI, response-model.
    - ✅ **Static dosya servisi — `static()` (2026-06-22):** framework
      paritesi turu 3. adım (Express `static` / Gin `Static` / FastAPI
      `StaticFiles`). `static("/static", "./public")` bir dizini URL prefix'i
      altında sunar. **404-fallback** mimarisi: `_wings_build_404` başına tek
      `_wings_static_try` enjeksiyonu (3 serve modunu da kaplar) → gerçek
      route'lar her zaman önce, static catch-all. Path-traversal (`..`) reddi;
      text asset'lere (html/css/js/json/svg/txt) doğru Content-Type, gerisi
      octet-stream. Saf Tulpar (`read_file` + `http_create_response`). Canlı
      doğrulandı: css/html 200 + doğru CT, eksik dosya 404, `/static/../secret`
      404 (sızmadı), gerçek route kazanır (`examples/wings_static/`). Binary
      asset (PNG, gömülü-NUL) byte-exact round-trip eder — `read_file`/
      `http_create_response` length-tracked (strlen değil), ampirik doğrulandı.
      Kalan paritesi: multipart/upload, param tip-coercion, DI, response-model.
    - ✅ **Tipli query-param erişimi — `query`/`query_int`/`query_bool` (2026-06-22):**
      framework paritesi turu 4. adım (FastAPI tipli query parametrelerine yakın).
      `req["query"]` ham string tutar; bu helper'lar varsayılan-fallback ile coerce
      eder — `int page = query_int(req,"page",1)`, `bool desc = query_bool(req,"desc",false)`.
      Eksik-key güvenliği `_wings_has_key`, sayısal coercion `toInt` (bozuk/boşta 0).
      Saf Tulpar. Canlı doğrulandı: varsayılanlar, `?page=3&limit=50&sort=name`,
      `?desc=true`/`?desc=0`, `?page=abc`→0 (`examples/wings_query_test.tpr`).
    - ✅ **Response model / çıktı şekillendirme — `response_model` (2026-06-22):**
      framework paritesi turu 5. adım (FastAPI `response_model`). `body_schema`'nın
      simetriği: route'a şema iliştirir, BAŞARILI handler çıktısını yalnız bildirilen
      alanlara filtreler → password/`_internal` gibi sırlar serileştirmeden önce
      düşer (handler tüm satırı dönebilir, sızma yok). Yalnız bilinen kontrol
      key'leri (`_status`/`_raw`/`_content_type`/`_stream`) korunur; hatalar (>=400)
      filtreyi bypass eder (`{error:...}` sıyrılmaz). Obje + obje-dizisi destekli.
      Saf Tulpar (`typeof`/`keys`/`_wings_has_key`). Canlı doğrulandı: tekil obje
      (password+`_internal` düştü), array (eleman-bazlı), `created` 201 `_status`
      korundu, hata bypass (`examples/wings_response_model_test.tpr`). **Not:**
      ilk sürümde "tüm `_`-önekli key'leri koru" kuralı `_internal`'ı sızdırıyordu;
      doğrulama aşamasında yakalandı, kontrol-key whitelist'ine çevrildi.
    - ✅ **Multipart/form-data + dosya yükleme (2026-06-22):** framework
      paritesi turu 6. adım (Express multer / Gin `c.FormFile` / FastAPI
      `UploadFile`) — **bu oturumda C gerektiren tek özellik.** Yeni native builtin
      `parse_multipart(body, content_type)` → `{fields, files}`; C parser
      `runtime_bindings.cpp`'de (binary-safe `aot_memfind`, boundary/part/header
      ayrıştırma, length-tracked arena string'ler). AOT'a `aot_split_ptr` deseninde
      2-arg pointer-ABI builtin olarak bağlandı: `llvm_backend.hpp` field +
      `llvm_backend.cpp` decl & dispatch + `builtins.cpp` (LSP). Wings
      sarmalayıcıları (`lib/wings.tpr`): `form(req,name,fb)` text alanı,
      `uploaded_files(req)` → `[{name,filename,content_type,data,size}]` (data ham
      byte). **Her iki target** (tulpar + tulpar_runtime) + repo-kökü
      `libtulpar_runtime.a` kopyalandı. Canlı doğrulandı: text alan (UTF-8) + text
      ve PNG dosya **byte-exact** kaydedildi; 52/52 örnek + 12/12 validation temiz
      (`examples/wings_upload_test.tpr`). Kalan tek paritesi boşluğu: DI (`Depends`).
    - ✅ **Küçük cila turu (2026-06-22):** (1) **Wings banner tutarlılığı** —
      tüm serve modları ortak `_wings_print_banner(port, suffix)` helper'ını
      çağırıyor (renkli kutu + route tablosu her modda aynı, mod Server satırında,
      sürüm v3.1). (2) **Wings TLS yük testi** (OpenSSL 3.5.5) — 1000 istek/50
      paralel → 0 hata ~663 req/s, keep-alive ~1.5ms, stabil. (3) **DB
      per-connection `cache_size`** — `db_apply_pragmas` her bağlantıya
      `PRAGMA cache_size=-2048` (2 MiB, `TULPAR_DB_CACHE_KB` ile tunable) → pool'da
      RSS sınırlama. (4) **DB prepared-statement cache** — `db_query` thread-local,
      bağlantı-başına bounded (64, FIFO) cache; aynı SQL `sqlite3_reset` ile yeniden
      kullanılır; `prepare_v2` şema-değişiminde auto-reprepare; `db_close` →
      `sqlite3_close_v2` + cache finalize. Hepsi canlı doğrulandı, 12/12 validation
      + tüm örnekler temiz.
    - ✅ **Dependency injection — `depends`/`dep` (2026-06-22):** framework
      paritesi turu son adımı (FastAPI `Depends`). `depends("fn")` route'a bir
      bağımlılık-fonksiyon adı iliştirir; handler'dan önce her bağımlılık `req` ile
      çağrılır, dönüş değeri enjekte edilir, handler `dep("name")` ile okur. Bir
      bağımlılık response dict (`_status`) dönerse kısa-devre (per-route auth/guard;
      sayaç/işleyiş middleware ile aynı). Çözülen değerler **thread-local**
      `_wings_deps`'te tutulur — `global_needs_tls` whitelist'ine eklendi (C, tek
      satır) → `_request` gibi listen_pool worker'ları arasında izole. Saf Tulpar
      (`call`/`typeof`). Canlı doğrulandı: token yok→dep 401 kısa-devre, token var→2
      bağımlılık (user+config) enjekte; **listen_pool 20/20 paralel istek doğru**
      (TLS izolasyonu). 53/53 örnek + 12/12 validation temiz (`examples/wings_di_test.tpr`).
      **Bununla framework paritesi turu tamamlandı** — Express/Gin/FastAPI'ye karşı
      kapatılabilir tüm boşluklar kapandı (middleware, route grupları, static,
      query-param, response-model, multipart/upload, DI).
    - ✅ **HTTP fiilleri `patch`/`head`/`options` (2026-06-25, v3.3.0):**
      `get`/`post`/`put`/`del` yanına eklendi (saf Tulpar, `lib/wings.tpr`).
      Dispatcher (`wings_find_route`) method string'ini generic memcmp ile
      eşleştirdiği için yeni fiiller doğrudan çalışır. iurl PATCH yerine PUT
      kullanmak zorunda kalmıştı — artık gerek yok. `tests/wings_verbs.test.tpr`
      4/4 (patch+param, method ayrımı, head/options eşleşme, yanlış-eşleşme yok).

- ✅ **`hmac_sha256(key, msg)` + ilk registry paketi `wings_jwt` (2026-06-29, v3.5.0) — güvenlik & ekosistem.**
  - **Yeni runtime primitifi `hmac_sha256(key: str, msg: str) -> str`** —
    anahtarlı MAC (HMAC-SHA256, RFC 2104), 64-karakter hex. Ağaç-içi SHA-256
    üstüne (OpenSSL yok); PBKDF2'yi besleyen mevcut C `hmac_sha256()` helper'ı
    .tpr koduna açıldı. İmzalı çerez / webhook imzası / JWT-tarzı token yapı taşı.
    5 noktada bağlandı (runtime `aot_hmac_sha256`+`_ptr`, llvm_backend hpp/cpp
    dispatch, typeinfer, lsp). **RFC 4231 test vektörleriyle doğrulandı**
    (Jefe/what-do-ya-want → tam eşleşme). 32 focused suite + 32 örnek regresyon temiz.
  - **İlk gerçek registry paketi: `wings_jwt`** (`packages/wings_jwt/`, saf Tulpar,
    sıfır bağımlılık) — HS256 imzalı oturum token'ları: `sign`/`sign_ttl`/`verify`/
    `decode`/`from_header`. base64url JSON segmentleri + `hmac_sha256` imza. Token
    Tulpar-native varyant (imza = base64url(hex MAC); RFC-7519 wire-interop roadmap'te,
    raw-bytes base64 yolu gerekir — README'de dürüstçe belgelendi). `wings_jwt.test.tpr`
    8/8 (round-trip, yanlış-secret, payload-tamper, malformed, exp/iat, decode,
    bearer-header), örnek wings app derleniyor, `tulpar pkg publish --dry-run`
    bundle'ı doğruluyor.
  - ✅ **Canlı yayınlandı (2026-07-04):** `POST /v1/publish` (owner
    `PUBLISH_TOKEN` ile) → `wings_jwt@0.1.0` registry'ye yüklendi (HTTP 201,
    sha256 doğrulandı), ardından `POST /v1/admin/packages/wings_jwt/approve`
    ile `approved=1` yapıldı (yeni publish'ler Faz D onay modelinde
    `approved=0` gizli geliyor — owner onayı şart). `GET /v1/packages/wings_jwt`
    artık `{"latest":"0.1.0",...}` dönüyor; `tulpar pkg add wings_jwt` ile
    kurulabilir.
  - **Docs:** `stdlib/builtins.mdx`'e Kriptografi & güvenlik bölümü (EN+TR). → [[tulpar-v330-security-apis]]

- ✅ **`secure_token(n)` — kriptografik rastgelelik (2026-06-26, v3.4.0) — güvenlik.**
  `randint`/`random` kriptografik DEĞİL (`rand()`, `srand(time())` ile tohumlanmış,
  tahmin edilebilir) — oturum token'ı/tuz gibi güvenlik değerleri için kullanılmamalı.
  `secure_token(n: int) -> str`, `std::random_device` (OS CSPRNG / `/dev/urandom`)
  ile rejection-sampling'li (modulo bias yok) n karakterlik base62 token üretir.
  5 noktada bağlandı (runtime `aot_secure_token`+`_ptr`, llvm_backend hpp/cpp,
  typeinfer, lsp). iurl `_rand_str` artık buna delege ediyor → öngörülebilir-token
  oturum-kaçırma açığı kapatıldı.

- ✅ **Parametreli SQL + şifre KDF (2026-06-25, v3.3.0) — güvenlik.** iurl gibi
  web/DB uygulamalarının ısırdığı iki gerçek boşluk kapatıldı:
  - **`db_query(db, sql, params)` / `db_execute(db, sql, params)`** — opsiyonel
    bound-params dizisi `?` yer tutucularına `sqlite3_bind_*` ile bağlanır →
    injection-safe, elle quote-escape yok. 2-arg formlar değişmedi (typeinfer'de
    3. param wildcard + "eksik arg serbest" kuralıyla). Codegen `argument_count>=3`
    dalı + yeni `aot_db_{query,execute}_params` runtime fonksiyonları (prepared-
    statement cache yeniden kullanılır, sabit SQL = tek cache girdisi).
  - **`password_hash(pw)` / `password_verify(pw, stored)`** — PBKDF2-HMAC-SHA256
    (100k iter, rastgele 16-byte salt, kendini tanımlayan `pbkdf2_sha256$iters$salt$dk`,
    sabit-zamanlı doğrulama). In-tree SHA-256 üstüne (`sha256_raw` eklendi),
    OpenSSL bağımlılığı yok. Auth'ta bare `sha256` yerine bu kullanılmalı.
  - `tests/db_params.test.tpr` 3/3, `tests/password_kdf.test.tpr` 3/3,
    `examples/40_db_params_password.tpr`; 52/52 örnek regresyon temiz.
    - ✅ **Yan-buluş: codegen verify hatası kapatıldı (2026-06-22):** karşılaştırma-
      yoğun validation, eski bir LLVM-18 O3/InstCombine miscompile'ını yüzeye
      çıkardı — `foldOpIntoPhi` karşılaştırma fast-path merge'inde geçersiz
      `phi i1` üretiyordu ("Global module verification failed"). `llvm_backend_optimize`
      artık O3 öncesi modülü `LLVMCloneModule` ile snapshot'lar; optimize sonrası
      `LLVMVerifyModule` başarısızsa temiz snapshot'a düşer → ISel'e asla geçersiz
      IR gitmez (tetiklenen modül o derleme için O3 kaybeder; nadir). 47/47 örnek
      + 12/12 validation temiz.
    - **Otomatik `/docs` + `/openapi.json`:** `listen()` artık Swagger UI'yı
      (`/docs`) ve OpenAPI 3.0 belgesini (`/openapi.json`) otomatik kaydeder
      (opt-out: `TULPAR_WINGS_NODOCS=1`). Belge route'lardan türer; `:id` path
      param'ları `{id}`'ye + parametre nesnesine, `body_schema` requestBody +
      422 yanıtına dönüşür. `docs_info(title, version)` başlığı/sürümü ayarlar.
  - ✅ **Zengin erişim logu (timestamp + latency + size) + banner (2026-06-18, UI):**
    Wings access log'u artık `  [HH:MM:SS] <method> <status> <path>  → <latency>  - <size>`
    formatında ve tamamen renkli — timestamp dim, method'a göre (GET yeşil, POST
    sarı, PUT mavi, DELETE kırmızı, PATCH magenta, HEAD cyan, OPTIONS dim) +
    **status sınıfına göre** (2xx yeşil, 3xx cyan, 4xx sarı, 5xx kırmızı), **latency
    eşiğe göre** (<50ms yeşil, <500ms sarı, ≥500ms kırmızı), size dim. Method 7
    sütuna padlenip status'ler hizalanıyor
    (`_wings_log_request(method, path, status, ms, bytes)`). Yardımcılar:
    `_wings_now_hms` (UTC ISO-8601'den `substring(...,11,19)`), `_wings_fmt_ms`
    (**adaptif**: <1ms ise mikrosaniye `34µs` — AOT sunucunun hızı 0.0ms'ye
    çökmeden görünür, üstü `12.4ms`; tenths `mod()`/`toInt(round(...))` ile, çünkü
    Tulpar'da `/` float operandla float bölme yapıyor ve `%` operatörü yok),
    `_wings_fmt_bytes` (B/KB/MB), `_wings_latency_color`, `_wings_method_color`,
    `_wings_status_color`. Latency serve loop'unda `clock_ms()` ile dispatch
    etrafında, size `length(response)` ile ölçülüyor. Status handler'dan sonra
    bilindiği için `_wings_dispatch_cached` her return'de `_wings_last_status`'u
    yazıyor, log serve loop'unda response belirlendikten SONRA basılıyor
    (matched→dispatch status, 404→404, OPTIONS→204) — 3 serve loop'unda da. Canlı
    ölçüm: GET 34µs/5µs/4µs, POST 9µs, 404 4µs, 500 18µs (sub-10µs dispatch).
    Başlangıç banner'ı
    (kutu cyan, başlık bold, route method'ları renkli + handler dim, Server URL
    bold yeşil, Ctrl+C dim). `_wings_c(code, text)` SGR sarmalayıcısı; **NO_COLOR**
    env'i renkleri kapatıyor (`_wings_color`, config'te snapshot —
    pipe/redirect'te temiz çıktı). Dil tarafı: lexer + t-string parser'a
    `\e`→ESC (27) escape'i eklendi. Not: `lib/*.tpr` (gömülü stdlib) değişince
    `./build.sh clean` gerekir — WSL'de incremental build embedded'ı stale
    bırakabiliyor.
  - ✅ **Varsayılan parametre + markalı port (2026-06-18):**
    - **Default-arg desteği (dil, codegen+typeinfer):** bir fonksiyon
      bildirdiğinden az arg ile çağrılınca eksik trailing slot'lar derleyicide
      box'lı 0 ile padlenir (`src/aot/llvm_backend.cpp` typed-call yolu); eskiden
      LLVM modül doğrulaması "Incorrect number of arguments" ile patlıyordu.
      typeinfer artık yalnızca **fazla** arg'da hata verir (eksik → izinli).
      `tests/defaultargs.test.tpr` 3/3.
    - **`serve()` markalı varsayılan port:** port'suz `serve()`/`listen()`
      (arg 0'a padleniyor) → **8484** (`_WINGS_DEFAULT_PORT`; ASCII 'T'=84,
      ikili `01010100`, "Tulpar portu"). `_wings_bind(port)`: port≤0 ise 8484'ten
      başlayıp 20 porta kadar **otomatik +1** (boştakini bağlar, banner gerçek
      port'u gösterir); port>0 ise tek dener, doluysa `_wings_bind_failed` ile
      **kullanıcıyı bilgilendirir** (sessizce kaydırmaz). Tüm 4 listen varyantı
      ortak yardımcı mesajı kullanıyor.
    - **`socket_server`'dan `SO_REUSEPORT` kaldırıldı** (`runtime_bindings.cpp`):
      varlığında iki bağımsız süreç aynı portu paylaşıp bind "fail" etmiyordu →
      fallback/bilgilendirme tetiklenmiyordu. `listen_pool` tek fd'yi worker'lara
      `accept()` ettirdiği için re-bind yok, REUSEPORT'a ihtiyaç yok;
      `SO_REUSEADDR` (hızlı restart) kalıyor.
    - Doğrulandı: `serve()`→8484; 8484 doluyken `serve()`→8485; `serve(8484)`
      doluyken bilgilendirir; `serve(8080)` (Windows Express dolu) bilgilendirir.
      `examples/demo_users_api.tpr` + `tests/compare_wings_users_api.tpr` artık
      `serve()` kullanıyor (port çakışması olmadan çalışır).
  - 📊 **Benchmark — Wings vs FastAPI (2026-06-17, `benchmarks/`):** aynı CRUD;
    `wings_vs_fastapi.py` (dep'siz çoklu-proses load client — native wrk/hey
    yokluğunda RPS yumuşak tavan, latency gerçek sinyal). Sonuç: Wings p50
    **0.33 ms** vs FastAPI **28 ms** (~85× düşük; Wings işlem ~10 µs, gerisi
    serial kuyruk); idle RSS **6.7 MB** vs 51 MB; binary **2.1 MB** tek dosya vs
    Python runtime. RPS ~3000 (Wings serial `serve()` tavanı) vs ~2250 — ikisi de
    client-bound. Benchmark **istek-başına ~2.6 KB bellek sızıntısı** ortaya
    çıkardı (RSS sınırsız büyüyordu) — aynı gün **per-request malloc region +
    runtime write-barrier ile çözüldü** (RSS artık düz; ASan temiz). Detay →
    Açık eksikler / Runtime + codegen.
  - 📊 **Stres testi — maksimum yük & latency (2026-06-18, `benchmarks/`):**
    çok-thread'li C load generator (`loadtest.c`, 1µs histogram, keep-alive +
    Connection:close modları) + `run_stress.sh`. 14 CPU'da tepe değerler:
    `serve` (varsayılan, tek bağlantı) **4.2k RPS** / p50 230µs;
    `listen_pool` (14 worker) çekirdek sayısına **lineer** ölçekleniyor → tepe
    **~39.8k RPS** / p50 360µs (CPU-bound handler'larda en iyi, en sıkı p99);
    `listen_evented` (tek thread, poll-multiplexed) hafif handler'da **57.8k
    RPS** (en yüksek ham RPS). Tüm modlar 200+ eşzamanlı bağlantı ve 500k+ istek
    altında **6.9–8.4 MB RSS'te düz**, `err=0`. **Kritik bulgu+düzeltme:** close
    modu **arena-checkpoint tükenmesi** sızıntısını ortaya çıkardı — pool RSS
    9.1 GB'a fırladı. Kök neden: 32-slot checkpoint stack'i (`arena_restore`
    checkpoint'i koruyor, `top=idx+1`), pool/evented kalıcı thread'de bağlantı/
    istek başına `arena_save` yapıp serbest bırakmıyor → 32'den sonra
    `arena_save` -1 döndürüp tüm reset'ler no-op oluyordu. Çözüm: yeni runtime
    primitifi **`arena_drop(handle)`** (rewind + checkpoint'i POP eder,
    `arena_save`'in scope-çıkış eşi) — `_wings_serve_connection`/
    `_wings_serve_one_request` çıkışta drop ediyor; `listen_evented` ayrıca her
    poll tick'ini arena scope'una alıp kalıcı `fds` dizisini **yerinde**
    (clear+refill) güncelliyor. Sonrası: pool 481k istekte **8.4 MB düz**,
    evented 293k istekte **6.9 MB düz**. **DB-bağlı test** (`stress_db_server.tpr`,
    SQLite 1000 satır, global paylaşımlı handle — `orm`'in deseni): read-by-PK
    pool **23.8k** / evented **29–32k RPS**, write rollback **8.8k** → **WAL
    20.4k** (2.3×). Asıl darboğaz **HTTP değil, SQLite paylaşımlı-handle
    serileştirmesi** (THREADSAFE serialized mutex): read'ler pool worker'larıyla
    ölçeklenmiyor, hatta tek-thread evented mutex çekişmesi olmadığı için pool'u
    geçiyor. Detay → `benchmarks/WINGS_STRESS.md`.
  - ✅ **DB varsayılanları + `db_last_insert_id`/`db_error` signature fix
    (2026-06-18):** `db_open` artık server-dostu varsayılanlar uyguluyor —
    `busy_timeout=5000` (SQLITE_BUSY yerine bekle-tekrar dene) + dosya-tabanlı
    DB'lerde **WAL + synchronous=NORMAL** (write throughput stres testinde
    **8.8k → 20.4k RPS / 2.3×**, artık env'siz varsayılan; `:memory:` atlanır,
    `TULPAR_DB_NO_WAL=1` ile opt-out). `db_close`'da WAL dosyaları temizleniyor.
    Ayrıca `db_last_insert_id` ve `db_error` codegen'de `db_open_type` (ptr) ile
    deklare ediliyordu ama runtime impl'leri VMValue-by-value — çağrı by-value
    geçtiği için LLVM **module verification** her DB programında uyarı basıyordu;
    by-value tipiyle deklare edilerek düzeltildi (çıktı zaten doğruydu, artık
    temiz). Detay → `benchmarks/WINGS_STRESS.md`.
  - ✅ **Thread/bağlantı-başına DB handle — paralel read (2026-06-18,
    `runtime_bindings.cpp`):** stres testinin bulduğu asıl darboğaz —
    tek paylaşımlı `sqlite3*` handle'ının SQLite serialized-mutex'inden
    geçip pool worker'larıyla ölçeklenmemesi — kapatıldı. **DB handle artık
    ham pointer değil, 1-tabanlı bir `DbConn` descriptor index'i** (kullanıcı
    API'si değişmedi, hâlâ int64). Dosya-tabanlı DB'lerde **her thread kendi
    `sqlite3` bağlantısını lazily açıyor** (`db_resolve` + `thread_local`
    bağlantı cache'i) → WAL altında çok okuyucu paralel ilerliyor.
    `:memory:`/temp DB'ler **tek paylaşımlı bağlantıda** kalıyor (bağımsız
    bağlantı diğerinin in-memory DB'sini göremez — serialized, tarihsel
    davranış). `db_last_insert_id` çağıran thread'in bağlantısına çözülüyor
    (handler içinde INSERT ile aynı thread → doğru rowid). `db_close`
    idempotent + `closed` bayrağı: tüm per-thread bağlantıları kapatır,
    sonrası `db_resolve` null döner (stale pointer kullanılmaz). **Ölçülen
    kazanım:** read-by-PK pool throughput **23.8k → 35.1k RPS (+~47%)** —
    paralel okuma artık worker'larla ölçekleniyor, SQLite serialized-mutex
    darboğazı kalktı; per-istek log'unda 14 worker'ın **14 ayrı `sqlite3`
    bağlantısı** açtığı ([dbopen] tid/db farklı) ve db_query'nin asla 0-satır
    dönmediği doğrulandı. Kalan küçük fark (evented tek-thread 37.3k) minik
    page-cache okumalarında thread-scheduling overhead'inden — mutex
    çekişmesinden değil. **Bellek düz + ihmal edilebilir:** baseline 9.36 MB →
    14 worker sonrası 9.9 MB, 900 istekte de 9.9 MB (per-bağlantı ~40 KB,
    sızıntı yok). Doğrulandı: smoke (insert/query/last_insert_id/close),
    `examples/13_database.tpr`, `bool_to_int_coerce` 3/3 — hepsi yeşil.
    **İki dev-environment notu:** (1) RSS'i AOT child `/tmp/.tulpar_run`'da ölç
    (`./tulpar` driver'ı LLVM yüzünden ~62 MB gösterir, sunucuyu değil).
    (2) **AOT linker repo kökündeki `libtulpar_runtime.a`'yı önce probe eder** —
    `runtime_bindings.cpp` değişince hem `./tulpar` hem `./libtulpar_runtime.a`
    köke kopyalanmalı, yoksa stale runtime linklenir ve değişiklik test edilmez
    (`build.sh` bunu otomatik yapar; manuel `cmake --build` sonrası elle kopyala).
    Detay → `benchmarks/WINGS_STRESS.md`.
  - ✅ **`toString()` thread-safety fix — pool'daki ~%1.1 sahte 404'ün kök
    nedeni (2026-06-18, `runtime_bindings.cpp`):** `listen_pool`/`listen_async`
    altında handler'ların ~%1.1'i 404 dönüyordu. Uzun teşhisten sonra kök neden
    bulundu: `aot_to_string` (yani `toString()`) **thread_local olmayan**
    `static char aot_string_buffer[1024]` paylaşıyordu. İki worker aynı anda
    `toString()` çağırınca buffer'da yarışıyor, biri diğerinin baytlarını
    eziyordu. Klasik belirti: bir handler'ın `"... id = " + toString(id)` ile
    kurduğu SQL'de **`toString()` boş string dönüyor** (`WHERE id = ` — sayı
    eksik) → 0 satır → handler `not_found()` → sahte 404. Düzeltme tek satır:
    buffer'ı **`thread_local`** yap. Bu, find_route/db_query'den **bağımsızdı**
    (her ikisi de temizdi: `[noroute]=0`, `[zero]=0`; teşhis bunu eledikten
    sonra `toString` kaldı). Doğrulama: fix öncesi pool conc=16/30 ~%1.1 hata
    → fix sonrası **pool conc=16: 107k istek 0 hata, conc=30: 80k 0 hata,
    evented: 149k 0 hata**. Not: aynı dosyadaki `base64_decode_buf`'un lazy-init
    tablosu da non-TLS ama idempotent + cold path (WS accept), zararsız.
  - ✅ **Concurrency audit — `runtime_bindings.cpp`'deki tüm non-TLS shared
    mutable state taraması (2026-06-18):** toString fix'inden sonra tüm
    `static` mutable state sistematik tarandı; iki ek **gerçek** çok-thread bug'ı
    bulunup kapatıldı (her ikisi de `listen_pool`/`listen_async`'i etkiliyordu):
    - **Exception handler context (`eh_main`/`eh_cur`) → `thread_local`:** bunlar
      global'di. Async modeli tek-thread event loop'ta güvenliydi ama bir Wings
      pool/async handler'ı `try`/`throw` kullanırsa **felaket**: worker'lar aynı
      `eh_main.stack[]`/`depth`'i paylaşıp bir thread'in `aot_throw`'u **başka
      thread'in setjmp frame'ine longjmp** ederdi (UB/crash). Stress server
      try/catch kullanmadığı için gizli kalmıştı. TLS yapıldı → her worker kendi
      try-frame stack'ine sahip; async swap hâlâ tek-thread'de çalışır.
      **Doğrulama:** her istekte try/throw/catch yapan pool sunucusu
      **152k istek, 0 hata, sunucu ayakta** (cross-thread longjmp yok).
    - **Dynamic-call cache (`g_call_cache`) → acquire/release atomic publish:**
      lock-free lookup `key`'i okuyup `ptr`'ı kullanıyordu; insert `key`'i en son
      yazıyordu ama **plain store** ile (x86 TSO'da doğru, ama yorumun da kabul
      ettiği gibi **ARM/aarch64'te değil** — proje Apple Silicon + aarch64 CMake
      target'ında çalışıyor). ARM'da lookup `key!=null` görüp **stale `ptr`**
      okuyup yanlış handler'a/crash'e gidebilirdi. `key` `std::atomic<const
      char*>` yapıldı: insert release-store, lookup acquire-load → klen/khash/ptr
      yazımları görünür garanti. x86'da bedava (mov), ARM'da ucuz (ldar/stlr).
    Diğer non-TLS static'ler güvenli doğrulandı: arena/checkpoint/region/
    `js_small_buffer`/`g_wings_current_fd` zaten TLS, `g_call_cache`/`g_db_registry`
    mutex-korumalı, `aot_runtime_init` startup-only. `base64_decode_buf` (cold,
    idempotent) ve `srand`/`rand` (hot-path değil) bilinçli bırakıldı.
    **Regresyon:** 27 focused suite + 30 örnek + async/errors testleri yeşil.
  - ✅ **Aşama 2 — fonksiyon-referans handler'ları + `req` parametresi
    (codegen/dil):**
    - **Fonksiyon adı değer olarak:** bir bare üst-seviye `func` adı codegen'de
      adının string sabitine indiriliyor (`src/aot/llvm_backend.cpp`
      AST_IDENTIFIER fallback'i). Böylece `get("/users", list_users)` çalışıyor
      (dispatch zaten `call(string)`) ve **yanlış isim derleme hatası** —
      sessiz runtime 404 değil. Forward-reference çalışıyor (handler aşağıda
      tanımlı olabilir). Fonksiyon *çağrıları* `foo()` etkilenmez (ayrı AST).
    - **`req` parametresi:** yeni `aot_call_dynamic_1(name, arg)` runtime +
      `call(h, arg)` 2-arg codegen formu; Wings dispatcher artık her handler'a
      `_request`'i geçiyor (`call(route["handler"], _request)`). 0-param
      handler'lar ekstra argümanı **ABI-güvenli** yok sayar, `func h(req)`
      olanlar alır — ikisi de çalışır.
    - **Keyword field erişimi:** parser `.field` konumunda tip-keyword'lerini
      de kabul ediyor (`req.json`, `obj.type` → `["json"]`/`["type"]`).
    - İdeal API (`examples/demo_users_api.tpr`) uçtan uca canlı doğrulandı;
      `tests/funcref.test.tpr` 4/4. 23 focused suite + tüm örnekler yeşil.
- **Native HTTP parser:** `http_parse_request(raw)`,
  `path_match(pattern, path)` (Express-style, `:id` capture, `*`
  wildcard), `parse_query(str)` URL-decoded, `http_status_text(int)`
  (30+ IANA reason), `http_create_response(status, ct, body, [headers])`
  4-arg form custom headers (CORS, Set-Cookie, X-*).
- **Wings handlers:** default CORS on, auto OPTIONS → 204 preflight,
  `_request` TLS (mutex kalkmış), `_wings_handler_mu` artık yok.
- **Streaming `http_recv_request`:** Content-Length-aware,
  `TULPAR_HTTP_MAX_BODY` env override (default 16 MiB).
- **4 listener varyantı:** `listen` (sync), `listen_async`
  (thread-per-conn), `listen_pool(port, n)` (worker pool, n=0 →
  `cpu_count()`), `listen_evented` (poll()-multiplex).
- **Wire-byte cache:** `cached_get(path, handler)` — handler bir kez
  çalışır, sonraki istekler cached bytes'i tek syscall ile gönderir.
- **Counter atomic RMW (PR #81):** Top-level VE imported-module
  int globals için `LLVMBuildAtomicRMW` superinstruction;
  `_wings_requests_total/2xx/4xx/5xx` data-race-free. Imported
  int globals AOT'da `add_local_typed(.., INFERRED_INT, ig)` ile
  kayıtlanır → `AST_ASSIGNMENT` typed-int fast-path'i
  `global_needs_atomic_rmw()` whitelist'ine takılan yazımları
  `atomicrmw add monotonic`'e indirir. `import "wings"`'le çekildiğinde
  de race-free.
- **Thread-local `_request` (PR #79):** `global_needs_tls()` AOT
  helper'ı `_request` global'ini `LLVMGeneralDynamicTLSModel` ile
  işaretliyor → her thread kendi slot'una yazar, mutex serileşmesi
  yok. Wings 4 listener varyantında handler dispatch tamamen paralel
  ilerliyor; `_wings_handler_mu` kalıntısı da PR #79'la kaldırıldı.
- **Native fast path'ler:** `wings_build_response` (envelope+toJson+framing
  tek C çağrısı), `wings_find_route` (route lookup tek C çağrısı),
  `fast_u32_itoa` + el-yazısı framing, co-located ObjString+chars
  allocation. Localhost loopback bind (LAN firewall prompt yok).
- **HTTPS listener (`lib/wings_tls.tpr`):** `wings_tls(port, cert, key)`
  açılışta tek bir `SSL_CTX` kurar, kabul edilen her bağlantıyı
  thread-per-conn worker'la `SSL_accept` → `SSL_read` →
  handler dispatch → `SSL_write` → `SSL_shutdown` zincirinde
  servis eder. OpenSSL gating CMake `find_package(OpenSSL)` ile.
  PR #197 ile CI smoke probe (`examples/api_wings_tls.tpr` +
  `tests/fixtures/tls_dev.crt/.key` 10-yıl geçerli self-signed
  fixture) end-to-end doğrulama altında.

### Pkg ekosistemi

- **CLI:** `init/list/add/remove/install/publish/search/info` + `.tpkg`
  JSON bundle (multi-file) + content sniff (raw `.tpr` vs `.tpkg`).
  `pkg search [query]` registry catalog'unu ASCII-lowercase substring
  match ile filtreler (boş query = tüm listele), `pkg info <name>`
  ise tek paket detayı (versiyon listesi, indirme sayısı, install
  hint string). İkisi de `--registry <url>` flag'i kabul eder ve
  registry'siz çağrıyı hata ile reddeder.
- **Manifest:** TOML alt-kümesi, `[registry]` + `[dependencies]`
  bölümleri, `strict = true` typecheck pre-pass eskaltörü.
- **Semver range:** exact, caret (`^1.2.3`), tilde (`~1.2.3`),
  any (`*`/`""`), bare comparator (`>=`, `<`, `=`), compound
  (`>=1,<2`). Pre-release + build-metadata (`1.0.0-rc1+ci.42`)
  parser + comparator: semver 2.0.0 §11 ordering, build
  metadata round-trips but doesn't affect precedence.
  `tests/pkg_smoke.py` covers add/remove/lockfile round-trip.
- **Lockfile (`tulpar.lock`):** `[resolved]` + `[checksums]` (SHA-256),
  byte-stable re-install + tamper detection.
- **Canlı registry (tulpar-be, Tulpar dilinde yazılmış):**
  `/v1/packages`, `/v1/packages/:name`,
  `/v1/packages/:name/versions/:version`,
  `/v1/packages/:name/versions/:version/source`, `/v1/publish`
  (Bearer auth). Audit log + per-IP rate limit + semver-aware
  `latest`.
- **`pkg init` default registry URL** (PR #147): yeni manifest'te
  `[registry] url = "https://api.pkg.tulparlang.dev"` yazılır;
  out-of-the-box install çalışır.
- **Registry source endpoint hardening** (tulpar-be PR #27):
  not-found durumunda 200+comment yerine 404 döner — silent
  corrupt install önlendi.

### Stdlib + tooling

- **Tame — 2D oyun kütüphanesi (`import "tame"`, v3.10.0, 2026-07-12):**
  Wings'in oyun kardeşi; iki katman, Wings ile aynı model:
  - **Native katman:** vendored **raylib 5.5** (`lib/raylib/`, zlib lisansı,
    SQLite ile aynı vendor deseni; GLFW derlemesi için eksik X11 uzantı
    header'ları `lib/raylib/x11_compat/` altında — MIT, ABI-stabil) + 26
    `aot_tm_*_ptr` builtin'i. Binding iki TU'ya ayrık: `runtime/tame_impl.c`
    (yalnız raylib.h görür, düz-skaler API + tuş-adı eşleme) ve
    `runtime/tame_bindings.cpp` (yalnız vm.hpp görür, VMValue ABI) —
    raylib.h ile windows.h aynı TU'da asla buluşmaz (Rectangle/CloseWindow/
    DrawText çakışmaları). Hepsi **ayrı `libtulpar_tame.a`** arşivinde:
    `tame_link_flags()` (aot_pipeline.cpp) linki yalnız `backend->uses_tame`
    işaretliyken ekler (import "tame" VEYA doğrudan tm_* çağrısı) → sıradan
    binary'lerin ldd'si değişmez; tame binary'si bile X11/GL'i runtime'da
    dlopen'lar (GLFW modül yükleyici + glad) → taşınabilir kalır.
  - **Codegen:** `k_tame_builtins` tablosu (`llvm_backend.cpp`) — 26 ayrı
    dispatch bloğu yerine tablo-güdümlü declare (döngü) + dispatch
    (`LLVMGetNamedFunction`); N-ptr VMValue ABI'si (`aot_ord_ptr` genellemesi),
    eksik trailing arg int 0 padlenir. Typeinfer koordinat pozisyonlarında
    int|float polimorfik (`TYPE_UNKNOWN`), renk/boyut int, tuş str; 26 sembol
    LSP hover/completion tablosunda TR dokümanlı.
  - **Tulpar katmanı (`lib/tame.tpr`, gömülü):** `window(w,h,title)`,
    `running()`, `frame_begin()/frame_end()`, `clear/rect/rect_lines/circle/
    line/pixel/text`, `key_down/key_pressed/key_released` (tuşlar adla:
    `"W"`, `"SPACE"`, `"LEFT"`, `"F1"`...), `mouse_x/y/down/pressed/wheel`,
    `set_fps/get_fps/frame_time/elapsed/screen_width/screen_height`,
    `rgb()/rgba()` + 25 adlı raylib rengi (paketlenmiş `0xRRGGBBAA` int) ve
    oyun yardımcıları `rect_overlap` (AABB çarpışma), `point_in_rect`,
    `clamp`. Varsayılan 60 FPS, ESC ile çıkış.
  - **Headless davranışı:** DISPLAY yoksa `window()` iki dilli hata basar,
    false döner, `running()` false → program temiz biter. Vendored raylib'e
    upstream-master'la birebir `InitWindow`/`InitPlatform` yaması (5.5
    dönüşü yok sayıp `rlglInit`'te segfault ediyordu — `lib/raylib/rcore.c`,
    "TULPAR PATCH" işaretli).
  - **Faz 3 — sprite/texture/font (2026-07-12):** `load_texture(path): int`
    (handle registry, DB-handle deseninin kopyası; 256 slot),
    `draw_texture(tex,x,y)` / `draw_texture_ex(tex,x,y,scale,rotation)`,
    `texture_width/height`, `unload_texture`; `load_font(path,size)` (TTF,
    64 slot) + `text_font(...)`; `measure_text(s,size)` (ortalamak için).
    Kaynaklar `close_window()`'da otomatik bırakılır (GL context / ses
    aygıtı kapanmadan — sıralı teardown).
  - **Faz 4 — ses/müzik (2026-07-12):** `load_sound/play_sound/stop_sound/
    sound_volume` (128 slot) + `load_music/play_music/stop_music/
    music_volume` (16 slot). Ses aygıtı ilk yüklemede otomatik açılır;
    **çalan müzik stream'leri `frame_end()` içinde otomatik pompalanır**
    (kullanıcıya "her karede update çağır" kuralı yıkılmaz).
  - **Faz 5 — `run(update, draw)` yönetilen döngüsü (2026-07-12):** Wings
    `listen()` modeli — fonksiyon-ref alır (`call(fn, 0)` dispatch, group()
    deseni), döngüyü + kare zamanlamasını + **kare belleğini** yönetir:
    döngü öncesi `arena_save`, her kare sonu `arena_restore` (tek-save-çok-
    restore), çıkışta `arena_drop` + pencere kapatma. Kural wings'le aynı:
    kalıcı oyun durumu global'lerde. Ek: `triangle()` (köşe sarımı işaretli
    alanla otomatik düzeltilir — raylib CCW tuzağı kapalı), `screenshot()`
    (PNG, CWD'ye — raylib yol kırpar).
  - **Gamepad (2026-07-13):** `gamepad_available(id)` / `gamepad_name(id)` /
    `gamepad_down(id, btn)` / `gamepad_pressed(id, btn)` /
    `gamepad_axis(id, axis)` — klavyedeki tuş-adı deseni gamepad'e taşındı:
    butonlar `"A"/"B"/"X"/"Y"` (PS eş anlamlıları `"CROSS"/"CIRCLE"/...`),
    `"UP"/"DOWN"/...`, `"LB"/"RB"/"LT"/"RT"` (`"L1"/"L2"...`),
    `"START"/"SELECT"/"GUIDE"`, `"L3"/"R3"`; eksenler `"LX"/"LY"/"RX"/"RY"`
    (-1..1) + `"LT"/"RT"` tetikler. `gamepad_name` string döndüren ilk tame
    builtin'i (`vm_alloc_string_aot` deseni). Donanımsız zarif yol
    (false/""/0.0, bozuk ad 0) canlı doğrulandı.
  - **Test + canlı doğrulama:** `tame_hello` / `tame_sprite_demo` /
    `tame_run_demo` suite'lerde compile-only (pencere bloklar); derlemeleri
    import → codegen → link zincirini doğrular. **WSLg altında pencereli
    canlı doğrulama yapıldı (2026-07-12):** tüm çizim türleri + sprite
    (normal & 3x/30° döndürülmüş) + ortalanmış yazı `tm_screenshot`
    çıktısıyla piksel-kanıtlı; ters-sarım üçgen auto-fix'i görüldü; 60 FPS
    pacing (`frame_time=0.0167`); ses aygıtı açıldı; `run()` 480 kare
    çökmesiz. Headless: iki dilli zarif hata + exit 0. Test varlıkları
    (`examples/tame_assets/ship.png` 239B + `beep.wav`) python stdlib ile
    üretildi. **WASM/web hedefi de tarayıcıda canlı doğrulandı (2026-07-14):
    mini/sprite/`run()` örnekleri headless Chromium'da 60 FPS render; `run()`/
    `call()` web'de bozuktu (dlsym sembol tablosu yok) — `aot_register_func` +
    arity'li dağıtımla düzeltildi (bkz. v1.0 kriterleri → Tame WASM).**
- **Test framework (`lib/test.tpr`):** jest-style `assert`,
  `assert_eq_int/str/bool`, `assert_contains`, `assert_status`,
  `assert_throws`. Runner: `test()` + `test_summary()`.
- **HTTP client (`lib/http_client.tpr`):** `http_get/post/put/delete/
  get_json/post_json`, OpenSSL-gated `https://`. Trust-store
  doğrulaması default açık: `SSL_VERIFY_PEER` + RFC 6125 hostname
  check via `X509_VERIFY_PARAM_set1_host`. `TULPAR_CA_BUNDLE=<pem>`
  ile özel CA bundle (MSYS2 builds default trust-store'a sahip
  değil), `TULPAR_TLS_INSECURE=1` ile self-signed / dev fixture'a
  düşüş — `make_client_tls_ctx` + `apply_tls_hostname_check` hem
  `tulpar pkg install` hem `http_request` builtin'inden geçer,
  handshake hatası mesajında `cert verify: …` detayı taşır.
- **Async HTTP client (non-blocking):** `http_request_async(method, url, body)`
  builtin + `lib/http_client.tpr`'de `http_get_async`/`http_post_async`/
  `http_put_async`/`http_delete_async` sarmalayıcıları. Promise döner; istek bir
  worker thread pool'da (`TULPAR_HTTP_POOL`, default 4) koşarken async event loop
  diğer coroutine'leri pompalamaya devam eder — worker yalnız ağ bacağını yapar,
  ana thread tampondan VM nesnesini kurup promise'i settle eder. Loop
  entegrasyonu `aot_io_register` ile (`runtime/tulpar_async.cpp`); sonuç sync
  client ile aynı `{ok, status, headers, body}` zarfı. `examples/37_async_http.tpr`
  (gerçek concurrency, yerel sunucu thread'i) suite'te PASS. Detay: async/await
  bölümü.
- **Kripto / encoding builtins:** `sha1(s)` (20-bayt ikili),
  `sha1_hex(s)` (40 hex), `base64_encode(s)`, `base64_decode(s)`,
  `wings_ws_accept_key(s)` (RFC 6455 §4.2.2 base64+sha1+GUID).
  Saf C++ (`runtime_bindings.cpp`), OpenSSL bağımlılığı yok —
  signed cookies, JWT HMAC, ETag, content-addressed cache vb. için
  ortak yapı taşı. `examples/31_crypto_sse_ws.tpr` FIPS 180-1
  TEST1 ve RFC 6455 §1.3 ws-handshake vektörlerini smoke test
  ediyor.
- **Wings WebSocket upgrade + frame I/O + SSE helpers:**
  `wings_ws_upgrade(req)` 101 Switching Protocols +
  Sec-WebSocket-Accept üretir; `wings_ws_send_frame(fd, opcode, payload)`
  FIN+opcode + length-encoded frame (< 126 / 16-bit / 64-bit
  uzunluk yolları) bir tek `send()` çağrısıyla yazar;
  `wings_ws_recv_frame(fd)` header parse + 7/16/64-bit length
  decode + masking key XOR'unu yapar, `{ok, opcode, fin, payload}`
  veya `{ok=0, error}` döner. `lib/wings.tpr` ergonomic
  wrapper'ları: `wings_ws_send_text/close/pong` +
  `WS_OPCODE_TEXT/BINARY/CLOSE/PING/PONG` sabitleri. SSE tarafında
  `wings_sse_headers()` + `wings_sse_event(name, data)` —
  `text/event-stream` yanıt başlığı + `data: …\r\n\r\n` event
  çerçevesi formatter'ları. `examples/32_wings_ws_frames.tpr`
  round-trip smoke + accept-key vector doğruluyor.
- **Wings streaming dispatcher (SSE / WS keep-alive akış):**
  Handler `{"_stream": 1}` döndürdüğünde wings dispatcher yanıt
  envelope build'ini atlar, soketi force-close eder
  (keep-alive bir stream'i hayatta tutamaz). Handler aktif istek
  fd'sini `wings_current_fd()` ile alır (C-tarafı thread-local,
  her istek başında dispatcher tarafından set ediliyor), sonra
  `socket_send` + `wings_sse_*` / `wings_ws_*` formatter'ları ile
  doğrudan yazar. Sidechannel `_request[k] = …` yazılımından
  bilinçli olarak kaçınıyor — açık wings cookies miscompile'ı
  tetiklememek için. `examples/api_wings_sse.tpr` end-to-end
  smoke: 5 tick SSE stream + done event, `/` + `/healthz` +
  `/metrics` paralel çalışıyor.
- **Datetime + regex stdlib:** `now`, `format`, `parse_iso8601`,
  `weekday`, `date_add_seconds`, `regex_match/search/capture/replace`.
- **CSV + glob + env:** `csv_parse/emit`, `file_glob`, `env()`,
  `keys()`.
- **ORM (`lib/orm.tpr`):** Active-Record over SQLite (define
  table, insert, find, update, delete).
- **Wings prod observability:** `/healthz`, `/metrics` (JSON ve
  Prometheus text), `wings_openapi(title, version)` OpenAPI 3.0
  generator, `log_info/error` structured JSON.
- **Formatter:** indent + trailing-ws + EOF newline + blank-line
  collapse + keyword-aware spacing, idempotent token-pass.
- **Parser multi-error mode:** ilk hatadan sonra panic-mode
  synchronisation (statement-terminator `;`/`}` veya
  statement-başlangıcı keyword'üne kadar atla) ile devam edip
  geri kalan dosyada bağımsız hata bildirimleri üretiyor. Tek
  derleme çağrısında birden fazla `parse error` rapor
  edilebiliyor — `tulpar build` çıkışında "missing `;`"
  diagnosticleri sıralı bir liste halinde geliyor.
- **`tulpar doc` (PR #219):** Markdown reference üretici.
  `tulpar doc <file.tpr>` → fonksiyon + top-level global +
  leading-comment docstring'leri stdout'a Markdown olarak
  basar. `--include-internal` underscore-prefixed isimleri de
  ekler. LSP hover'ı besleyen aynı `aot_check_and_index`
  helper'ından geçer; iki yüzey lockstep.
- **LSP:** init, diagnostics, hover (functions, builtins, AND
  scope-aware variables), completion, go-to-definition,
  find-references, rename, signatureHelp (active-parameter aware
  for both user functions and builtins), **incremental document
  sync** (`change=2`; full-text replace still accepted for
  clients that don't negotiate the cheaper form).
  `tests/lsp_smoke.py` 9/9 check.
- **AOT debug info (Plan 07 Parça A — PR'lar #160–#173):**
  `tulpar [--debug | -g] build <file>` opt-in. LLVM IR'da `!dbg`
  metadata: `DICompileUnit` + `DIFile` + module flags, her user
  fonksiyon (+ main) için `DISubprogram`, her statement için
  `DILocation` (top-level + boxed + typed-int body), her int/boxed
  VMValue local + parameter için `DILocalVariable` + `dbg.declare`,
  top-level + imported-module globals için
  `DIGlobalVariableExpression` + `dbg` metadata kind. Optimizer
  `--debug` modunda `verify` (`-O0`) — 1:1 source mapping korunur.
  `CMakeLists.txt` `TULPAR_LLVM_MAJOR` macro'su LLVM 18 (CI) ile
  LLVM 19 (MSYS2) arası C API rename'leri için hazır. `gdb
  ./binary` step-through + breakpoint + `info locals/variables`
  artık `.tpr` satırlarına çözünüyor.
- **DAP server (Plan 07 Parça B — TulparLang #178/#180/#186/#188/#190
  ve tüm polish bundle'ları #199–#223, ayrıca TulparLang-ext PR #1):**
  `tulpar debug <file.tpr>` stdio JSON-RPC DAP server. AOT-build
  with --debug → spawn `gdb --interpreter=mi3` (cross-platform:
  Windows `CreatePipe` + `CreateProcessW`, POSIX `pipe` + `fork`) →
  background reader thread MI3 records (`*stopped`, `=...`,
  `~`/`@`/`&` stream) parse edip DAP
  `stopped`/`terminated`/`output` event'lerine çevirir.
  Yapılan handler'lar: `initialize`/`launch`/`setBreakpoints`/
  `setFunctionBreakpoints`/`setDataBreakpoints`/`dataBreakpointInfo`/
  `setInstructionBreakpoints`/`configurationDone`/`threads`/
  `stackTrace`/`scopes`/`variables` (struct/array drill-down via
  `-var-create`)/`evaluate`/`setVariable`/`continue`/`pause`/`next`/
  `stepIn`/`stepOut`/`restart`/`terminate`/`disconnect`.
  Breakpoint'lerin tamamı condition + hit-count destekli; logpoint
  `logMessage`'ları `{expr}` interpolation ile detached worker
  thread'de evaluate edilir + `output` event yayınlanıp
  `-exec-continue` ile devam edilir (reader thread'den synchronous
  evaluate deadlock olur, worker thread bunu sidestep eder). MI
  tuple/list splitter `mi_split_list` ile
  `stack=[frame={...},frame={...}]` ve `variables=[{...},{...}]`
  parse edilir. SIGINT disambiguation (`pause` vs `exception`).
  `tests/dap_smoke.py` gdb-missing branch + gdb-present
  setBreakpoints → stack inspection → continue → terminated
  round-trip'i doğruluyor. `vscode-tulpar` v0.4.0
  `contributes.debuggers` (`type: "tulpar"`) + `breakpoints` +
  `tulpar.debug` komutu ile F5 ergonomi.
- **Editör DX turu (2026-06-16) — `vscode-tulpar` v0.5.0:**
  - **LSP completion genişledi** (`src/lsp/builtins.cpp`): Wings helper'ları
    (`ok`/`created`/`not_found`/`bad_request`/`unauthorized`/`forbidden`/
    `conflict`/`server_error`/`no_content`/`text`/`with_status`),
    `get`/`post`/`put`/`del`/`serve` ve `persist` artık tamamlamada. LSP
    smoke (`tests/lsp_smoke.py`) ALL PASSED.
  - **Snippet'ler** (tulpar-ext): `twings`/`tasync` modern stile güncellendi
    (fonksiyon-ref + `req` + `ok()` + `serve()`; gerçek `async func`/`await`),
    13 yeni snippet (`tcrud` tam CRUD, `tget`/`tpost`, `tok`/`tcreated`/`t404`/
    `t400`, `tpersist`, `tt` t-string, `tmatch`, `tawait`/`tgather`/`thttpgeta`).
  - **Grammar** (`tulpar.tmLanguage.json`): t-string `t"..{expr}.."` (embedded
    highlight), `async`/`await`/`match` keyword'leri, 35+ yeni builtin.
  - Stale AOT-only yorumları + `--vm`/`--repl` notları düzeltildi. (`.vsix`
    paketleme `vsce` ile yapılır — bu ortamda kurulu değil.)

### Test harness

- **CI runtime smoke (build.sh + run_tests.ps1):** her
  `COMPILE_ONLY_TESTS` örneği derleme sonrası 2 s spawn edilir,
  `kill -0` (PS: `HasExited`) ile hayatta mı kontrol edilir, kayıtlı
  HTTP probe URL'i varsa (wings/router examples için
  `127.0.0.1:3000/` veya `:8080/`) `curl --max-time 5` ile çağrılır,
  sonra `SIGTERM` ile durdurulur. Probe HTTP responsesı bekleyenden
  farklıysa veya server probe sırasında ölürse FAIL. `continue-on-error`
  mask'i CI workflow'undan kaldırıldı; gerçek bir runtime regresyonu
  iş kırmızı yapar, release'i bloke eder. PR #76 wings cookies
  miscompile gibi 4 PR boyunca main'de yatan regresyonların
  tekrarını önler.

### Benchmark harness

- `benchmarks/ci_run.py` her main push'unda çalışır, RESULTS.json +
  RESULTS.md + README badge bloğu otomatik güncellenir (bench-bot
  PR'ları).
- CPU bench (loopsum, fib): C, Rust, Go, Node, Python karşılaştırma.
- HTTP bench: 5 Tulpar varyantı + Node.js + Python ThreadingHTTP;
  düşük (3000/4) ve yüksek (12000/16) concurrency iki tablo.
- Localhost-only socket bind: Windows Firewall prompt yok.

### Build / release / CI

- CMake-presets (`CMakePresets.json`), MSYS2 OpenSSL default
  detection, AOT linker `--export-all-symbols` / `-rdynamic`
  (kullanıcı fonksiyonları `call()` ile erişilebilir).
- Inno Setup Windows installer (`installer/tulpar.iss`):
  `tulpar.exe` + `libtulpar_runtime.a` `%LOCALAPPDATA%`'a.
- GitHub Actions: docs-only commit'ler step-level skip (gates
  hala "success"), build-linux/macos/windows + benchmark + auto-merge
  chain.
- VS Code marketplace: `vscode-tulpar` v0.3+ (12 snippet, 60+ builtin
  grammar, LSP-aware README).

### Doc + site

- `pkg.tulparlang.dev` Astro static site (Cloudflare Pages) +
  Pages Functions (legacy `<name>/<version>.tpr` resolver proxy →
  `api.pkg.tulparlang.dev`).
- README + landing güncel performans rakamlarıyla.

---

## 🔜 Açık eksikler

Öncelik sırası: 🔴 kritik, 🟡 önemli, 🟢 nice-to-have. Her madde
**"Sıradaki adım"** satırıyla: bir sonraki oturuma cold-start
girildiğinde ne yapacağımı bilelim.

### Dil seviyesi

- ✅ **Float→string en-kısa-round-trip (düzeltildi 2026-07-20).** `toString(1000000.5)`
  → "1e+06" veriyordu (`%g` 6 basamak → kesir kaybı + bilimsel gösterim); float 32-bit
  olduğu için sabit yüksek precision da gürültü gösteriyordu (3.14 → "3.14000010490417").
  Artık aynı float32'ye round-trip eden en kısa ondalık basılıyor (`aot_format_float`;
  toString/print/concat/JSON ortak): 3.14→"3.14", 1000000.5→"1000000.5", 0.1+0.2→"0.3".
- ✅ **String sıralama karşılaştırması (`<`,`>`,`<=`,`>=`) ÇALIŞIYOR (düzeltildi 2026-07-20).**
  `vm_binary_op`'ta string/string çifti `default: VM_BOOL(0)`'a düşüyordu → sıralama
  operatörleri hep false, string dizisi sıralama sessizce hiçbir şey yapmıyordu
  (`==`/`!=` zaten strcmp kullanıyordu). Dördü artık `strcmp` ile lexicographic
  karşılaştırıyor; `"apple" < "banana"` true, string dizisi bubble/insertion sort çalışıyor.
- ✅ **Non-trivially-unboxable struct dönüşü + iç içe struct ÇALIŞIYOR (düzeltildi 2026-07-20).**
  float/string/nested alanlı struct boxed VM_OBJECT'tir; call-site ve `return`
  codegen'i native res-ptr ABI varsayıyordu → `P e = mk(1.5,2.5)`, `push(a, mk())`,
  `arr[i] = mk()` sıfır dönüyordu ve struct-alanlı struct (`Body{V pos}`) hiç
  çalışmıyordu. Native res-ptr yolu artık yalnız `struct_is_trivially_unboxable`'a
  gate'li; diğerleri normal boxed VMValue dönüşü olarak akıyor. İç içe struct
  round-trip yapıyor (`bs[0].pos.x`, `bs[0].mass=99`); float/string/mixed struct
  constructor'dan dönebiliyor. int/bool struct native hızlı yolu değişmedi.
- ✅ **Metot çağrısı her alıcıda ÇALIŞIYOR (düzeltildi 2026-07-20).**
  `recv.method(args)` artık düz kimlik dışında dizi-elemanı (`ents[i].area()`)
  ve çağrı-sonucu (`mk(3,5).area()`) alıcılarda da çalışıyor (eskiden "Null
  closure" çökmesi). Parser `<expr>.<name>(args)`'ı her alıcı için yeniden
  yazar; `resolve_qualified_call` kimlik-olmayan alıcıyı doğrudan metot-dispatch'e
  (`name(recv, args)`) yönlendirir (alias olamaz). Boxed struct alıcı (dizi
  elemanı) struct parametresine unpack edilir (`emit_unpack_boxed_struct_into`).
  Not: field'da saklı closure'ı `.()` ile çağırmak desteklenmez (metot-dispatch
  olur → "fonksiyon bulunamadı").
- ✅ **`call(fn, a, b, …)` N-argüman dispatch ÇALIŞIYOR (düzeltildi 2026-07-20).**
  Eskiden dinamik dispatch yalnızca 0/1 argüman biliyordu; 2+ arg segfault'tu
  (codegen fazlalıkları yutuyor, boxed hedef geçilmeyen pointer'ı çöp okuyordu).
  `aot_call_dynamic_n` + `aot_invoke_boxed_n` (runtime_bindings.cpp) hedefin
  **kayıtlı arity**'sine göre 0–8 arg dispatch eder (eksik→VOID, fazla→atılır,
  wasm tipli `call_indirect` dahil güvenli); codegen `argument_count>=3` dalı +
  typeinfer'de `call` variadic. Wings `call(handler, req)` (tek-arg) regresyonsuz.
  **Sonuç:** callback'ler artık 0-arg olmak zorunda değil. Repro + 76 örnek yeşil.
- ✅ **Dizideki (int/bool) struct'a alan geri-yazma ÇALIŞIYOR (düzeltildi 2026-07-20).**
  `push(ents, mk()); ents[i].x = ents[i].x + 5` artık çalışıyor. Trivially-unboxable
  struct (tümü int/bool) native LLVM aggregate'e iner; diziye girince statik tip
  kaybolur ve `arr[i].x` dinamik string-key araması olur — compact ObjStruct bunu
  karşılamaz. Float-alanlı struct'lar zaten VM_OBJECT olduğu için çalışıyordu; fix
  int/bool struct'ı push/array-literal boxlamasında **aynı string-key'li VM_OBJECT**'e
  çevirir (`box_native_struct_as_object`, llvm_backend.cpp). Statik `s.x` yolu
  değişmedi → regresyon yok (76 örnek + 8 arcade testi yeşil). **Geri çıkarma da
  tamam:** `Ent e = ents[i]` artık saklı alanları değer-semantiğiyle kopyalıyor
  (`aot_struct_unpack_named` — string-key VM_OBJECT'i ada göre çözer, `match`
  öznesinin ObjStruct'ını konuma göre; codegen alan-adı tablosu emit eder).
- ✅ **Lambda + closures (AOT) ÇALIŞIYOR.** `(int a, int b) => a + b`
  lambda'ları, outer-scope capture'lı closure'lar (`make_counter`,
  `make_adder`, sayaç mutasyonu), higher-order ve nested kullanım,
  ayrıca top-level `var f = (..) => ..` hepsi AOT'ta yeşil
  (`tests/closure_basic.test.tpr` 3/3, `tests/closure_capture.test.tpr`
  4/4). Closure modeli: heap env array (slot 0 = parent env, slot 1+ =
  yakalanan değişkenler) + `aot_create_closure`/`aot_call_closure` +
  `analyze_module_captures` capture analizi.
  - **Kök bug (çözüldü):** capture analizi + env codegen vardı ama
    `vm_array_set` 16-baytlık VMValue'yi **değer-geçişiyle** alıyordu;
    SysV/MinGW struct-arg ABI'sinde payload eightbyte düşüyor → her
    yakalanan değişken sessizce 0'a sıfırlanıyordu. Fix: env yazımı
    `vm_array_set_aot_ptr_wrapper` ile **pointer-geçişine** çevrildi
    (`vm_array_push` gibi); ayrıca `codegen_typed_expr`'in
    `AST_IDENTIFIER`'ına ve `AST_VARIABLE_DECL`'e captured-var dalları
    eklendi, `aot_call_closure`'daki `[DEBUG]` print'leri kaldırıldı.
  - **Not:** AOT tek yürütme yolu (AOT-only); VM kaldırıldığı için
    "VM closure paritesi" diye bir iş artık yok.

- ✅ **Pattern matching / `match` ifadesi ÇALIŞIYOR.** Rust-stili
  `match subject { 1 => "a", _ => "f" }`. Hem ifade (değer döndürür,
  `str g = match score {…}`) hem statement pozisyonunda; int/str/bool
  subject + `_` wildcard; arm gövdesi expr veya blok. AOT'ta yeşil
  (`tests/match.test.tpr` 6/6, `examples/33_match.tpr`).
  Pattern'lar: literal, `_` wildcard, `|`-alternatif (`1 | 2 | 3`) ve
  inclusive range (`10 .. 20`). Lowering: subject bir kez değerlendirilir
  (VM'de temp local slot), her atom `==`/range/OR testiyle if-else
  zincirine iner. `match` artık ayrılmış keyword (TR: `eşle`/`esle`),
  `..`=`TOKEN_DOTDOT`, `|`=`TOKEN_PIPE`.
  - ✅ **Destructuring ÇALIŞIYOR (v1.2).** Array `[a, b]`, `[0, x]`
    (literal kısıtı), `[head, ..rest]` (rest binding), `[]`; json object
    `{x, y}` / `{role: "admin", id}`; typed struct varyantları
    `Point{x, y}` / `Circle{r: 0}`. Bare identifier = binding, literal =
    kısıt, `_` = yoksay. Lowering iki aşamalı guard'a iner (tip+uzunluk →
    oku+bind+kısıt); typed-struct subject heap'e box'lanıp alan-indeksiyle
    okunur (`aot_struct_type_is`/`aot_struct_get_field_ptr`), json key ile
    (`vm_get_element`), rest `aot_array_slice` ile. Arm-scoped binding'ler
    bir sonraki arm'a sızmaz. İç içe (nested) destructuring de çalışıyor
    (`{center: {x, y}, radius}`, `[[a, b], [c, d]]`, derin literal kısıt):
    matcher özyinelemeli — her seviye okumadan ÖNCE tip-kontrolü yapıp
    uyuşmazlıkta `fail_bb`'ye dallanır (yanlış-tipli değerden okuma →
    sahte "invalid index" hatası önlenir). `tests/match_destructure.test.tpr`
    12/12, `examples/36_match_destructure.tpr` PASS.

- ✅ **Boxed değer üzerinde tekli `-` ÇALIŞIYOR.** `-o["id"]` /
  `-json_field` (runtime-tipli değer) artık doğru negatifleniyor. Eski
  hata: unary-minus codegen'i VMValue'nun payload'ı (alan 2) yerine
  hizalama pad'ini (alan 1, `[4 x i8]`) çıkarıp 4 byte'ı double'a
  bitcast ediyordu → LLVM "Invalid bitcast". `tests/unary_boxed.test.tpr`
  4/4.

- 🟢 **Generics.** Type system overhaul; `func f<T>(T x): T` parse
  hatası veriyor.
  - **Sıradaki adım:** v1.0 sonrası. Typecheck pre-pass'te `<T>`
    syntax'ını parse + iz sürebilecek şekilde genişlet,
    monomorphization stratejisini seç.

- ✅ **`async/await` (AOT) ÇALIŞIYOR.** `async func f(){ await sleep_async(10);
  return 1; }`, `var p = f(); int r = await p;`. Gerçek kooperatif eşzamanlılık:
  bir coroutine `await`'te askıya alınırken diğerleri çalışır (10ms timer'lı
  task, 20ms'liden önce yerleşir — `examples/34_async.tpr` çıktısıyla doğrulandı).
  `tests/async.test.tpr` 8/8 yeşil, `examples/34_async.tpr` + `examples/35_gather.tpr`
  suite'te PASS.
  - **Mimari (state-machine DEĞİL):** stackful coroutine + event loop
    (`runtime/tulpar_async.{h,cpp}`). POSIX `ucontext` / Windows Fiber ile
    yığın takası → compiler dokunuşu minimal (CPS/state-machine transform yok).
    `async func` boxed `t_<name>(ret*,args*)` ABI'ye zorlanır; çağrı
    `aot_async_spawn` ile coroutine başlatıp promise döndürür; `await`
    `aot_await` ile yield eder; `sleep_async(ms)` bloklamayan timer; program
    sonunda `aot_event_loop_run()` kalan task'ları drene eder.
  - **Promise:** yeni `OBJ_PROMISE` obj tipi (`vm.hpp`), `arena_allocated=1`
    ile ARC erken-free etmez (scheduler raw pointer tutar).
  - ✅ **`gather(...)` ÇALIŞIYOR.** `var r = await gather(a, b, c);` — N promise'i
    eşzamanlı bekler, sonuçları **argüman sırasında** dizi olarak verir (toplam
    süre `max(task)`, `sum` değil). Promise olmayan argümanlar olduğu gibi geçer.
    Bir gather-coroutine her çocuğu sırayla `await` eder; çocuklar zaten kuyrukta
    olduğundan paralel ilerler (`aot_gather`, `runtime/tulpar_async.cpp`).
  - ✅ **Gerçek async I/O — async HTTP client ÇALIŞIYOR.**
    `http_request_async(method, url, body)` (ve `lib/http_client.tpr`'deki
    `http_get_async`/`http_post_async`/`http_put_async`/`http_delete_async`
    sarmalayıcıları) bloklamayan bir promise döner; istek bir **worker thread
    pool** üzerinde koşarken event loop diğer coroutine'leri pompalamaya devam
    eder. Worker yalnızca ağ bacağını (`http_request_url`) yapıp std::string
    doldurur; ana thread tampondan VM nesnesini kurup promise'i settle eder —
    scheduler'a worker'dan dokunulmaz, devir tek atomik bayrakla olur. Loop
    entegrasyonu `aot_io_register(poll, ud)` ile (`runtime/tulpar_async.cpp`):
    `loop_step` her tick I/O kaynaklarını yoklar, hiçbir şey hazır değilken
    `kIoPollMs`=1ms ile bekler. Havuz boyutu 4 (env `TULPAR_HTTP_POOL`).
    `examples/37_async_http.tpr` (gerçek concurrency, yerel sunucu thread'i)
    suite'te PASS; in-flight HTTP sırasında timer-tabanlı coroutine'in
    ilerlediği doğrulandı.
  - ✅ **Async fn parametre limiti 8 → 16.** `call_user_fn` (tek arity-bağımlı
    nokta; codegen zaten flat VMValue array + argc ile çağırıyor)
    `runtime/tulpar_async.cpp` içinde 9–16 case'leriyle genişletildi. 16 param +
    karışık tip doğrulandı (`/tmp` smoke). >16 hâlâ net hata mesajı verir.
  - ✅ **reject/try hata yayılımı ÇALIŞIYOR.** Bir `async func` içindeki
    yakalanmayan `throw` promise'i **reject** eder (state=2); `await` reject'i
    bekleyen tarafta (coroutine veya main) yeniden fırlatır → normal `try/catch`
    ile yakalanır, yakalanmazsa "Uncaught Exception" + exit(1). `gather`
    çocuğunun reject'i de gather-await'inde yayılır. Çözüm: `setjmp/longjmp`
    exception stack'i **coroutine-aware** yapıldı — her coroutine kendi
    `EhContext`'ine sahip (`aot_eh_context_new/free/swap`,
    `src/vm/runtime_bindings.cpp`); `resume()` her dilimde bağlamı takas eder,
    `task_body` bir kök handler kurar (`runtime/tulpar_async.cpp`). Böylece
    askıdaki bir coroutine'in throw'u kardeş bir stack'e longjmp etmez ve
    await-üstü `try` izole çalışır. `tests/async.test.tpr` 12/12 (4 yeni reject
    testi: main'de yakalama, coroutine boyunca yayılım, self-heal, gather
    reject). Senkron `try/catch` davranışı değişmedi (yalnız `eh_main` bağlamı).
  - ✅ **gather reject-yolu ARC temizliği.** Bir `gather` çocuğu reject edince
    `aot_await` throw'u `gather_body`'den longjmp ile çıkıp normal-yol temizliğini
    atlıyordu (retained child arg'ları + kısmi sonuç dizisi sızıyordu). `task_body`
    kök-handler catch dalı artık eşdeğer temizliği yapıyor: N child ref release,
    `items` free, kısmi diziyi (`gs->arr`, `arena_allocated=0 ref=1`) arc_release
    ile serbest bırak, `GatherState` delete. Normal yol değişmedi. **ASan/LSan
    ile kanıtlandı:** 200-iterasyon gather-reject döngüsünde (a) bellek-güvenliği
    çalışması double-free/UAF göstermedi, (b) LSan'de promise'ler (`aot_promise_new`,
    tasarımca arena sızıntısı) bastırılınca **sıfır** kalan sızıntı — yani
    `GatherState`/items/kısmi dizi tamamen serbest bırakılıyor; bastırmasız
    çalışma yalnız 598 promise sızıntısı raporluyor (gather promise'i dahil hepsi
    `aot_promise_new` kaynaklı). Tarama için AOT link adımına env hook eklendi:
    `TULPAR_AOT_LINK_FLAGS` (örn. `-fsanitize=address`) final clang++ link'ine
    forward edilir (`src/aot/aot_pipeline.cpp`); set edilmezse no-op.
  - **Kalan (🟡):** yok — async/await v1 tamam (timer + gather + gerçek async
    HTTP I/O + reject/try, reject-yolu temizliği dahil). Not: `>16` parametreli
    async fn ileride. (VM paritesi yok — AOT-only.)

### Runtime + codegen

- 🟢 **Per-request bellek sızıntısı (Wings hot path) — ÇÖZÜLDÜ (2026-06-17).**
  Benchmark ortaya çıkarmıştı: salt-okuma `GET /users/1` altında bile RSS lineer
  ve sınırsız büyüyordu (~2.6 KB/istek; 6.7→33 MB / 10k istek). **Kök neden:**
  AOT'ta her obje/dizi `malloc`'lanıyor (`arena_allocated = 0`) ve istek
  sınırında ARC/GC sweep yok; arena yalnızca string'leri geri alıyordu →
  container'lar (request objesi, ara dict'ler, response envelope) sızıyordu.
  - **Çözüm — per-request malloc region + runtime write-barrier**
    (`runtime_bindings.cpp`): AOT'ta malloc'lanan obje/dizi LITERAL'leri
    per-thread bir region'a (`g_region` vektör + `g_region_set`) zincirlenir;
    `aot_arena_save` region işaretini snapshot'lar, `aot_arena_restore` o
    işaretten sonraki container'ları (+ kendi malloc buffer'larını) arena
    string-rewind'iyle birlikte serbest bırakır. Yalnızca **arena-scope içinde**
    (checkpoint_top>0) yaratılanlar izlenir → top-level global'ler ve
    `aot_persist`/`string_pin` kopyaları izlenmez, dokunulmaz.
  - **Kaçışlar güvenli:** container mutator'larına (`vm_object_set`,
    `aot_array_push`, `vm_array_set`, `aot_array_set_fast/raw_fast`) **runtime
    write-barrier** eklendi (`wb_persist_escape`): transient bir değer kalıcı bir
    container'a (global / persist-kopyası) yazılırken otomatik derin kopyalanır.
    Value-flow tabanlı olduğu için global'i **local'e alias'layıp** yazmayı da
    yakalar (compile-time check'in kaçırdığı boşluk). Compile-time push/index
    barrier'ları kaldırıldı (runtime üstlendi); whole-var atama barrier'ı kaldı,
    `_request` (transient çerçeve global'i) muaf — eskiden her istekte tüm
    request objesini persist'leyip sızdırıyordu.
  - **Doğrulama:** leak probe'ları 476 MB → düz (~6.7 MB); CRUD `GET /users/1`
    500 istek boyunca RSS düz; **ASan** ağır karışık CRUD altında **0 bellek
    ihlali** (UAF/double-free/overflow yok); tüm örnekler + 6 focused suite yeşil.
  - Yan düzeltme: ham dizi dönen handler'larda (`ok(_users)`) dispatcher
    `result["_stream"]`'i diziyle indeksleyince stdout'a zararsız "Invalid index"
    basıyordu. `vm_get_element` artık diziyi non-int (string) key ile
    indekslemeyi — objedeki olmayan key gibi — **sessizce 0** döndürüyor;
    gürültü kalktı, liste yanıtları çalışmaya devam ediyor.

- 🟢 **Ctrl+C / sıfır-olmayan çıkışta yanıltıcı "compile/link failed"
  mesajı (2026-06-18) — ÇÖZÜLDÜ.** `tulpar script.tpr` derleyip programı
  çalıştırıyor; program sıfır-olmayan çıkınca (ör. sunucuya Ctrl+C) sürücü bunu
  "AOT derleme/baglama basarisiz" sanıp yanıltıcı mesaj basıyordu
  (`aot_compile_and_run_silent` `run_result != 0` için `AOT_ERROR_LINK`
  döndürüyordu). Yeni `AOT_RAN_NONZERO` durumu eklendi: derleme+link zaten
  başarılı, program çalıştı → sürücü çıkış kodunu mesajsız iletir. SIGINT
  (Ctrl+C, sunucu için normal durdurma) `WIFSIGNALED`+`SIGINT` ile temiz çıkışa
  (exit 0) eşlenir. `aot_pipeline.cpp/.hpp` + `main.cpp`.

- 🟢 **Wings cookies miscompile.** Çözüldü ve yeşil yapıldı.
  - **Kök Neden:** AOT/HTTP ayrıştırıcısı (HTTP parser) tarafından oluşturulan yerel HTTP istek nesneleri (request objects/arrays) AOT arena alanında (`aot_arena_alloc`) tahsis edilmektedir ve `arena_allocated = 1` bayrağı taşımaktadır. `lib/wings.tpr` içerisinde bu nesnelere ardışık yeni anahtarlar eklendiğinde (`_request["params"]`, `_request["cookies"]`), nesnenin kapasitesi dolduğu için runtime üzerindeki `vm_object_set` veya `vm_object_set_aot_wrapper` fonksiyonları nesneyi yeniden boyutlandırmaya (resize) çalışıyordu. Orijinal kod, arena üzerinde tahsis edilmiş olan bu nesnelerin `keys`/`values` veya `items` işaretçilerini standart `realloc` ile büyütmeye çalışıyordu. Arena bellek alanları doğrudan `malloc` ile alınmadığı için, `realloc` çağrısı doğrudan heap bozulmasına (`STATUS_HEAP_CORRUPTION` - `0xC0000374`) ve çökmeye yol açıyordu.
  - **Çözüm:** `src/vm/runtime_bindings.cpp` dosyasındaki `vm_object_set`, `vm_object_set_aot_wrapper`, `vm_array_push_aot_wrapper` ve `aot_array_push` fonksiyonları güncellenerek `arena_allocated` bayrağı kontrol edildi ve bu nesnelerin kapasiteleri büyütülürken `realloc` yerine `aot_arena_alloc` kullanılarak elemanlar yeni bir arena bloğuna kopyalandı. `lib/wings.tpr` dosyasında `_request["cookies"] = wings_cookies(req);` satırı başarıyla geri yüklendi ve tüm AOT web testleri başarıyla geçmektedir.

- 🟢 **WebSocket recv multi-call calling-conv quirk** (PR #231
  development sırasında çıktı, doğrudan kullanıcı etkisi yok ama
  smoke'da multi-frame round-trip yazamadık): ardışık
  `wings_ws_recv_frame(fd)` çağrılarında ikinci çağrı `fdVal`
  parametresini ilk çağrıdan farklı (server thread'in accepted
  fd'si) görüyor. Debug print'lerinde `[recv] enter fd=344` sonrası
  `[recv] enter fd=356` çıkıyor. Tek-frame round-trip çalışıyor
  (`examples/32_wings_ws_frames.tpr` PASS).
  - **Sıradaki adım:** AOT'da `wings_ws_recv_frame` dispatch
    site'ına bak — VMValue 1-arg fn ABI'si MinGW64 SysV-shaped
    struct'da sıkışıklık yaşıyor olabilir (aot_socket_close'a
    yapılan ABI fix'inin benzeri). Şu an `string_pin_type`
    kullanılıyor; tek-i64 alan ayrı bir signature deneyebilir,
    veya çağrı taraflı `LLVMBuildAlloca` + value-by-pointer
    ABI'ye geç.
  - **İnceleme notu (2026-06-29):** runtime impl (`aot_wings_ws_recv_frame`)
    doğru; semptom **yalnızca MinGW64/Windows** ABI'sinde (ikinci çağrı
    `fdVal`'i yanlış slottan okuyor). **Linux WSL'de reprodüke/dogrulanamaz**
    olduğundan ve doğrulama kapısı zorunlu olduğundan, çalışan Linux yolunu
    bozma riskiyle körlemesine ABI değişikliği yapılmadı — Windows test
    erişimi gelene kadar açık bırakıldı.

- 🟢 **VM'de native struct desteği yok.** `tests/struct_native.test.tpr`
  altında 17/19 yeşil ama `_translate_x(Point p, int dx)` gibi
  "typed struct param + field mutation + return" yolu fail ediyor:
  expected 101 got 1 — yani mutasyon hiç oluşmadan dönüyor. AOT
  Plan 04 native struct codegen'i tamam, VM tarafında karşılığı yok
  (her şey boxed VMValue üzerinden ilerliyor).
  - **Karar (2026-06-29) — YAPILMAYACAK (ölü madde).** Proje AOT-only
    (2026-06-15); "VM için özellik yeniden yazma" proje kararına aykırı.
    `src/vm/` artık yalnızca paylaşılan runtime, yürütme motoru değil — hiçbir
    kullanıcı struct'ı VM yolundan geçmiyor. Bu madde STATUS'ta tarihsel kayıt
    olarak kalıyor; iş listesinden düşürüldü.

- ✅ **AOT bool→int variable-decl coercion divergence (ÇÖZÜLDÜ).**
  `int ok = db_execute(...);` artık AOT'ta da int olarak tutuluyor
  (`toString` "1"). Fix `llvm_backend.cpp` AST_VARIABLE_DECL boxed
  yolunda (~5474): `node->data_type == TYPE_INT` ise boxed VMValue'nun
  type-tag'i `VM_VAL_BOOL` → `VM_VAL_INT` olarak yeniden yazılıyor
  (slot 2 payload her iki tag için de aynı int olduğundan kayıpsız).
  Not: ilk hipotez (i1→i64 zext) yanlıştı — `INFERRED_BOOL` değerleri
  `codegen_typed_expr`'de zaten i64; sorun store değil type-tag idi.

### Stdlib DX (Wings/ORM)

- ✅ **Tame WASM — `tulpar build --target=web` (2026-07-13):** derleyici
  wasm32-unknown-emscripten objesi üretir (CMake her mimaride WebAssembly
  LLVM bileşenlerini bağlar), em++ `wasm/dist` arşivlerine linkler
  (`wasm/build_tame_web.sh`: async'siz runtime + raylib PLATFORM_WEB +
  tame) → `oyun.html + .js + .wasm`; ASYNCIFY bloklu oyun döngüsünü
  tarayıcı event-loop'una çevirir. ⚠️ wasm32'de VMValue C ABI'si Win64
  gibi sret+byval — `vmvalue_abi_uses_sret()` çalışma zamanında seçer ve
  bayrak `llvm_backend_create`'ten ÖNCE kurulmalı (declare create içinde;
  aksi halde wasm'da bozuk bitcast-thunk'ları üretilir — canlı yakalandı,
  kapatıldı). HTML kabuğunu em++ değil Tulpar yazar (npm bağımlılığı yok).
  Varlıklar `TULPAR_WEB_ASSETS=<dizin>` ile gömülür. **Tarayıcıda canlı
  doğrulandı (2026-07-14, headless Chromium + swiftshader WebGL):**
  `tame_web_mini` (elle döngü), `tame_sprite_demo` (gömülü ship.png/beep.wav —
  texture + ses yüklendi) ve `tame_run_demo` (yönetilen `run()` döngüsü) üçü de
  60 FPS render ediyor; global durum kareler arası korunuyor.
  ⚠️ **`run()`/`call()`/wings `listen()` web'de bozuktu — düzeltildi
  (2026-07-14):** isimle dağıtım (`aot_call_dynamic`) native'de `dlsym(RTLD_
  DEFAULT)` + `-rdynamic`'e dayanıyordu; statik wasm modülü dlsym'e sembol
  tablosu açmadığından `run(guncelle, ciz)` her karede "Function not found
  't_...'" veriyordu. Çözüm: AOT backend main prolog'una her boxed top-level
  fonksiyon için `aot_register_func(ad, ptr, arity)` emit ediyor → call()
  önbelleği dlsym olmadan dolduruluyor (native de artık `-rdynamic`'ten
  bağımsız). Ayrıca wasm `call_indirect` katı tip denetimi yüzünden 0-param
  hedefi 2-ptr imzayla çağırmak "function signature mismatch" trap'ı veriyordu;
  önbellek artık **arity** tutuyor ve dağıtıcı (`aot_invoke_boxed`) hedefin
  gerçek imzasıyla eşleşen çağrıyı seçiyor. Önbellek 256→2048 slot (512-fonk
  codegen sınırının üstünde).
  ⚠️ **Ses açan oyunlar web'de çöküyordu — düzeltildi (2026-07-14):** raylib
  ses arka ucu (miniaudio) ScriptProcessorNode geri-çağrısında
  `Module.HEAPF32.buffer` okur; Emscripten 5.0 HEAP view'lerini artık Module'e
  otomatik bağlamadığından `Module.HEAPF32` undefined → `load_sound`/
  `load_music` kullanan her oyun her audio-frame'de "Cannot read properties of
  undefined (reading 'buffer')" ile çöküyordu. Çözüm: web link satırına
  `-sEXPORTED_RUNTIME_METHODS=HEAPF32` (aot_pipeline.cpp) — büyümede otomatik
  yeniden atanır, sessiz oyunlara zararsız. Detay: docs/mindmap/Tame.md
  "Web hedefi".
- ✅ **Tame Faz 0-5 + gamepad — çekirdek + çizim + girdi (klavye/fare/
  gamepad) + sprite/font + ses/müzik + `run()` yönetilen döngü: TAMAMLANDI
  ve pencereli canlı doğrulandı (v3.10.0, 2026-07-12/13).** Adlı gamepad:
  `gamepad_available/name/down/pressed/axis` — buton/eksen adla ("A"/"LB"/
  "START", "LX"/"RT"; PS eş anlamlıları "CROSS"... dahil); donanımsız zarif
  yol (false/""/0.0) doğrulandı. Detay: Yapılanlar → Stdlib + tooling → "Tame".
- ✅ **Wings/ORM yazım kolaylığı turu — TAMAMLANDI (v3.7.0, 2026-07-03).**
  [WINGS_DX.md](WINGS_DX.md)'deki dört ayak da uygulandı; detay:
  Yapılanlar → HTTP runtime → "Wings/ORM DX turu". Testler:
  `tests/orm.test.tpr` 11/11, `tests/wings_dx.test.tpr` 10/10 + canlı curl.
  Faz-0 doğrulamaları: `delete` fn adı ✓, orm+wings çift import ✓, lambda
  route **kaydı** derleniyor ✓ (canlı dispatch'i ayrıca doğrulanmadı — docs
  funcref stilini öğretiyor), param-binder bool'u native bind ediyor ✓.
- ✅ **`where()`/`all()` opsiyonel `order`/`limit` (2026-07-04) — ORM v2
  sonrası ilk aday, kapatıldı.** `lib/orm.tpr`: yeni `_orm_clause(opts)`
  yardımcı fonksiyonu (`typeof(opts) != "object"` ise no-op — default-arg
  padding'in boxed-0'ını sessizce yutar). `where(m, cond, params, opts)` ve
  `all(m, opts)` — her ikisinde de `opts` opsiyonel 4./2. arg:
  `Note.where("done = ?", [0], {"order": "id DESC", "limit": 20})`,
  `Note.all({"order": "id ASC", "limit": 10})`. Eski 3-arg `where()` /
  0-arg `all()` çağrıları değişmeden çalışır (typeinfer eksik trailing arg'ı
  serbest bırakıyor). `order` ham SQL parçasıdır (`raw()` gibi kullanıcı
  girdisini gömme uyarısıyla belgelendi); `limit` `toInt(toString(...))` ile
  coerce edilir. `tests/orm.test.tpr` yeni test + tümü **13/13**; tüm örnek
  + `wings_dx.test.tpr` 10/10 regresyon temiz. `WINGS_CHEATSHEET.md`
  güncellendi.
  - **Kalan (🟢, talep görürse):** ilişkiler (`has_many`), migration
    sistemi, tipli query-builder (`Note.q().eq(...)`), `where`'siz
    `first(m)` overload'u. WINGS_DX.md "Kapsam dışı" bölümünde gerekçeli.

### Tooling

- 🟢 **JetBrains plugin — YAPILMAYACAK (karar 2026-07-04).** Kullanıcı kararı:
  VS Code eklentisi (`vscode-tulpar`) tek IDE hedefi olarak yeterli;
  IntelliJ/JetBrains desteğine gerek yok. İş listesinden düşürüldü.

### Ağ / I/O

- 🟢 **HTTP/2.** HTTP/1.1 keep-alive + multi-thread bugünkü iş
  yüklerini karşılıyor.
  - **Sıradaki adım:** v1.x sonrası. nghttp2 entegre etmek
    (`wings_h2(port, …)` listener) veya saf C HPACK decoder —
    karar gerekli.

### Pkg ekosistemi (içerik)

- 🟡 **Gerçek 3rd-party paket içeriği.** Registry'de `demo` + `multipkg`
  smoke-test paketlerinin yanında artık **`wings_jwt` canlı ve onaylı**.
  Stdlib paketleri (`wings`, `router`, `http_utils`, vb.) hâlâ registry'de
  publish edilmemiş.
  - ✅ **İlk gerçek paket oluşturuldu VE canlıya yayınlandı: `wings_jwt`
    (oluşturma 2026-06-29, publish 2026-07-04).**
    `packages/wings_jwt/` — HS256 imzalı oturum token'ları (`sign`/`sign_ttl`/
    `verify`/`decode`/`from_header`), sıfır bağımlılık, `hmac_sha256` +
    `base64_encode` üstüne saf Tulpar. 8/8 test yeşil, örnek wings app derleniyor.
    Publish akışı: production `tulpar-be` sunucusunda (`/root/tulpar-be/.env`)
    `PUBLISH_TOKEN` üretilip `TULPAR_PUBLISH_TOKEN` ile
    `tulpar pkg publish` → `HTTP 201`; ardından
    `POST /v1/admin/packages/wings_jwt/approve` ile Faz D onay modelinin
    varsayılan `approved=0` gizliliği kaldırıldı. `GET /v1/packages/wings_jwt`
    artık `{"latest":"0.1.0",...}` dönüyor — `tulpar pkg add wings_jwt`
    herkese açık çalışıyor. Bunu mümkün kılan runtime primitifi
    **`hmac_sha256(key, msg)`** (RFC 4231 vektörleriyle doğrulandı) —
    bkz. Yapılanlar / Çekirdek + [[tulpar-v330-security-apis]].
  - **Sıradaki adım:** ek paketler yayınla — `wings_postgres` vb. (henüz
    yazılmadı; en azından tasarım/kapsam kararı gerekir).

- 🟢 **Server-side `/v1/search` endpoint.** Bugün `pkg search`
  client-side filtreliyor — registry catalog'u büyüdüğünde
  server-side full-text + featured-flag endpoint'ine geçmek gerek.
  Şu anki paket sayısı (2) ile gereksiz.
  - **Sıradaki adım:** N>50 paket olduğunda; tulpar-be'ye
    `/v1/search?q=…` ekle, SQLite FTS5 ile.

- ✅ **Registry binary release asset support (2026-07-05) — CLI + registry tarafı, canlıya deploy edildi.**
  - ✅ **Manifest şeması (`src/pkg/manifest.hpp/.cpp`):** `[release.binaries]`
    (bu paketin kendi platform→URL eşlemesi — `linux-x64`/`windows-x64`/
    `macos-universal`, `tulpar update`'in kullandığı aynı platform id'leri)
    + `[binaries]` (tüketici tarafı opt-in tablo, `name = true`). Round-trip
    parse/serialize doğrulandı.
  - ✅ **`tulpar pkg add <name> --binary`:** dependency'yi hem
    `[dependencies]` hem `[binaries]`'e ekler.
  - ✅ **`tulpar pkg publish`:** `[release.binaries]` doluysa JSON gövdeye
    `release_binaries` alanı olarak ekler; **her URL `is_safe_binary_url()`
    ile doğrulanır** (yalnızca düz http(s), tırnak/backtick/`$`/`&`/`;`/boşluk
    yok) — kontrol `--dry-run` dahil her zaman çalışır, çünkü indirme tarafı
    `curl`'u shell string'e gömerek çağırıyor ve URL üçüncü-parti bir
    publisher'dan geliyor (command-injection riski gerçek — kanıtlandı: bir
    `"; touch /tmp/pwned; #` payload'ı publish denemesinde reddedildi, dosya
    oluşmadı).
  - ✅ **`tulpar pkg install`:** `[binaries]`'te opt-in edilmiş bağımlılıklar
    için ekstra `GET /v1/packages/<n>/versions/<v>` (metadata, `/source`
    değil) çağrısı → `release_binaries[current_platform_id()]` varsa indirir
    (`tulpar_modules/<n>/bin/<n>[.exe]`, POSIX'te `chmod 0755`). Opt-in
    yoksa **sıfır ekstra istek** — mevcut davranış değişmedi. Kayıt yoksa/
    indirme başarısızsa install'ın geri kalanını **düşürmez**, bilgilendirici
    satır basar.
  - ✅ **tulpar-be (`registry.tpr`, ayrı repo) — kodlandı ve canlıya deploy
    edildi (commit `71c7257`):** `packages` tablosuna `release_binaries TEXT DEFAULT '{}'`
    kolonu (CREATE TABLE + var olan DB'ler için idempotent `ALTER TABLE`
    migration'ı, `disabled`/`approved`/`repo`/`ref` ile aynı desen).
    `publish()` opsiyonel `release_binaries` alanını kabul eder, sunucu
    tarafında da `_release_binaries_valid()` ile doğrular (400 döner —
    CLI'yi atlayıp doğrudan POST eden bir publisher için savunma katmanı),
    `_get_version()` alanı metadata yanıtına ekler.
  - **Doğrulama:** lokal `tulpar registry.tpr` (scratch SQLite + blob dir)
    ile canlı uçtan uca test edildi — publish→approve→
    `GET .../versions/0.1.0` `release_binaries` doğru döndü; kötücül URL
    400 ile reddedildi; `release_binaries`'siz publish + var olan tohum
    paketler (`demo` vb.) etkilenmedi. CLI tarafı ayrıca gerçek production
    registry'ye (henüz bu alanı sunmayan) karşı test edildi — nazikçe
    "no published binary" diyerek source install'ı bozmadı (ileri-uyumlu
    çöküş). `./build.sh test` + `tests/orm.test.tpr`/`wings_dx.test.tpr`
    regresyon temiz (bu iş `lib/*.tpr` dokunmadı, C++-only).
  - ✅ **Canlıya deploy edildi (2026-07-04, kullanıcı onayıyla).** tulpar-be
    main'e commit+push edildi (`71c7257`), GitHub Actions deploy workflow'u
    tetiklendi. Workflow'un health-check adımı ilk denemede "failed"
    raporladı (60 sn boyunca `connection refused`) — kök neden muhtemelen
    sunucudaki `deploy/auto-deploy.sh` cron fallback'inin (her dakika)
    GH Actions'ın SSH deploy'uyla aynı ana denk gelip container'ı ikinci kez
    `docker compose up -d --build` ile yeniden başlatması; benim değişikliğimden
    kaynaklı bir regresyon değil. **Doğrulama:** `curl https://api.pkg.tulparlang.dev/healthz`
    → `uptime_s` kesintisiz artıyor (crash-loop yok), `GET /v1/packages/demo/
    versions/1.0.1` ve `wings_jwt/versions/0.1.0` artık `"release_binaries":{}`
    döndürüyor — yeni kod canlıda kararlı çalışıyor, mevcut paketler
    etkilenmedi. **Kalan (🟢, talep görürse):** gerçek bir binary-shipping
    paket (ör. `wings_jwt` derlenmiş bir CLI aracıysa) ilk denemeyi yapabilir;
    ayrıca GH Actions health-check script'i cron fallback'iyle yarışma
    ihtimaline karşı sağlamlaştırılabilir (ayrı bir altyapı işi, bu turun
    kapsamı dışında).

### Dokümantasyon

- 🟡 **`tulparlang.dev/docs/` deep dives.** Docs sitesi (`tulpar-lang-web`,
  Astro **Starlight**, EN+TR bilingual) zaten kapsamlı: Introduction,
  Language Guide, Standard Library, Ecosystem & Tooling, Reference. Bu
  turda kapatılan boşluklar:
  - ✅ **async/await dokümantasyonu** — yeni `guide/async.mdx` (EN+TR):
    async func/await, `sleep_async`, `gather`, reject/try, bloklamayan
    HTTP client, sınırlar (stackful coroutine, 16 param, AOT-only),
    async-vs-thread tablosu. Sidebar'a eklendi. `guide/concurrency`'nin
    artık yanlış "no green-thread runtime" iddiası düzeltilip async'e
    cross-ref verildi. `ecosystem/http-client`'a async bölümü.
  - ✅ **Derleyici flag + env-var matrisi** — `reference/cli.mdx` (EN+TR):
    per-invocation flag tablosu (`--aot/--build/--debug/--no-typecheck/
    --strict/--vm/--repl`) + kategorize env-var matrisi (~16 değişken,
    kaynaktan doğrulandı). `TULPAR_LANG` aslında implement değil →
    gerçek mekanizma `LANG`/`LC_*` olarak düzeltildi.
  - ✅ **`reference/language.mdx` syntax highlighting** — ```tpr fence'leri
    kayıtlı ```tulpar diline çevrildi (highlight artık çalışıyor).
  - ✅ **OS-spesifik kurulum kılavuzları** — `intro/installation.mdx` (EN+TR)
    "Build from source" bölümüne per-OS toolchain kurulumu eklendi
    (Ubuntu/Debian apt `llvm-18-dev`, Fedora dnf, Arch pacman, macOS Homebrew
    + `LLVM_DIR` keg-only gotcha, Windows MSYS2 — resmi LLVM installer'ın dev
    lib'leri içermediği notuyla). CI workflow'undan doğrulandı. "native MSVC"
    yanlış çerçevesi düzeltildi (build.bat MSVC↔MSYS2 auto-detect eder).
  - ✅ **Dil reference güncel** — `reference/language.mdx` (EN+TR) yeni
    özelliklerle: **match destructuring** (dizi `[head, ..tail]`, json/struct
    alanları `{role: "admin", name}`, tipli varyant `Circle{r}`, iç içe) ve
    yeni **async/await** bölümü (`examples/36_match_destructure.tpr`'den
    doğrulandı). TR reference'ta `match` bölümü tamamen eksikti → eklendi
    (EN paritesi).
  - ✅ **Kriptografi & güvenlik bölümü (2026-06-29, EN+TR)** —
    `stdlib/builtins.mdx`'e yeni bölüm: `sha256` / `hmac_sha256` /
    `password_hash` / `password_verify` / `secure_token` / base64, hangisini
    nerede kullan rehberiyle + `wings_jwt` örneği. (Daha önce crypto
    builtin'lerinin hiçbir doc bölümü yoktu — gerçek bir boşluktu.) Astro
    build temiz (72 sayfa, EN+TR indexlendi).
  - ✅ **JWT-auth cookbook tarifi (2026-07-04, EN+TR)** —
    `ecosystem/wings-cookbook.mdx`'e yeni "How do I add JWT authentication?"
    / "JWT authentication nasıl eklerim?" bölümü: canlı yayınlanan
    `wings_jwt` paketi (`tulpar pkg add wings_jwt`, `jwt.sign_ttl`) + Wings'in
    yerleşik `jwt_guard`/`jwt_public` middleware'i birlikte — daha önce
    `jwt_guard`/`jwt_public` sitede hiç dokümante edilmemişti (gerçek
    boşluktu). Tarif gerçek bir sunucuda derlenip `curl` ile uçtan uca
    doğrulandı (401 token'sız, hatalı kimlik bilgisinde 401, login→token→
    `/me` claim'leri doğru). `astro build` sonrası 72 sayfa temiz.
  - **Kalan:** pkg/wings derinlemesine başka guide'lar genişletilebilir.
    **Site canlı deploy (Cloudflare Pages/wrangler) — dışa-dönük, credential
    gerekir:** `astro build` lokalde temiz çalışıyor (`./node_modules/.bin/astro
    build` → `dist/`);
    deploy adımı kullanıcının CF hesabı/wrangler auth'unu istiyor
    (`wrangler pages deploy dist`). v1.0 release blocker'ının büyük kısmı kapandı.

---

## 🎯 Olgunluk kriterleri (sürüm bağımsız)

> **Sürümleme notu (2026-06-18):** Proje **v1.0'ın çok ötesinde** — yayınlı
> son tag **`v3.0.0`** (2026-06-15, AOT-only breaking change). main onun 8
> commit önünde; sıradaki yayın **`v3.1.0`** (geriye-uyumlu feature'lar +
> fix'ler, bkz. `CHANGELOG.md`). `CMakeLists.txt` `project(VERSION 3.1.0)`
> ile hizalandı (önceden 2.1.0'da takılıydı → dev build'ler en son release'in
> gerisini raporluyordu). Aşağıdaki kriterler "dil olgun mu?" kalıcı
> gate'leridir, bir sürüm numarasına bağlı değil.

1. **Sıfır bilinen davranış regresyonu** — CI compile + runtime gate'leri
   yeşil (`./build.sh test` 48/48 örnek + 27 focused suite yeşil).
2. **Motto taşınıyor** — bench'ler C/Rust sınıfında (loopsum 3×C içinde,
   HTTP Node'u 2×+ geçiyor); örnekler Python kadar okunur.
3. **Ekosistem self-host** — tulpar-be prod'da, kullanıcı `tulpar pkg
   add foo@^1` ile çalışan dep ekleyebiliyor (✓ teknik altyapı; içerik
   eksik).
4. **Dokümantasyon eşiği** — quickstart + reference + pkg guide
   `tulparlang.dev/docs/` altında canlı.
5. **Stable release süreci** — `v*` git tag + binary release artifact'leri;
   `tulpar update` bunu çekiyor (✓ v2.2.0 + v3.0.0 yayınlandı).

**Şu an konumumuz:** v3.0.0 yayınlı, **v3.1.0 yayına hazır**. (1) ve (2)
karşılandı (tam suite yeşil). (3) altyapı tam, içerik az. (4) reference büyük
ölçüde hazır, canlı deploy eksik. (5) süreç işliyor — v3.1.0 tag'i atılmayı
bekliyor.

---

## Notlar

- Bilinen ufak konular:
  - ✅ **Parse error satır no off-by-one — ÇÖZÜLDÜ (2026-06-29).** EOF'taki
    bir hata (kapanmamış ifade vb.) lexer'ın trailing-newline sonrası
    artırdığı satır sayacını taşıyordu → 4 satırlık dosyada "line 5" + boş
    fantom excerpt. `render_parse_error_pretty` artık `clamp_line_to_source`
    ile raporlanan satırı içerik taşıyan son satıra sıkıştırıyor (lexer'a
    dokunulmadı → başka diagnostik kaymadı). LSP sink yolu da aynı clamp'i
    kullanır. Doğrulandı: EOF hatası artık doğru satırı + excerpt'i gösteriyor,
    orta-dosya hataları değişmedi, `errors/ternary/match/strings` suite'leri
    yeşil.
  - ✅ **`[AOT]` progress mesajları artık opt-in — ÇÖZÜLDÜ (2026-06-29).**
    `tulpar build` artık yalnız `[AOT] Successfully created: <exe>` (+ hata)
    basıyor; ara adımlar (Parsing/Codegen/Optimizing/Linking + "O3 applied")
    `TULPAR_AOT_VERBOSE=1` arkasına alındı (`aot_pipeline.cpp` `AOT_PROGRESS`
    makrosu + `llvm_backend.cpp` O3 satırı env-gate). Silent run yolu zaten
    temizdi. Doğrulandı: quiet vs verbose çıktı.
  - `assert_throws` çalışıyor; **string `==` incelendi (2026-06-29) — gerçek
    bug değil:** literal/concat/UTF-8/obje-değeri/dizi-elemanı eşitliği ve
    cross-type karşılaştırmalar (int↔str implicit coerce yok — statik dil için
    doğru) tutarlı çalışıyor. Belgelenen "toString roundtrip" edge case'i
    reprodüke edilemedi; not güncellendi.
- `tulpar-be` ayrı bir repo (`d:/yazilim/tulpar-be`); Tulpar dilinde
  yazılmış (`registry.tpr`, ~60 KB), Docker'da deploy.
- `pkg.tulparlang.dev` Astro static site, ayrı bir repo.
- `EKSIKLER.md` ve `OZET.md` (eski) bu dosyada toplandı, repodan kaldırıldı.

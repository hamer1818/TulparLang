# Changelog

All notable changes to TulparLang are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/): MAJOR for breaking
language/stdlib/ABI changes, MINOR for backwards-compatible features, PATCH for
fixes. Releases are cut by pushing a `v*` tag (see [RELEASING.md](RELEASING.md));
`tulpar --version` reports the tag at release time and `<version>-dev` otherwise.

## [Unreleased]

### Added — 3D kamera: yörünge ve birinci şahıs (Faz 6)

3B sahnenin kamerası bugüne kadar **hiç dönmüyordu**: oyuncunun sabit +Z
arkasında durup onu izliyordu, dolayısıyla W tuşu her zaman dünya ekseninde
-Z demekti. Oynanabilir tek tür, tek açıdan izlenen sahnelerdi — oyuncu
etrafına bakamıyor, arkasını dönemiyordu. Faz 6 kamerayı serbest bırakıyor.

- **Üç kamera modu** (`scene3d`): `camera_follow(hedef, mesafe, yukseklik)`
  eskisi gibi SABİT; `camera_orbit(...)` / `kamera_yorunge(...)` üçüncü şahıs
  yörünge; `camera_fps(hedef, goz_yuksekligi)` / `kamera_fpv(...)` birinci
  şahıs. Yörünge, `camera_follow` ile **birebir aynı açıdan başlar** (aynı
  mesafe/yükseklik küresel koordinata çevrilir) — fark, artık döndürebilmen.
  Birinci şahısta hedef entity **çizilmez** (kamera gövdenin içindedir) ve
  gövde bakış yönüne döner.
- **Hareket artık kameraya göre:** `move3d` yön girdisini kamera yaw'ıyla
  döndürüyor — "ileri" kameranın baktığı yön. Sabit kamerada yaw hep 0 olduğu
  için dönüşüm birim matris kalır, yani **mevcut sahnelerin kontrolü ve
  görüntüsü değişmez**.
- **Bakış girdisi üç yoldan:** fare (birinci şahısta imleç kilitli, sürekli;
  yörüngede SAĞ tuş basılıyken sürükleme), ekranın **sağ yarısına parmakla
  sürükleme** (mobil — dokunup çekmek hâlâ zıplatır, çünkü zıplama parmak
  KALKINCA ve parmak kaymadıysa tetiklenir), ve **Q/E** klavye yedeği
  (tarayıcı Pointer Lock vermediğinde de kamera çevrilebilsin). Tekerlek
  yörüngede yakınlaştırır. Eğim ±85°'de kırpılır (kamera takla atmasın),
  yörüngede üst sınır 25° ve kamera zeminin altına düşürülmez.
- **Yeni API:** `camera_sens3d` / `camera_touch_sens3d` (hassasiyet),
  `camera_look3d(yaw, pitch)` (sahneyi çerçevele), `camera_yaw3d()` /
  `camera_pitch3d()` / `camera_dist3d()` (okuma), `camera_zoom3d(min, max)`,
  `camera_lock3d(on)` (imleç kilidi) — hepsinin Türkçe alias'ı var.
  `scene3d_reset()` kamerayı da sıfırlar.
- **4 yeni tame builtin:** `tm_mouse_dx` · `tm_mouse_dy` (kare içi fare
  deltası — imleç kilitliyken `mouse_x()` sabit kaldığı için bakışın tek girdi
  kaynağı) · `tm_cursor_lock` · `tm_cursor_locked`. Sarmalayıcılar:
  `mouse_dx`/`fare_dx`, `mouse_dy`/`fare_dy`, `cursor_lock`/`imlec_kilitle`,
  `cursor_locked`/`imlec_kilitli`. Web'de imleç kilidi tarayıcının Pointer
  Lock'ıdır; pencere yokken (headless) sessiz no-op.
- Örnek: `examples/scene3d_camera.tpr` — 1/2/3 ile üç mod canlı değiştirilir.

### Fixed — tuş sorgusu sayısal kodu yok sayıyordu

`key_down(87)` gibi **ham raylib tuş kodu** verilen çağrılar her zaman `false`
dönüyordu: binding argümanı yalnız string ad ("W", "SPACE") olarak açıyor,
sayı gelince `NULL`'a düşüp tuşu "basılı değil" sayıyordu. Kendi tuş
sabitlerini tutan `scene3d`'nin (`K_W = 87`, `K_LEFT = 263` …) **tüm klavye
girdisi bu yüzden ölüydü** — 3B sahneler yalnız dokunmatikle sürülebiliyordu.
`tm_key_down` / `tm_key_pressed` / `tm_key_released` artık ad VEYA kod kabul
ediyor (tip runtime'da ayrılıyor).

### Added — 3D doku, gökyüzü ve materyal (Faz 5)

3B sahneler artık **düz renk olmak zorunda değil**: yüzeylere doku döşenebiliyor,
arkalarında gradyan bir gökyüzü var ve parlaklıkları ayarlanabiliyor.

- **Doku bir DURUM'dur:** `texture3d(t, tile_u, tile_v)` / `doku3d(...)` bir kez
  ayarlanır, sonraki her primitif (kutu/küre/silindir/düzlem) onu kullanır;
  `no_texture3d()` / `doku3d_kapat()` düz renge döner. `tile`, dokunun yüzey
  boyunca kaç kez tekrarlanacağı — 60×60 birimlik bir zemine tek bir 128×128
  karo dokusunu 24×24 döşemek böyle olur. Renk (çizim çağrısının son
  parametresi) dokuyla **çarpılır**: beyaz ver, doku olduğu gibi çıksın; renkli
  ver, dokuyu boya. Modeller kendi dokularını korur (`model_texture`) — GLB
  materyalini ezmek yıkıcı olurdu.
- **Prosedürel damalı doku:** `checker(w, h, cells, c1, c2)` / `damali(...)`
  dosyasız doku üretir ve normal bir doku handle'ı döner (2B'de de kullanılır).
  Motorun geri kalanı (gömülü shader'lar, `gen_*` primitifleri) gibi asset
  gerektirmeme çizgisinde: kimse basit bir zemin karosu için PNG taşımasın.
- **Materyal:** `material3d(shine, spec)` / `materyal3d(...)` — `shine`
  specular üssü (büyük = küçük ve keskin parlama, "cilalı"; küçük = geniş ve
  yayvan, "mat"), `spec` parlamanın gücü (0 = tamamen mat). Hazır iki uç:
  `mat3d()` / `matte3d()` ve `parlak3d()` / `glossy3d()`. Varsayılan 16 / 1.0,
  yani **Faz 5 hiçbir mevcut sahnenin görüntüsünü değiştirmez.**
- **Gökyüzü:** `sky(tepe, ufuk)` / `gokyuzu(...)`, `sky_off()` /
  `gokyuzu_kapat()`. Cubemap veya 6 resim yok — kameranın konumunda duran büyük
  bir kürenin rengi **bakış yönüne** göre hesaplanıyor, yani yukarı bakınca
  zenit, aşağı bakınca ufuk rengi geliyor (2B gradyan arka planın yapamadığı
  şey bu). Küre derinliğe yazmaz, sahnenin geri kalanından önce çizilir.
- **`scene3d` entegrasyonu:** `sky3d(tepe, ufuk)` / `gokyuzu3d(...)` ve
  `ground_texture3d(t, tile)` / `zemin_doku3d(...)`. Işık ve gölgenin aksine
  gökyüzü **varsayılan kapalı** — o bir sanat yönü kararı, algı düzeltmesi
  değil; karanlık/stilize bir sahneye zorla açık mavi gökyüzü koymak yanlış
  olurdu. Zemine doku verilince ızgara çizgileri çizilmez (doku zaten ölçeği
  gösteriyor).
- **Mesafe sisi:** `fog(renk, yogunluk)` / `sis(...)`, `fog_off()` /
  `sis_kapat()`. Üssel-kare eğri — yakında hiç yok, uzakta hızlı doyuyor; düz
  doğrusal sisin "her şey biraz soluk" görüntüsünü vermez. Sisin doğru rengi
  gökyüzünün **ufuk** rengidir (uzaktaki cisim arkasındaki gökyüzüne
  karışmalı, başka bir renge değil — yoksa "kirli cam" gibi görünür), o yüzden
  `fog_sky(yogunluk)` / `sis_gokyuzu(...)` rengi `sky()`'den kendisi alır. Sis
  ışık shader'ında hesaplandığı için `lights_off()` ile ışık kapatılırsa sis de
  çizilmez (gölgeyle aynı bağımlılık). `scene3d`'de `fog3d(yogunluk)` /
  `sis3d(...)`, varsayılan kapalı.
- Örnek: `examples/tame3d_texture.tpr` (BOŞLUK / dokunuş ile dokuyu aç-kapat;
  aynı küre solda parlak, sağda mat).

**7 yeni builtin:** `tm3_texture` · `tm3_material` · `tm3_sky` · `tm3_sky_off` ·
`tm3_fog` · `tm_checker` (+ Faz 4b'den `tm3_shadows_active`). Masaüstü GL 3.3,
web GLES2 ve Android GLES2'nin üçünde de derleniyor.

### Added — 3D gölgeler (shadow mapping)

Yönlü ışık (güneş) için **gölge haritalama**: nesneler zemine ve birbirlerine
gölge düşürüyor — "havada yüzüyor" hissi gidip "yerde duruyor" hissi geliyor.
Yumuşak kenar için 3×3 PCF, yüzeyin kendini gölgelemesine (shadow acne) karşı
eğime göre bias + gölge geçişinde ön-yüz ayıklama.

- **3 yeni builtin:** `tm3_shadows` (aç/kapat) · `tm3_shadow_area` (kapsanan
  alanın yarı-genişliği) · `tm3_shadows_active` (gölge GERÇEKTEN çalışıyor mu).
  Sarmalayıcılar: `shadows_on`/`golge_ac`, `shadows_off`/`golge_kapat`,
  `shadow_area`/`golge_alani`, `shadows_active`/`golge_aktif`.
- **Gölge haritası bir RENK dokusudur, derinlik dokusu değil:** derinlik
  shader'da RGBA8'e paketleniyor. Sebep aşağıda (GLES2) — bu yöntem masaüstü,
  web ve Android'de aynı kod yolunu kullanır.
- **Dürüst durum bildirimi:** `shadows_on()` çağırmış olmak gölgenin çalıştığı
  anlamına gelmez. FBO kurulamazsa gölgeler kapatılıp açık bir mesaj basılıyor,
  **ışıklandırma çalışmaya devam ediyor**, ve `shadows_active()` oyun koduna
  gerçeği söylüyor — demo bunu ekranda gösteriyor.
- Örnek: `examples/tame3d_shadows.tpr` (BOŞLUK / dokunuş ile aç-kapat; zıplayan
  kürenin gölgesi büyüyüp küçülür).
- Teşhis: `-DTAME_SHADOW_DEBUG` ile derlenirse gölge haritası ekranın sol
  üstünde gösterilir (varsayılan kapalı).
- **`scene3d`'de gölgeler VARSAYILAN AÇIK** (ışık gibi) — motoru kullanan oyun
  sıfır konfigürasyonla gölgeli görünür. Işık kapalıysa gölge de çizilmez.
  `golge3d(false)` / `shadows3d(false)` ile kapanır,
  `golge_alani3d(a)` / `shadow_area3d(a)` ile alanı daralır (varsayılan 24.0;
  küçültmek gölgeyi keskinleştirir).

**Mobil/GLES2'de dört ayrı hata çıktı, hepsi düzeltildi** — masaüstünde
şans eseri gizlenmişlerdi:

1. **Alfa harmanlama paketlenmiş derinliği bozuyordu.** raylib alpha blending'i
   varsayılan açık tutar; derinliği RGBA'ya paketlerken alfa kanalı da veri
   taşıdığı için rastgele bir opaklık gibi yorumlanıp her fragment arka planla
   harmanlanıyordu → benekli gölge haritası, noktalı/kayıp gölge. Derinlik
   geçişinde `rlDisableColorBlend()`.
2. **`mediump` yetmiyordu.** Derinlik karşılaştırması ~10 bit mantiste bozuluyor;
   gölge cismin üstünde tutuyor ama geniş zemin düzleminde hiç oluşmuyordu.
   Fragment shader `GL_FRAGMENT_PRECISION_HIGH` varsa `highp`. (Masaüstü GPU'lar
   `mediump`'ı zaten fp32 işlediği için web'de sorun görünmüyordu.)
3. **GLES2 yalnız-derinlik FBO'yu kabul etmiyordu.** Android emülatörü
   `OES_depth_texture`'ı DESTEKLEDİĞİ HALDE framebuffer "incomplete attachment"
   veriyordu (raylib boyutlu iç format kullanıyor; katı GLES2'de
   internalformat == format olmalı). Çözüm: renk dokusu + derinlik
   renderbuffer'ı.
4. **Paketlenmiş derinlik interpolasyona gelmez.** Bilinear örnekleme iki komşu
   paketin ortalamasını alıp anlamsız derinlik üretiyordu → `NEAREST` + `CLAMP`.

Mimari not: gölge sahneyi **iki kez** çizmeyi gerektirir (ışığın gözünden
derinlik + kameradan normal geçiş), ama tame'de çizimler `space_begin`/
`space_end` arasında anında yapılıyordu. Artık **gölge açıkken** çizimler bir
listeye kaydedilip `space_end` iki geçiş halinde oynatıyor; **gölge kapalıyken
eski anında-çizim yolu birebir korunuyor** (sıfır maliyet).

### Added — 3D ışıklandırma (Faz 4)

3D sahneler artık **ışık alıyor**: düz renkli geometri yerine gölgeli yüzeyler,
specular parlama ve yüze göre parlaklık. Blinn-Phong shader **kaynak içine
gömülü** (`LoadShaderFromMemory`) — `.vs/.fs` asset'i taşınmıyor, dolayısıyla
web/Android paketlerine ek dosya girmiyor ve "oyunu kopyaladım, ışık gitti"
sınıfı hata mümkün değil. Masaüstü (GLSL 330) ve GLES2 (GLSL 100, web+Android)
varyantları ayrı gömülü.

- **4 yeni builtin:** `tm3_lights` (aç/kapat) · `tm3_light_set` (slot 0-3,
  yönlü veya nokta) · `tm3_light_off` · `tm3_ambient`. Tulpar sarmalayıcıları
  çift dilli: `lights_on`/`isik_ac`, `sun`/`gunes`, `point_light`/`nokta_isik`,
  `dir_light`/`yonlu_isik`, `light_off`/`isik_sil`, `ambient_light`/`ortam_isik`.
- **En çok 4 ışık**; nokta ışıklar **mesafeye göre zayıflıyor** (yakındaki
  ışığın sahneyi patlatmasını önler). Işık açıldığında hiç ışık tanımlı değilse
  makul bir güneş otomatik verilir — "ışığı açtım, ekran simsiyah" tuzağı yok.
- **`scene3d` motorunda ışık VARSAYILAN AÇIK** (güneş + ortam ışığı): motoru
  kullanan oyun sıfır konfigürasyonla gölgeli görünür. `lights3d(false)` /
  `isik3d(false)` ile kapatılıp eski düz-renk görünüme dönülebilir.
- Örnek: `examples/tame3d_lights.tpr` (BOŞLUK ile ışık aç/kapat — aynı sahnenin
  iki hali).

Uygulama notu (iki raylib gerçeği bu tasarımı zorunlu kıldı): `BeginShaderMode`
rlgl'in anlık shader'ını değiştirir ama `DrawMesh` `material.shader` kullanır →
**modeller BeginShaderMode'dan etkilenmez**, materyallerine ayrıca atanır. Ve
`DrawSphereEx`/`DrawCylinder` **normal üretmez** → ışık altında yanlış
gölgelenirdi; bu yüzden ışık açıkken küre/silindir/düzlem/kutu, normal'i doğru
olan **cached birim mesh'ler** üzerinden `DrawModelEx` ile çizilir (ışık
kapalıyken eski immediate-mode yolu birebir korunur).

### Fixed

- **Web (wasm) derlemesi `tame_impl.c`'yi platform bayrakları olmadan
  derliyordu** (`android/build_tame_android.sh` veriyordu, `wasm/build_tame_web.sh`
  vermiyordu). Sonuç: ışık shader'ı web'de sessizce MASAÜSTÜ varyantını seçiyor
  ve `'in' : storage qualifier supported in GLSL ES 3.00 and above only` ile
  derlenmiyordu — sahne ışıksız çiziliyordu. Script artık Android ile aynı
  bayrakları geçiyor; ayrıca shader seçimi `__EMSCRIPTEN__`/`__ANDROID__` gibi
  derleyicinin kendi tanımlarına da bakıyor, tek bir build bayrağına güvenmiyor.

### Added — 3D oyun katmanı (tame3d + scene3d)

TulparLang artık **3D oyun** yapabiliyor. Vendored raylib'in zaten derlenen 3D
modülü (`rmodels.c`, kamera/mesh/model) `tm3_*` binding ailesiyle açığa çıkarıldı
ve üzerine saf-Tulpar bir preset motoru (`scene3d`) kondu. Web (WASM/WebGL) +
masaüstü + Android link doğrulandı; sahneler headless Chrome/CDP ile görsel teyit
edildi.

- **~28 yeni `tm3_*` builtin** (5-nokta bağlı, çift dilli TR/EN sarmalayıcı,
  `tm3_` codegen prefiks kapısı):
  - **Faz 0 — temel:** `camera3d` · `space_begin/end` · `cube` · `cube_wires` · `grid`.
  - **Faz 1 — primitifler + raycast:** `sphere` · `sphere_wires` · `cylinder` ·
    `plane` · `line3d` · **`pick_box`/`pick_sphere`** (ekran ışını ile tıklama-seçim)
    · `vec3_len`/`vec3_dist`.
  - **Faz 2 — modeller + animasyon:** `load_model` (OBJ/GLTF/GLB/IQM) · **7 `gen_*`
    prosedürel şekil** (cube/sphere/plane/cylinder/torus/cone/knot) · `draw_model` ·
    `draw_model_rot` · `model_texture` · **iskelet animasyonu** (`anim_count`/
    `anim_frames`/`anim_play`) · `unload_model`. Model registry doku/font registry
    deseniyle; `close()`'ta otomatik boşaltma.
- **`lib/scene3d.tpr` — 3D preset motoru (arcade'in kardeşi, saf Tulpar).**
  Modern `struct Ent3` **dizisi** (paralel-dizi mirası yok — struct-in-array +
  alan-yazma artık çalışıyor). Motor her kare: girdi → hareket → yerçekimi/zemin →
  **AABB çarpışma + duvar itme (MTV)** → takip kamerası → shape'e göre çizim → HUD.
  Kullanıcı yalnız `setup`/`update`/çarpışma kancası yazar (`on_hit3d`,
  `me3d()`/`other3d()` bağlamı). "~40 satırda 3D toplayıcı".
- **Örnekler:** `examples/tame3d_cube.tpr` (Faz 0), `tame3d_primitives.tpr` (Faz 1),
  `tame3d_models.tpr` (Faz 2), `scene3d_collector.tpr` (Faz 3 — yürü/zıpla/topla).
- **Doğrulama:** scene3d motoru **8/8 headless birim test** (yerçekimi, zemin,
  zıplama, toplama+skor, duvar itme); 4 sahne WebGL'de görsel; Android `.so`
  (arm64-v8a + x86_64) `-Wl,--no-undefined` ile linklendi.
- Prebuilt arşivler (`wasm/dist`, `android/dist/<abi>`) yeni `tm3_*` sembolleriyle
  yeniden derlendi; `kMaxFunctions` 1024 (launcher + 3D için).

Third mobile wave — full app shell, per-game juice, three new games, and a
full-stack global leaderboard. All verified live on the Android emulator.

### Added (third wave)
- **Per-game effects + centralized audio across all games.** Every arcade game
  now sprays a `patlama()` particle burst at its scoring/collision/death points
  (10 games). Sound + haptics are centralized in the engine (DRY): `skor_ekle`
  blips, `game_over` plays a lose tone + death rumble, level-up / win / new-record
  each get their own tone — so all games gained audio with one edit, not ten.
- **App shell.** In-app **Settings** screen (Sound / Haptics / Language TR↔EN
  toggles, persisted via `save_data`, live-retranslates the whole shell UI),
  a **Pause** overlay (Resume / Restart / Menu, opened with the Android **Back**
  button / ESC), and a gear button on the menu. `sound_on()/ses_ac()`,
  `haptics_on()/titresim_ac()` gate every engine beep/vibrate.
- **Juice.** Floating `+N` score popups (anchored at the collision point),
  a purely-visual **combo/streak** indicator (`SERI x3` — never changes the
  score, so best-scores stay fair), and a short **menu↔game fade** transition.
- **Three new games** (13 total in the launcher): **2048** (swipe/arrow merge,
  colored tiles), **Pong** (vs a beatable AI, endless rally = score), and
  **Vur** (whack-a-mole reaction, pure tap, speeds up).
- **Global leaderboard (full-stack Tulpar).** `examples/arcade_app/skor_sunucu.tpr`
  is a Tulpar Wings server (`POST /skor`, `POST /tablo` → top-10). The engine's
  `skor_tablosu_url(url)` + `oyuncu_adi(name)` make every game submit its score
  on game-over and fetch + render the global top-5 on the game-over screen — a
  Tulpar game talking to a Tulpar backend, verified end-to-end (emulator →
  server). Off by default (`_lb_url==""` → no network); the launcher enables it
  from the `TULPAR_LB_URL` env var at generation time.

### Added (third wave, follow-ups)
- **Per-game music themes + larger touch targets + 3 more browser games.**
  `music_theme(n)` / `muzik_tema(n)` picks one of three in-game tracks (default,
  tense, bouncy); shooters/space use the tense theme, the platformer the bouncy
  one. Touch **hit-areas** are enlarged without changing the visuals (direction
  buttons and the action button are more forgiving to tap). The three endless
  games (2048, Pong, Whack/Vur) gained **English twins** and joined the bilingual
  browser gallery — 14 games now, each TR + EN. Diagonal top-down movement is
  normalized (was ~41% faster diagonally) and the joystick dead-zone is tighter.
- **Progression: per-level stars + level-select + badges (all games unlocked).**
  Tapping a leveled game now opens a **level-select screen** first — a grid of its
  levels, each showing a gold star if completed — and you pick which level to play
  (or Back). **Each level earns its own star** the moment you clear it, persisted
  per level (`arcade_lvl_<game>_<k>.txt`); the launcher card shows **completed/total**
  (e.g. ★ 4/6). Endless games (2048/Pong/Vur) have no levels, so a tap plays
  immediately and their card keeps a 0–3 star rating from score thresholds
  (`yildiz_hedef(a,b,c)` / `star_goals`). A **trophy button** on the menu opens a
  **Badges** screen with 5 local achievements — First Win, Record Breaker, Three
  Stars (3 total), Collector (15), Master (30) — each persisted, with a toast +
  jingle when newly earned. No locks: any game is always playable, and you can
  replay any earlier level. Level counts + completion are discovered at launcher
  start by probing each game's (side-effect-free) setup. Verified headless
  (`tests/arcade_progression.test.tpr`, 4/4: per-level marking, start-at-level,
  badge unlock+persist, endless thresholds). The launcher's function table also
  outgrew the old 512-symbol AOT cap (13 namespaced games + arcade + tame); raised
  to 1024 in `llvm_backend.{hpp,cpp}`.
- **Procedural background music (fileless chiptune).** A new volume-controlled
  synth builtin `tm_tone(freq, ms, vol)` (the `tm_beep` sibling; `beep` = full
  volume) drives an arcade music sequencer that plays a soft looping melody —
  a calm A-minor arpeggio on the menu/settings/pause screens and an upbeat
  pentatonic loop during active play — timed against `elapsed()` so tempo is
  frame-rate-independent. Notes play *under* the SFX (≈0.26 volume) so effects
  stay audible. No audio files → **zero APK-size cost**. Toggle via the new
  **Music / Muzik** Settings row (persisted in `arcade_mus.txt`), independent of
  the SFX (Sound) toggle; public `music_on()/muzik_ac()`. Settings now has 6 rows
  (Sound / Music / Haptics / Language / FPS / Records), re-spaced to fit.
- **Config-driven build target — `tulpar build` reads `tulpar.toml`.** A new
  `[build]` section (`target = "desktop"|"web"|"android"|"apk"|"aab"`, plus
  optional `entry` and `output`) lets a bare `tulpar build` (no flags, no file)
  pick the target, source, and output name from the manifest. CLI flags still win
  (`--target=…`, positional src/out override the manifest). So updating the mobile
  app is now just `cd examples/arcade_app && tulpar build` — which produces the
  signed `tulpar-arcade.apk` with the correct `dev.tulparlang.arcade` / "Tulpar
  Arcade" identity, instead of remembering the long `--target=android … out_apk`
  invocation (which silently produced a generic app).
- **More levels — every leveled game is now 6 levels (was 3).** +3 levels each
  for topla, zipla, nisan, tugla, uzay, labirent, karsiya, ucus, goktasi, yilan
  (2048/Pong/Vur stay endless by design), with escalating difficulty. Parameter
  games ramp tempo/speed/target; layout games get new hand-built boards. The
  labirent boards (M4–M6) are generated by a recursive-backtracker maze generator
  and **verified solvable by flood-fill** (every key reachable, every patrol on a
  horizontal corridor) — no unbeatable levels. Platformer (zipla) boards respect
  the documented jump-arc limits (≤80px vertical / ≤140px horizontal).
- **Fixed joystick.** The touch joystick base is now anchored at a fixed
  bottom-left position instead of spawning under (and drifting with) the finger —
  on some phones it could creep toward the screen center and the player's hand
  would block the view. Direction is read as the finger's offset from the fixed
  center; the base no longer moves. Its activation area is now a small circle
  around the fixed base (was the whole bottom-left quadrant), so touching the
  screen center — or anywhere off the joystick — no longer spawns a stick.
- **Professional game-over / win screen.** Ending a game now shows a full-screen
  overlay with the big score, best/record line, optional global top-4, and two
  buttons — **Tekrar Oyna / Play Again** and **Ana Menu / Menu** (in standalone
  `play()` the second is **Cikis / Quit**). Tapping the screen at random no longer
  restarts the game (only the buttons act; keyboard R/Enter = restart, Esc/Back =
  menu). A finished game can no longer be paused. The three go-to-menu paths
  (home button, pause→Menu, game-over→Menu) share one `_lx_goto_menu()` helper.
- **FPS counter (Settings-toggled).** A 4th Settings row (**FPS**, off by
  default, persisted via `arcade_fps.txt`) shows the live frame rate top-center
  during play (`tm_fps()` = raylib `GetFPS`; no C++ change needed). Public
  `show_fps()/fps_goster()`.
- **Reset records (Settings).** A 5th Settings row (**Rekorlar / Records**) wipes
  every game's persisted best score, with a two-tap confirm (`Sil`→`Emin?`→wiped)
  so it can't fire by accident; menu best-badges clear immediately.

### Fixed (third wave)
- **Launcher HOME↔gear tap cascade.** The in-game HOME button and the menu's
  gear (Settings) button share the top-right corner; tapping HOME used to
  cascade into opening Settings the same frame. Going home now consumes the
  touch (`_ar_touch_prevn`). Caught by on-emulator testing.

### Changed (third wave)
- **Pong now uses the vertical (▲▼) touch scheme** instead of the 4-button dpad —
  a paddle only moves up/down, so the horizontal buttons were dead. `kontrol_semasi("dikey")`.
- **2048 is swipe-only on mobile** (`kontrol_semasi("yok")`): no on-screen
  buttons, the swipe gesture drives it (arrow keys still work on desktop).

---

Second mobile wave — "juice", real device sensors, and store-ready packaging.
Every item below was verified live on the Android emulator (screencap /
logcat / dumpsys / bundletool validate).

### Added
- **Particle + screen-shake + flash effects in `arcade` (pure Tulpar).**
  `patlama(x,y,renk)` / `explode` (+ `patlama_n` count variant) spray
  gravity-affected sparks from a slot-recycling parallel-array pool; `sars()`
  / `shake()` (damped random offset — the whole scene shudders) and `parla()`
  / `flash()` (white full-screen). `game_over()` now auto-triggers shake+flash,
  so all ten games gained impact feedback with zero code changes. Effects keep
  animating on the game-over screen. `sars_x()/sars_y()` expose the offset for
  games that draw their own scene (snake).
- **Persistent per-game high scores in `arcade`.** The engine saves each game's
  best score (`save_data`, keyed by scene/launcher title) and shows a gold
  "NEW RECORD!" / "En iyi: N" badge on the game-over screen **and** on the
  launcher menu cards (loaded once at launch, no per-frame IO). Survives cold
  boot + reinstall (verified).
- **F1 — accelerometer / tilt controls.** `tm_accel_x/y/z`,
  `tm_accel_available` (NDK `ASensorManager`, **no JNI**, callback-driven event
  queue drained through raylib's existing looper poll) + `accel_x()/egim_x()`
  … wrappers; arcade `kontrol_semasi("egim")` (scheme 6) reads tilt as a
  **deviation from a captured neutral baseline** (auto-calibrates to however
  the phone is held, so gravity doesn't false-trigger). Desktop/web return 0 →
  keyboard still drives.
- **F2 — `tm_beep(freq, ms)` / `beep()` / `bip()`.** Fileless SFX: synthesises a
  sine wave (with attack/decay envelope) on the fly and plays it through a
  round-robin `Sound` pool — short game sounds with no asset shipping. Same on
  desktop/web/Android (pure raylib audio); AAudio stream open verified on device.
- **G1 — screen stays awake.** `AWINDOW_FLAG_KEEP_SCREEN_ON` set alongside
  fullscreen in the vendored `rcore_android.c` (TULPAR PATCH); the device no
  longer dims/sleeps mid-game (`dumpsys` shows `fl=KEEP_SCREEN_ON`).
- **G2 — background lifecycle.** On `APP_CMD_LOST_FOCUS`/`GAINED_FOCUS` the
  Android backend now `SetMasterVolume(0/1)` so audio mutes when the app is
  backgrounded and resumes on return (the loop already freezes = auto-pause).
  `tm_active()` / `aktif_mi()` exposes foreground state to game code.
- **G3 — splash background + adaptive/round icon.** The driver writes a
  `TulparSplash` theme (window background = splash colour, killing the black
  cold-start flash) plus an `res/mipmap-anydpi-v26/ic_launcher.xml`
  adaptive-icon (foreground = your PNG, background = splash colour) and
  `android:roundIcon`. Colour via `tulpar.toml [android] splash_color="#RRGGBB"`.
- **G4 — `tulpar build --aab`.** Emits a Play-Store-ready **Android App
  Bundle** via `android/package_aab.sh` (aapt2 `--proto-format` link →
  bundletool `build-bundle` → jarsigner). bundletool is invoked from the
  gradle-cached jar with an assembled classpath when no fat jar is present;
  `bundletool validate` passes (base module carries both ABIs + adaptive icon).
- **H1 — synchronous HTTP on Android.** `INTERNET` permission is now always
  written to the manifest; the existing blocking `http_request`/`http_get`
  works on-device over bionic sockets (verified: live `GET` returns status 200).
- **`tm_view_left/right/top/bottom` + `view_left()`/`ekran_sol()` … wrappers.**
  Report the visible screen's edges in world coordinates.

### Changed
- **Touch controls now anchor to the real screen edges (Android camera model).**
  Previously raylib letterboxed the scene and the ortho projection was the game
  world's size, so the on-screen joystick/buttons were cramped into the world's
  small inner area instead of reaching the phone's real corners. Now on Android
  `window(w,h)` opens **fullscreen** and centres the `w×h` world via a
  `Camera2D` (zoom+offset); `frame_begin/end` wrap `BeginMode2D/EndMode2D` and
  touch/mouse read back through the inverse transform — **no game code
  changes**. arcade anchors the joystick, ◀▶/dpad/vertical buttons, action
  button, home button and full-screen flash to `tm_view_*` (the real
  bottom-left / bottom-right / top-right corners). Desktop/web keep the camera
  off → byte-identical behaviour.
- `examples/arcade_uzay.tpr` (+ EN `invaders`) burst an orange explosion when an
  invader is shot; `examples/arcade_yilan.tpr` sparks gold when food is eaten.

## [v3.12.0]

Tulpar goes properly mobile: the arcade launcher becomes the official
ten-game **Tulpar Arcade** app, Android packaging collapses to a single
`tulpar build --apk` command with full app identity from `tulpar.toml`,
games gain assets/sound, persistent saves, haptics, swipe gestures,
per-game touch control schemes and hardware-back handling — every item
verified live on the Android emulator.

### Changed
- **Modern mobile touch controls in `arcade` — dynamic analog joystick +
  context-aware action button.** The fixed 4-arrow D-pad is replaced by a
  **floating analog joystick** (circle-within-a-circle): pressing anywhere in
  the movement zone spawns the base under the finger, the knob follows the
  finger, and the base *trails* the finger past the ring so a long drag never
  loses control — direction persists as long as the finger stays down (modern
  virtual-stick feel). The analog vector is thresholded into the same 8-way
  booleans the movement presets already consume, so every game and the desktop
  keyboard path are unchanged. The **action/fire button now only appears in
  games that actually read it** (`action_pressed`/`action_held`/`fire_pressed`
  or the platformer jump preset lazily flag intent) — collector/dodge/maze
  games (Topla, Tugla, Labirent, Karsiya, Goktasi) no longer show a dead
  button. **The joystick is context-aware too**: it only appears in games that
  read directional input, so a tap-only game (Ucus/flight) shows just the
  action button. And the joystick is **corralled to a bottom-left region** —
  the base spawns under the finger clamped into that region and stays fixed
  until release, so it can never wander across the screen. New public
  direction readers `left()/sol()`, `right()/sag()`, `up()/yukari()`,
  `down()/asagi()` (keyboard OR joystick) let games that drive movement
  manually join in: Tugla/Uzay/Goktasi (+ EN twins breakout/invaders/dodge)
  switched from raw `key_down("LEFT")` to them — making those games
  touch-playable for the first time — and Ucus/flight + shooter/invaders
  fire now goes through `fire_pressed()`. The launcher resets both flags per
  game so one game's controls don't linger on the next. Verified on the
  Android emulator: Nisan shows joystick+fire, Topla joystick-only, Ucus
  button-only (tap flaps), and Tugla's paddle moves by touch with the
  joystick pinned to the bottom-left corral.
- **Per-game control schemes (`kontrol_semasi` / `control_scheme`).** A
  joystick doesn't fit every game, so the touch layout is now a preset picked
  per game: `"joystick"`, `"dpad"` (4 arrow buttons in a cross), `"yatay"`/
  `"horizontal"` (◀ ▶ only), `"dikey"`/`"vertical"` (▲ ▼ only), `"yok"`/
  `"none"`, or the default `"auto"` — which infers the layout from the
  directions the game actually reads: left/right only → ◀ ▶ buttons, up/down
  only → ▲ ▼, both axes → joystick, none → no movement control. Buttons are
  round, semi-transparent, bottom-left, with pressed-state highlight, and
  hold-to-move semantics. Labirent + Karsiya (and EN maze/crossing) opt into
  `"dpad"`; everything else relies on auto (Tugla/Uzay/Goktasi/Zipla get
  ◀ ▶, Topla/Nisan the joystick, Ucus nothing but the flap button). The
  launcher resets the scheme per game; `tools/make_arcade_launcher.py` folds
  `kontrol_semasi`/`control_scheme` into generated setups. Verified on the
  Android emulator: Tugla shows ◀ ▶ (auto) and the held ▶ drives the paddle,
  Karsiya shows the 4-button dpad with ▲ climbing lanes, Topla still gets the
  joystick. A companion `dokunuldu()`/`tapped()` reader (any-finger tap edge)
  lets tap-only games use the whole screen as the control: Ucus/flight flaps
  on a tap anywhere — no button, no joystick, nothing drawn. Nisan/shooter and
  Goktasi/dodge pin `"yatay"`/`"horizontal"` explicitly (◀ ▶ buttons; Nisan
  keeps the fire button next to them).

### Added
- **One-command Android packaging + app identity (`tulpar build --apk`,
  `tulpar.toml [android]`).** `tulpar build --apk game.tpr out` now goes all
  the way to an installable **signed `out.apk`** in a single command (it
  implies `--target=android`, then drives `android/package_apk.sh` — aapt2 →
  zipalign → apksigner — automatically; `TULPAR_ANDROID_TOOLS` points at the
  script dir for non-repo layouts). App identity comes from a new
  `[android]` section in `tulpar.toml`: `package` (applicationId — finally
  lets two Tulpar games install side by side; default stays
  `dev.tulparlang.game`), `name` (launcher label), `icon` (a PNG the driver
  copies into `res/mipmap/ic_launcher.png` and package_apk.sh compiles via
  `aapt2 compile`), `orientation` (`landscape`/`portrait`/`sensor`), and
  `version_code`/`version_name`. Release distribution: pointing
  `TULPAR_ANDROID_KEYSTORE` (+ `_KS_PASS`, `_KEY_ALIAS`, optional
  `_KEY_PASS`) at your own keystore signs with it instead of the debug key —
  a Play-Store-uploadable APK. Verified on the emulator: a
  `dev.tulparlang.arcade` / "Tulpar Arcade" build with a custom icon
  installed **alongside** the default-identity APK, shows its own icon+label
  in the app drawer, and `apksigner verify` prints the release certificate.
- **Game assets inside the APK — Android games get sound and sprites.**
  `[android] assets = "<dir>"` in `tulpar.toml` (or `TULPAR_ANDROID_ASSETS`,
  mirroring the web target's `TULPAR_WEB_ASSETS`) stages a directory into the
  APK's `assets/` **preserving the path as written** — the same relative
  paths the game uses on desktop (`load_texture("assets/top.png")`) resolve
  on-device through raylib's AAssetManager integration, so no code changes
  per platform. Files are added to the zip by the packer itself rather than
  `aapt2 -A` because Windows aapt2 writes backslashed entry names that
  AAssetManager can never match. Verified on the emulator: a PNG loaded from
  the APK renders at its true 64×64 size (rotating, scaled, alpha intact)
  and a WAV `load_sound`/`play_sound` opens an AAudio output stream with
  zero raylib file warnings.
- **Mobile quality-of-life batch: persistent saves, haptics, hardware back,
  swipe gestures, portrait games.** Five additions that make Tulpar games
  feel native on a phone, all verified on the emulator:
  - `save_data`/`kayit_yaz(name, text)` + `load_data`/`kayit_oku(name)`
    (tame): persistent key-file storage through raylib's file layer — on
    Android it lands in the app's internal storage, on desktop in the CWD,
    so a high score written with the same two lines survives force-stop and
    relaunch on-device (proved: counter 3 → kill → relaunch → still 3).
  - `vibrate`/`titret(ms)` (tame): real device vibration via JNI from the
    NativeActivity (`getSystemService("vibrator")`); the driver always adds
    the `VIBRATE` permission to the manifest; desktop/web are silent no-ops.
    The vibrator service recorded our exact 150 ms step. Gotcha fixed on the
    way: `android/build_tame_android.sh` compiled `tame_impl.c` without
    `-DPLATFORM_ANDROID`, silently no-op'ing every Android-only branch — it
    now uses the same flags as raylib.
  - **Hardware BACK button** (`"BACK"`/`"GERI"` key name in tame; raylib
    keeps it away from the OS): the arcade launcher now treats BACK like
    ESC in-game (returns to the menu) and exits the app from the menu
    (focus verifiably returns to the previous task).
  - `kaydirildi`/`swiped("sol|sag|yukari|asagi|left|right|up|down")`
    (arcade): touch swipe gesture, classified on finger release with a
    dominant-axis threshold — snake-style games play with no on-screen
    controls at all (a right-swipe moved the test box exactly +80 world px).
  - **Portrait games**: `[android] orientation = "portrait"` + a portrait
    `window(480, 800)` render a proper vertical game (verified full-screen
    portrait on-device). `dokunuldu()`-style tap games pair naturally.
- **Yilan (Snake) rewritten on the arcade engine + official "Tulpar Arcade"
  app + APK download on the website.** `examples/arcade_yilan.tpr` is a full
  arcade-engine snake (grid state in plain arrays, zero entities, drawing via
  `ciz_ustune`, 3 levels with speed-up + obstacle walls, haptic feedback on
  eat/crash) steered by **swipe gestures, dpad buttons or arrow keys** — the
  launcher now bundles **ten** games (the menu grid auto-flows to 4 columns).
  `examples/arcade_app/` holds the official app identity (tulpar.toml:
  `dev.tulparlang.arcade` / "Tulpar Arcade" + icon) so
  `tulpar build --apk ../arcade_launcher.tpr tulpar-arcade` reproduces the
  shipping APK. The games page generator (`web_demo/gen_index.py`) gained a
  bilingual "Android — play on your phone" card linking `tulpar-arcade.apk`,
  and the site repo carries the APK + regenerated index (Astro build passes;
  publish pending). Emulator-verified: 10-card menu, snake turns via swipe
  and via the held ▲ dpad button, wall crash → engine game-over, tap
  restarts.
- **Arcade launcher — multiple games in one window / one APK (`launcher()`).**
  `import "arcade"` gains a game-registry + menu layer so a single native
  binary (desktop, web, or Android APK) can hold several games behind a
  touch/keyboard menu. `oyun_ekle(name, setup)` / `add_game(name, setup)`
  registers a game as a 0-arg **setup** function — it does the usual
  `baslangicta`/`her_kare`/`carpisinca`/`bilgi` registrations but *not*
  `sahne`/`oyna`, because the window and loop belong to the launcher.
  `launcher()` (alias `oyun_menusu()`) draws a responsive card grid and runs a
  menu↔game state machine: picking a card (tap, or arrows + ENTER) **fully
  resets the engine's registration state** — sahne callbacks, level registry,
  collision rules, hint/HUD, physics — before calling the chosen game's setup,
  so games never bleed into one another. An on-screen home button (top-right)
  or ESC returns to the menu; `menu_basligi(s)` / `launcher_title(s)` sets the
  title. Example `examples/arcade_launcher.tpr` bundles **all nine shipped
  arcade games** (Topla / Zipla / Nisan / Tugla / Uzay / Labirent / Karsiya /
  Ucus / Goktasi) in one APK — generated by `tools/make_arcade_launcher.py`,
  which namespaces each `examples/arcade_*.tpr` game's top-level functions +
  globals with a per-game prefix and folds its registration block into a
  `<prefix>_setup()` (the source games still run standalone). Verified on the
  Android emulator: the 3×3 menu renders, games launch with clean per-game state
  (e.g. Uzay's custom-tag fleet, Goktasi's countdown), and the home button /
  ESC return to the menu.

## [v3.11.0]

The Tame 2D game library and its two new compile targets — WebAssembly
(`tulpar build --target=web`) and **native Android**
(`tulpar build --target=android`) — land together, turning pure-TulparLang
games into browser and mobile apps. The `import "arcade"` preset engine gains a
level system, entity slot recycling, generation-tagged handles, and on-screen
touch controls, so all 10 bundled games are touch-playable on-device. Alongside
the game work, a batch of language/runtime correctness fixes — struct↔array
round-trips, `call()` N-argument dispatch, method-style calls on any receiver,
closures on the native fast path, and `try`/`catch` handler-stack hygiene. All
backwards-compatible.

### Added
- **Native Android target — `tulpar build --target=android game.tpr out`.**
  Tame/arcade games now compile to real Android apps (NativeActivity +
  raylib GLES2), verified rendering + animating + reading touch on the
  Android Studio emulator. One compiled module is emitted for both ABIs
  (arm64-v8a for devices, x86_64 for the emulator) and linked with the NDK
  into `lib/<abi>/libtulpargame.so`, then an APK is staged with a
  NativeActivity manifest. Mirrors the web target's architecture:
  `android/build_tame_android.sh` cross-compiles the runtime + raylib
  `PLATFORM_ANDROID` + `native_app_glue` + tame bindings into per-ABI
  static archives; `android/package_apk.sh` runs aapt2 + zipalign (16KB
  pages) + apksigner (driving the Windows SDK tools over WSL interop when
  there's no Linux SDK); `android/install_run.sh` does adb install + launch
  + screencap. The whole arcade preset engine (`import "arcade"` — entity
  store, MTV collision, level system, `call()`-based hooks) runs on-device
  unchanged. `async` is unsupported on Android (bionic has no
  makecontext/swapcontext, same as web).
- **Tame touch input — `touch_count()` / `touch_x(i)` / `touch_y(i)` /
  `touched()`** (bilingual: `dokunma_sayisi`/`dokunma_x`/`dokunma_y`/
  `dokunuldu`). Multi-touch finger positions for mobile games; on desktop a
  single touch still maps to the mouse so existing `mouse_*` code keeps
  working. 52 `aot_tm_*` bindings now.
- **Arcade touch controls — on-screen D-pad + action button.** The `import
  "arcade"` preset engine now draws and reads a translucent control overlay
  (bottom-left 4-way D-pad, bottom-right action/jump/fire button), auto-
  enabled on the first touch, so all 10 shipped arcade games are playable
  on touch-only devices — verified on-device. Movement presets read combined
  keyboard-OR-touch input (desktop keyboard play unchanged); game-over
  restart accepts a screen tap; `action_pressed()`/`action_held()` and
  `fire_pressed()`/`ates_basildi()` (+ TR aliases) let games read the action
  button (the two shooter examples adopt it). `touch_controls(true/false)`
  forces the overlay.
- **Tame — 2D game library (`import "tame"`).** Open a window, draw shapes
  and text, and read keyboard/mouse input from pure TulparLang — Pong-class
  games in ~40 lines (`examples/tame_hello.tpr`):
  - **Native layer:** vendored raylib 5.5 (`lib/raylib/`, zlib license, same
    model as SQLite) + 26 `aot_tm_*` builtins in a **separate**
    `libtulpar_tame.a` (`runtime/tame_impl.c` + `tame_bindings.cpp`, two-TU
    split so raylib.h never meets the runtime/windows headers). The AOT
    pipeline links it **only when the program imports "tame"** (or calls a
    `tm_*` builtin), so ordinary binaries gain no GL/window dependency —
    and even a tame binary dlopens X11/GL at runtime rather than linking
    them (GLFW module loader + glad), so it stays portable.
  - **Tulpar layer:** embedded `lib/tame.tpr` wraps the `tm_*` builtins with
    game-friendly names — `window/running/frame_begin/frame_end/clear/rect/
    circle/line/pixel/text/key_down/key_pressed/mouse_x/...` — plus
    `rgb()/rgba()`, the full named raylib palette (`GOLD`, `SKYBLUE`, ...,
    packed `0xRRGGBBAA` ints), and helpers (`rect_overlap`, `point_in_rect`,
    `clamp`). Keys are addressed by name (`"W"`, `"SPACE"`, `"LEFT"`,
    `"F1"`...), no key-constant table to learn.
  - Codegen binds the family table-driven (`k_tame_builtins` in
    `llvm_backend.cpp` — one row per builtin instead of 26 hand-rolled
    dispatch blocks); type inference accepts int **or** float for
    coordinates; all 26 symbols documented in the LSP hover/completion table.
  - Headless/no-DISPLAY runs fail gracefully (bilingual error, clean exit)
    instead of crashing — includes an upstream-matching patch to vendored
    raylib's `InitWindow` (5.5 ignored `InitPlatform()` failure and
    segfaulted in `rlglInit`).
  - **Sprites, fonts, audio (Phases 3-4):** `load_texture/draw_texture/
    draw_texture_ex(scale, rotation)/texture_width/height/unload_texture`,
    `load_font` (TTF) + `text_font`, `measure_text` (centering);
    `load_sound/play_sound/stop_sound/sound_volume` and
    `load_music/play_music/stop_music/music_volume`. Resources are int
    handles in slot registries (the DB-handle pattern); the audio device
    opens automatically on first load, playing music streams are pumped
    automatically inside `frame_end()`, and `close_window()` tears
    everything down in the right order (GL resources before the context,
    sounds before the device).
  - **Managed game loop (Phase 5):** `run(update, draw)` — the Wings
    `listen()` model for games. Takes two function refs, owns the loop,
    frame pacing, **and frame memory**: one `arena_save` before the loop,
    `arena_restore` every frame, `arena_drop` + window close on exit, so
    per-frame strings/objects never accumulate. Same rule as Wings:
    persistent game state lives in globals. Plus `triangle()` (vertex
    winding auto-corrected — raylib's silent CCW-only trap is closed) and
    `screenshot(path)` (PNG, written to the working directory).
  - **Named gamepad input:** `gamepad_available(id)`, `gamepad_name(id)`,
    `gamepad_down(id, btn)`, `gamepad_pressed(id, btn)`,
    `gamepad_axis(id, axis)` — the keyboard's name-based pattern extended
    to controllers: buttons `"A"/"B"/"X"/"Y"` (PS synonyms
    `"CROSS"/"CIRCLE"/...`), dpad `"UP"/"DOWN"/...`, shoulders/triggers
    `"LB"/"RB"/"LT"/"RT"` (`"L1"/"L2"...`), `"START"/"SELECT"/"GUIDE"`,
    stick clicks `"L3"/"R3"`; axes `"LX"/"LY"/"RX"/"RY"` (-1..1) plus
    `"LT"/"RT"` triggers. Without a controller everything degrades
    gracefully (false / `""` / 0.0).
  - Verified live under WSLg: every draw primitive, sprite scaling and
    rotation, and centered text confirmed pixel-level via `tm_screenshot`
    output; 60 FPS pacing (`frame_time` = 0.0167 s); reversed-winding
    triangle rendered; audio device opened; `run()` ran 480 frames stable.
  - `tame_hello.tpr`, `tame_sprite_demo.tpr`, and `tame_run_demo.tpr` are
    compile-only in the test suites (a window would block on machines with
    a display); compiling them end-to-end exercises the whole
    import → codegen → link chain. Test assets live in
    `examples/tame_assets/`.
- **Web target: `tulpar build --target=web` (or `--web`).** Compiles the
  program to a `wasm32-unknown-emscripten` object with the same LLVM
  backend (WebAssembly components now linked on every arch) and links it
  with `em++` against the `wasm/dist` archives produced by
  `wasm/build_tame_web.sh` (async-free runtime + raylib `PLATFORM_WEB` +
  the tame bindings) → `game.html + .js + .wasm`, runnable in a browser.
  `-sASYNCIFY` turns Tulpar's blocking `while (running())` game loop into
  browser-friendly cooperative yielding (raylib's web backend calls
  `emscripten_sleep` in `EndDrawing`). Game assets are embedded with
  `TULPAR_WEB_ASSETS=<dir>` (`--preload-file`). Two ABI notes for
  maintainers: on wasm32 the VMValue C ABI is sret+byval like Win64 — the
  codegen picks it at runtime via `vmvalue_abi_uses_sret()`
  (`llvm_values.cpp`) — and `tulpar_async` is excluded from the web
  runtime (stackful ucontext coroutines don't exist under Emscripten).
- **Arcade: entity slot recycling + generation-tagged handles.** A killed
  entity's slot now returns to a free-list and is reused, so a game that spawns
  bullets forever no longer grows the parallel arrays without bound (a shooter
  creating ~133 entities over 600 frames now plateaus at 22 slots). Reusing a
  raw index would silently alias: code holding a dead entity's id would start
  driving whoever took the slot. So ids handed out are now **handles** —
  `_egen[slot] * 2^20 + slot` — and killing bumps the slot's generation, making
  every existing handle to it stale at once. The whole public id API resolves
  through `_slot_of()`; a stale handle is ignored (setters no-op, getters return
  a neutral value, `alive()/yasiyor()` honestly returns false) instead of
  corrupting the new occupant. `entity_count()/entity_sayisi()` counts *allocated
  slots*, not live entities — new `live_count()/canli_sayisi()` gives the live
  count. Verified on native and web.
- **Arcade: levels (`bolum()/level()`).** Multi-level games are now an engine
  feature instead of something each game re-implements — register a 0-arg setup
  function per level and call `next_level()/bolum_gec()` when its win condition
  is met. New (bilingual, as everything in arcade): `level(n, fn)/bolum`,
  `next_level()/bolum_gec`, `level_no()/bolum_no`, `level_count()/bolum_sayisi`,
  `is_won()/kazandin_mi`, plus `tag_count(tag)/tag_sayisi` (live entities with a
  tag — for "all items collected?" conditions, since `live_count()` also counts
  the player and walls) and `get_vx()/get_vy()` (`vx_of`/`vy_of`).
  - **Semantics:** loading a level runs `clear_entities()` → the global
    `on_start`/`baslangicta` function → that level's function. **Score is kept
    across levels** (it's the player's running total); `R` restarts from level 1
    with score 0. `_ar_state` gained `2 = won`, drawn as a KAZANDIN screen with
    the total score; the HUD gains a `Bolum n/N` indicator (top-right) and a
    ~1.2 s "Bölüm N" banner on each transition. `is_over()/bitti_mi()` now means
    `state != 0` (winning is also an ending).
  - **Backwards compatible:** with no level registered, state 2 never occurs and
    behaviour is exactly as before (`on_start` + OYUN BITTI + R). Level
    registrations survive `clear_entities()/temizle()` — like collision rules,
    they are part of the game's definition, not its entities.
  - **`next_level()` defers the switch** (a `_lvl_pending` flag applied at end of
    frame in `_ar_update`/`step`) rather than swapping levels in place: it is
    typically called from a collision callback, i.e. while `_ar_collisions()` is
    iterating the parallel arrays — calling `clear_entities()` right there would
    mutate the array being walked.
  - Regression suite: `tests/arcade_levels.test.tpr` (headless via `step()/adim()`).
- **Levels in the four bundled games (3 each).** `arcade_zipla` (easy → zigzag
  climb → narrow platforms + a moving lethal obstacle), `arcade_topla` (3 items/1
  enemy → 5/2 → 6/3 fast), `arcade_nisan` (cumulative target score 30 → 60 → 100
  as spawn rate and speed rise), and `tame_snake` (built on raw tame, not arcade,
  so its level flow is hand-rolled: 5 food per level, `MOVE_FRAMES` 8→6→4, plus
  obstacle walls and a win screen in level 3). All four are also built for the
  web in `web_demo/` with a refreshed index page.
- **Four more arcade games, 3 levels each** — each one exercises a different part
  of the engine, so together they double as a feature sweep:
  - `arcade_tugla.tpr` (Breakout): bricks are `item()`s, the ball is a
    `TAG_BULLET` + `MV_VELOCITY` entity, the paddle is `MV_NONE` driven
    horizontally from `on_frame` (`MV_TOPDOWN` would move in 4 directions).
    Levels: flat wall → pyramid → gapped wall + faster ball.
  - `arcade_uzay.tpr` (Space Invaders): a fleet that moves as one body (reverses
    and drops a step at the edge), so the invaders are `MV_NONE` and driven from
    `on_frame`. Levels: 3×5 slow → 4×6 fast → 4×7 + the fleet shoots back (a
    custom tag `6` for enemy bullets, alongside the built-in `TAG_*`).
  - `arcade_labirent.tpr` (maze): layouts are written as ASCII rows and read with
    `ord()` (`#` wall, `P` player, `A` key, `E` patrol), so adding a level means
    writing 15 strings. The player is `MV_TOPDOWN` and rides the engine's MTV wall
    resolution; patrols are `MV_VELOCITY` (which ignores walls), so their corridor
    bounds are computed at setup by scanning the map. Levels: open → symmetric /
    2 patrols → dense / 3 fast patrols.
  - `arcade_karsiya.tpr` (Frogger): lanes of `MV_VELOCITY` traffic wrapped around
    at ±660 — the wrap has to happen *before* the engine's own "64px outside the
    world" auto-kill. Levels: 3 slow lanes → 4 → 5 fast.
- **Arcade: engine-drawn HUD is bilingual (`language()/dil()`).** The strings the
  engine renders itself — the score prefix, the `Level n/N` indicator, the
  `Level N` banner, GAME OVER / YOU WIN and the restart hint — are now drawn in
  Turkish or English by a language code. It defaults to the system UI language
  (`sys_lang()`), and `language("en")` / `dil("tr")` overrides it. Turkish stays
  the default and byte-for-byte the old output, so existing games are unchanged.
  The `controls()/bilgi()` strip is game-supplied, so its language is the calling
  game's choice.
- **English twins of all eight games (`examples/en/`).** Each Turkish game has a
  full English counterpart — English API aliases (`player()`, `level()`,
  `next_level()`, …; every arcade function already has a TR+EN name), English
  comments, English `scene()` title and `controls()` text, and a `language("en")`
  call. `jump`, `collect`, `shooter`, `snake`, `breakout`, `invaders`, `maze`,
  `crossing`. They live in `examples/en/` so the `examples/*.tpr` test runner
  doesn't double-count them; verified by direct compile + browser screenshot.
- **`web_demo` is bilingual.** `web_demo/index.html` (generated by a small script
  from the game sources, so it stays in sync) has a page-level TR/EN switch, a
  Play (TR) / Play (EN) button per game — both language builds are published
  (`zipla.html` + `jump.html`, …) — and a "See code / Kodu gör" panel with TR/EN
  tabs showing the actual embedded source, so a visitor sees each game written in
  both languages.

### Fixed
- **The regex builtin family is now registered in the type checker and LSP.**
  `regex_match` / `regex_search` / `regex_capture` / `regex_replace` worked at
  runtime but were invisible to tooling (no completion/hover, unknown to
  inference) and had zero test coverage. Registered with real signatures and
  documented semantics: `regex_match` is full-string match, `regex_search` is
  substring, an uncompilable pattern is a safe no-op.
- **StringBuilder was dead on arrival — the first `sb_append` segfaulted.**
  Its VMValue argument was declared as the raw by-value aggregate — the SysV
  lowering trap documented at `llvm_make_vmvalue_func_type` — so the callee
  read a corrupted value and dereferenced garbage. The argument now travels
  through the `aot_*_ptr` pointer ABI; all `sb_*` entry points gained
  null-handle guards (`sb_append(0, …)` is a no-op, not a crash); and the
  undocumented creator `StringBuilder(capacity)` is now in the LSP table.
- **`write_file`/`append_file` now actually return the documented bool.** Both
  are typed `-> bool` (type checker + LSP) but returned VOID unconditionally —
  a failed write (bad directory, no permission) was indistinguishable from
  success. True only when the file opened and every byte landed.
- **Wings: request-header lookup is now case-insensitive (RFC 7230).** Headers
  are stored with the client's exact casing and every internal read was a
  hand-rolled 2-case probe (`"Cookie"` then `"cookie"`) — any other casing
  (`COOKIE`, `AUTHORIZATION`, `CONTENT-TYPE`, …) silently missed: cookies
  dropped, `jwt_guard` 401'd valid bearer tokens, gzip never engaged, ETag 304s
  never matched. New `req_header(req, name)` public accessor (+ internal
  `_hdr_ci`) resolves case-insensitively; all five internal sites (cookies,
  jwt, `form_data`, If-None-Match, Accept-Encoding) use it. Stored casing is
  untouched, so existing exact-case reads keep working.
- **Closures created in a native-fast-path function computed garbage.** The
  all-int-signature fast path has no closure support, but its gate accepted
  lambda-initialized decls (`var f = () => i;` fits the i64 decl shape), so
  `func f(): int` bodies with a lambda silently produced wrong numbers. A new
  `expr_has_lambda` check bails return/assignment/decl statements with a
  value-position lambda to the boxed codegen.
- **Top-level lambdas couldn't capture top-level scope-locals.** Capture slots
  were only computed for function/lambda nodes, never the program root, so a
  lambda capturing a top-level for-init var (`for (int k …) { var g = () => k; }`)
  resolved nothing and returned nullptr (globals were always fine). The program
  root now gets the same capture analysis and `main` allocates an env array
  like any capturing function. Loop-created closures share the frame's one
  slot per variable (JS-`var`/Python semantics: they see the final value).
- **`try`/`catch`: leaving a `try` via `return`/`break`/`continue` leaked the
  handler frame.** The setjmp frame was only popped on normal fall-through, so
  a later `throw` could longjmp into a stack frame that no longer existed
  ("longjmp causes uninitialized stack frame" abort). Codegen now tracks open
  try scopes: `return` pops them all (after evaluating its expression),
  `break`/`continue` pop down to the loop's entry depth, and lambdas
  save/reset both try and loop tracking so a nested body can't pop the outer
  function's frames. A bare `throw` followed by more statements also corrupted
  its basic block ("Terminator found in the middle of a basic block") — throw
  now spawns a dead continuation block like `break` does.
- **`parse_iso8601` accepted impossible calendar days.** The day was only
  range-checked 1..31, so `"2026-02-30…"` silently normalised to March 2
  instead of failing — a data-corruption trap for a parser that returns -1 on
  every other malformed input. Days are now validated against the month's real
  length with the full Gregorian leap rule (2024-02-29 and 2000-02-29 parse;
  2025-02-29, 1900-02-29, Apr 31, Feb 30 → -1).
- **`contains()` / `indexOf()` silently failed on arrays.** Both were
  string-only: `contains([1,2,3], 2)` returned false and `indexOf` returned -1
  (with a spurious `expected str` typecheck warning), so the only way to test
  array membership was a manual loop. They now also search arrays by value
  (int/float/bool/string) — `contains([1,2,3], 2)` → true,
  `indexOf(["a","b"], "b")` → 1 — while the string-substring behaviour is
  unchanged. Signatures are now polymorphic in the type checker.
- **Float-to-string lost precision and printed scientific notation
  (`toString(1000000.5)` → `"1e+06"`).** Float formatting used `"%g"` (6
  significant figures), which dropped the fraction of larger values and
  switched to scientific notation. Tulpar floats carry 32-bit precision, so a
  naive bump to higher fixed precision instead exposed float32 rounding noise
  (`3.14` → `"3.14000010490417"`). Float display now prints the **shortest
  decimal that round-trips to the same float32** (new `aot_format_float`, used
  by toString / print / string-concat / JSON alike): `3.14` → `"3.14"`,
  `1000000.5` → `"1000000.5"`, `0.1 + 0.2` → `"0.3"` — matching how Python/Go/Rust
  render floats.
- **String ordering comparisons (`<`, `>`, `<=`, `>=`) were always false.** In
  `vm_binary_op` the string/string type pair fell through to a
  `default: VM_BOOL(0)` for the four ordering operators, so any `"a" < "b"`
  returned false regardless of the operands — and sorting an array of strings
  silently did nothing. (`==`/`!=` already used `strcmp`.) The four operators
  now compare lexicographically via `strcmp`, so `"apple" < "banana"` is true,
  `"abc" <= "abc"` is true, and bubble/insertion sorts over string arrays work.
- **Returning a non-trivially-unboxable struct from a function returned zeros —
  and this also unblocked nested structs.** A struct with float / string /
  nested-struct fields is a boxed `VM_OBJECT`, not a native aggregate, but both
  the call site and the `return` codegen assumed the native res-pointer ABI, so
  `P e = mk(1.5, 2.5)`, `push(a, mk())`, and `arr[i] = mk()` all yielded a zero
  placeholder, and a struct containing a struct (`Body { V pos; }`) didn't work
  at all. The native res-ptr write ABI (and its zero-placeholder fallback) is
  now gated on `struct_is_trivially_unboxable`; every other struct flows as a
  normal boxed `VMValue` return — the function stores its boxed object into the
  result slot and the caller loads it. Nested structs now round-trip
  (`bs[0].pos.x`, `bs[0].mass = 99`), as do float/string/mixed-field structs
  returned from constructors. Trivially-unboxable (int/bool) struct returns keep
  the native fast path unchanged.
- **`call(fn, a, b, …)` forwards N arguments (was a segfault).** Dynamic
  dispatch only knew the 0- and 1-argument shapes; calling a by-name function
  with 2+ args silently dropped the extras, so the boxed callee read an
  unpassed pointer parameter as garbage and crashed. Added `aot_call_dynamic_n`
  / `aot_invoke_boxed_n` (`runtime_bindings.cpp`), which invoke the target
  through its *registered arity* (0–8), padding missing params with VOID and
  ignoring extras so the pointer count always matches the callee (wasm's typed
  `call_indirect` included); a codegen branch (`argument_count >= 3`) stashes
  the args in a stack VMValue array, and `call` is now treated as variadic in
  the type checker. Callbacks no longer have to be 0-arg + global context — a
  `func on_hit(a, b)` handler can be `call()`-ed directly. The existing
  1-argument path (Wings `call(handler, req)`) is unchanged.
- **A native (int/bool) `struct` stored in an array lost field access.**
  `push(arr, s)` then `arr[i].x` reported "Invalid index or target". Trivially
  unboxable structs (all int/bool fields) lower to a native LLVM aggregate;
  once the static type is gone inside a dynamically-typed array, `arr[i].x` is
  a runtime string-key lookup, which the compact int-indexed `ObjStruct` can't
  serve. Float-carrying structs already lived as key-value objects and worked.
  The push/array-literal boxing now converts a native struct to the same
  string-keyed `VM_OBJECT` (`box_native_struct_as_object` in `llvm_backend.cpp`),
  so `struct Ent { int x; int y; }` works in an array with `ents[i].x = …` —
  no more parallel-array workaround. Static `s.x` on a typed local is
  unchanged; no regression across the 76-example suite.
- **Pulling a struct back out of a dynamic array (`Ent e = arr[i]`) read
  zeros.** The extraction path unpacked an int-indexed `ObjStruct`, but after
  the fix above a struct in an array is a string-keyed `VM_OBJECT`, so the
  fields didn't line up. `aot_struct_unpack_named` now resolves fields BY NAME
  from an object (and still copies an `ObjStruct` by position, so a `match`
  subject keeps working); codegen emits the declaration-ordered field-name
  table. `Ent e = ents[i]` now copies the stored fields with value semantics
  (mutating `e` leaves `ents[i]` untouched).
- **Assigning a struct value into an array element (`arr[i] = mk(…)`) stored a
  zero placeholder.** A struct-returning call / struct local on the RHS of an
  element assignment now boxes through the same string-keyed object path as
  `push`, via the shared `codegen_struct_expr_as_object` helper (which also
  backs `push` and array literals — deduplicated). As a side benefit, an array
  literal of struct-returning calls (`[mk(3,4), mk(5,6)]`) now works too (the
  old push/array-literal code only recognised struct *identifiers*).
- **Method-style calls now work on any receiver, not just a bare identifier.**
  `r.area()` worked, but `ents[i].area()` (array element) and `mk(3,5).area()`
  (call result) fell through to a field-closure call and crashed with "Null
  closure". The parser now rewrites `<expr>.<name>(args)` for any head, and
  `resolve_qualified_call` sends a non-identifier receiver straight to method
  dispatch (`name(recv, args)`) — it can never be a module alias. A boxed
  struct receiver (e.g. an array element) is unpacked into the struct
  parameter (`emit_unpack_boxed_struct_into`), so `ents[i].area()`,
  `rs[0].scaled(4)`, and `js[0].wide()` all resolve. Existing identifier /
  json / module-alias call sites are unchanged.
- **Arcade: `arcade_topla` read enemy velocity with a handle.** The bounce logic
  indexed the engine's parallel arrays directly (`_evx[enemy_id]`), but ids are
  handles (`gen * 2^20 + slot`), so it read past the array rather than the
  enemy's velocity. Use the new `vx_of()/vy_of()` getters, which resolve the
  handle to a slot.
- **Arcade: `overlaps()/degiyor()` had the same handle bug.** It took entity ids
  but indexed the parallel arrays with them directly, so the manual overlap test
  read out of bounds for every id (every id is a handle since slot recycling
  landed). It now resolves through `_slot_of()` and returns false for a stale or
  dead handle.
- **`tulpar build --target=web -o game.html` produced `game.html.html`.** The web
  output name is a *base*: the `.html` shell, `.js` and `.wasm` are all appended
  to it, so spelling the extension (the natural thing to write) doubled it and
  also produced `game.html.js`. A trailing `.html` is now stripped once, so
  `-o game` and `-o game.html` both emit `game.html` + `game.js` + `game.wasm`.
- **A local redeclared in a sibling block lost its initializer.** `if`/`else`
  blocks don't open a scope (only functions, lambdas, `for` and main do), so two
  declarations of the same name in sibling branches landed in the *same* scope.
  `add_local` appended a second entry while every lookup scans front-to-back and
  returns the first hit, so the later declaration's initializer store went to an
  alloca nothing ever read: the variable read back as the first branch's value,
  or — when that branch never ran — as uninitialised stack garbage. A
  declaration now reuses the existing slot, so the most recent one wins.
  Surfaced as the arcade platformer reading a garbage speed
  (`float s = _espeed[i]`) and slamming the player into the screen edge.
  Regression suite: `tests/scope_redecl.test.tpr`.
- **Silently wrong code from the native (all-int) fast path.** Functions typed
  `func f(int p): int` are emitted by `codegen_native_func_def`, a hand-rolled
  i64 statement subset that **silently drops** any statement it has no explicit
  case for. Its eligibility gate (`native_codegen_supports_stmt`) was far more
  permissive than the emitter, so ordinary typed code was miscompiled with no
  diagnostic:
  - `if (c) { return 1; } else { return 2; }` never emitted the else branch —
    every input taking the else returned **0**.
  - `if (c) { int a = 1; return a; }` dropped the declaration, leaving `a`
    unregistered and **crashing the compiler** (segfault).
  - a nested `if` inside a `while`/`for` body was dropped (loop bodies only
    emitted assignments and declarations), so accumulator loops computed the
    wrong result.

  The gate now mirrors the emitter exactly; anything richer falls back to the
  boxed VMValue codegen, which handles the full language. Guarded recursion
  (`if (n < 2) { return n; }` in `fib`) still takes the native path.
  Regression suite: `tests/native_fastpath.test.tpr`.
- **A builtin call in a native loop body was silently dropped** (open since
  2026-07-03). Native locals are i64, but `codegen_typed_expr` returns a *boxed*
  VMValue whenever an expression isn't statically int — typically when it
  contains a call. The while/for body emitters stored only the
  `INFERRED_INT`/`BOOL` case and discarded anything else without a diagnostic,
  so the store never happened:
  - `while (i < 3) { toplam = toplam + mod(n, 10); i = i + 1; }` returned **0** —
    the accumulator kept its old value.
  - `while (b != 0) { b = mod(a, b); ... }` (iterative Euclid GCD) never updated
    `b` → **segfault**.
  - `while (n > 0) { n = toInt(n / 10); ... }` never updated `n` → **infinite
    hang**.

  Loop bodies now unbox the payload (field 2), the same treatment the loop
  *condition* already applied to a boxed compare. `for` bodies had the identical
  defect and got the identical fix. This is the long-standing "while loop +
  builtin call miscompiles, use `for`/recursion instead" workaround — it is no
  longer needed.

## [v3.10.0]

A terminal-UI builtin suite for building flicker-free, app-like TUIs in pure
TulparLang, a locale probe, string-escape parsing, and an AOT codegen
correctness fix for comparison-heavy programs on LLVM 22. All
backwards-compatible.

### Added
- **Full-screen TUI builtins.** The flicker-free details (alternate screen,
  synchronized output, cursor home, line-wrap off) are hidden behind clean
  builtins so apps read like Python and never write raw ANSI themselves:
  - **`screen_open(): void`** — enter the alternate screen, hide the cursor,
    disable line-wrap, clear. **`screen_close(): void`** — the inverse (restore
    the normal screen, cursor, and wrap).
  - **`screen_render(frame: str): void`** — draw one frame atomically via
    synchronized output with the cursor homed, so a full repaint never tears or
    scrolls. The app builds the frame as a normal string; unlike `print()` it
    adds no trailing newline.
  - **`style(s: str, spec: str): str`** — wrap `s` in ANSI styles from a
    space-separated spec (`bold dim italic underline invert`; color names
    `red green yellow blue magenta cyan white gray`; `bright-<color>`;
    `on-<color>` backgrounds) instead of hand-written escapes.
  - **`display_width(s: str): int`** — visible terminal column width of `s`,
    ANSI- and UTF-8-aware (color codes count 0, wide/emoji 2, combining marks 0).
    Correct for alignment where byte-based `length()` is not.
  - **`fit_width(s: str, width: int): str`** — fit `s` to exactly `width`
    columns: truncate at a code-point boundary with `…`, or right-pad with
    spaces. For laying out TUI columns.
  - **`term_width(): int` / `term_height(): int`** — controlling-terminal size
    (columns / rows), falling back to 80 / 24 when it can't be queried. For
    responsive layout.
  - **`read_key_timeout(ms: int): str`** — like `read_key()` but waits at most
    `ms` milliseconds, returning `""` on timeout. Turns a blocking key read into
    the event loop a live/animated TUI (spinners, progress, auto-refresh) needs.
- **`sys_lang(): str`** — the OS UI language as a lowercase ISO-639 code
  (`"tr"`, `"en"`, …), or `""` when undeterminable. For app localization.
- **Octal and hex string escapes.** String literals now accept `\NNN` (octal,
  e.g. `\033`) and `\xNN` (hex, e.g. `\x1b`) alongside the existing
  `\n \t \r \e \\ \"`, so ANSI/control sequences can be written directly.
- **`ord(s: str, i: int): int`** — the unsigned byte value (0–255) at byte index
  `i` of `s`, or `-1` if out of range. Strings are UTF-8 byte sequences and
  `length()` / `substring()` are byte-based, so `ord` is the missing primitive
  for hand-rolled UTF-8 handling — e.g. deleting a whole multi-byte code point by
  walking back over continuation bytes (`0x80`–`0xBF`), which a naive
  drop-one-byte would corrupt.

### Fixed
- **Invalid O3 IR for comparison-heavy programs on LLVM 22.** The AOT backend's
  boxed-comparison fast-path merge (int / float / runtime-fallback) built its
  boolean result three different ways, so when a later truthiness check let
  LLVM 22's InstCombine `foldOpIntoPhi` sink the compare through the merge phi,
  it could leave a transient PHI with mismatched operand types
  (`phi i1 [ i1, i1, i64 ]`). It is self-correcting at InstCombine fixpoint, but
  the O1/O2/O3 pipelines run InstCombine with a bounded iteration count, so the
  invalid state could reach the verifier and force the whole module down to
  unoptimized. Two changes fix it: (1) the runtime-fallback path now rebuilds a
  comparison's boolean as the same `zext(i1)` shape as the fast paths, so all
  three phi operands are uniform and the fold is clean; and (2) if the
  in-process verifier still rejects the transient state, `llvm_backend_optimize`
  recovers by round-tripping the module through the IR printer/parser and
  re-verifying, keeping the full optimization level (it only ever emits a module
  that passes the verifier). LLVM 18 was unaffected; the toolchain-specific
  `TULPAR_AOT_DEBUG_O3=1` hook remains for diagnosis.

## [v3.9.0]

New backwards-compatible builtin for interactive terminal UIs.

### Added
- **`read_key(): str`** — blocks for a single keypress with no Enter and no echo,
  and returns its name. Arrow keys resolve to `"up"` / `"down"` / `"left"` /
  `"right"`; other special keys to `"enter"` / `"esc"` / `"space"` / `"tab"` /
  `"backspace"`; printable keys return the character itself. Backed by `_getch`
  on Windows and a `termios` raw-mode read on POSIX (with a graceful plain-read
  fallback when stdin is not a TTY). This is the missing primitive for building
  real app-like TUIs — arrow-key navigation, live selection, in-place repaint —
  instead of line-based `input()` prompts.

## [v3.8.1]

### Fixed
- **Windows console UTF-8 for AOT programs.** AOT-compiled binaries run their
  own entry point and call `aot_runtime_init()`, which set the UTF-8 locale and
  started Winsock but never switched the console code page. On Windows this made
  `print(...)` output with box-drawing, emoji, or Turkish characters render as
  code-page mojibake. `aot_runtime_init()` now calls `SetConsoleOutputCP(CP_UTF8)`
  / `SetConsoleCP(CP_UTF8)` (the compiler driver already did this for itself), so
  every compiled program renders UTF-8 correctly with no per-program workaround.

## [v3.8.0]

New backwards-compatible builtin plus a native-codegen correctness fix.

### Added
- **`sys_run(cmd: str): int`** — runs a shell command with inherited stdio (its
  output streams live) and returns the process exit code (`0` = success). Lets
  Tulpar programs drive external tools such as `winget`, `git`, or any CLI.
  Wired through the standard builtin path: runtime binding (`aot_sys_run`),
  typeinfer signature, LSP doc, and LLVM codegen dispatch.
- `TULPAR_AOT_EMIT_LL_PRE=1` — dump the pre-optimization LLVM IR (debug aid,
  companion to `TULPAR_AOT_EMIT_LL`).

### Fixed
- Native (all-int) fast-path codegen mis-compiled two cases exposed by
  value-returning functions that read globals:
  - `while` / `for` conditions that come back boxed (e.g. `i < length(arr)`)
    were emitted as `br i1 true` — an infinite loop. The boxed payload is now
    tested `!= 0`, matching the `if`-condition path.
  - reading an array/object subscript (`arr[i]`) inside a native function
    returned garbage; such statements now fall back to the boxed VMValue
    codegen, mirroring the existing subscript-assignment bail.

## [v3.7.0]

Backwards-compatible **developer-experience round** (design doc:
[WINGS_DX.md](WINGS_DX.md), cheatsheet: [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md)).
No breaking changes — every rename keeps the old name as a delegating wrapper.

### Added — ORM v2 (lib/orm.tpr, pure Tulpar)
- **Model handles + UFCS:** `database(path)` + `model(table, schema)` return a
  plain-json handle used method-style — `Note.create({...})`, `Note.find(id)`,
  `Note.all()`, `Note.where("done = ?", [0])`, `Note.first(cond, params)`,
  `Note.count()`, `Note.update(id, {...})`, `Note.save(obj)` (upsert),
  `Note.remove(id)`, `Note.raw(where_sql)` (escape hatch).
- **Schema shorthands** shared with `body_schema` vocabulary: `"pk"`, `"str"`,
  `"int"`, `"float"`, `"bool"` (+ `!` = NOT NULL); anything unrecognized passes
  through as raw SQL (`"TEXT UNIQUE"`). `"bool"` columns are cast to
  `true`/`false` on read — no more hand-written `row_to_note()` converters.
- **Parameterized SQL everywhere:** every generated statement binds values via
  the 3-arg `db_query`/`db_execute` (`?` placeholders); nothing is ever
  string-interpolated into SQL. Multiple databases work — each handle carries
  its own connection.
- New regression suite `tests/orm.test.tpr` (11 cases).

### Fixed — ORM
- **Zero/empty values are no longer silently dropped.** v1 selected columns by
  value truthiness, so `orm_update(id, {"done": 0})` (or `""`/`false`) wrote
  nothing. Column selection now iterates `keys(attrs)` — explicit `0`/`""`/
  `false` persist. Applies to the v1 wrappers too (intentional behavior fix).

### Added — wings DX layer (lib/wings.tpr, pure Tulpar)
- **`resource(path, Model[, opts])`** — automatic REST CRUD from an ORM model
  handle: `GET/POST path`, `GET/PUT/DELETE path/:id`, request schema derived
  from the model (`"str!"` → required, others optional → auto-422), rows
  bool-cast, `/docs` + OpenAPI fed automatically.
  `opts: {"only": [...]}` / `{"except": [...]}`
  (actions: index/show/create/update/destroy). A persistent CRUD API is now
  7 lines (`examples/wings_orm_resource.tpr`).
- **`serve(port, workers)`** — one front door for the server:
  `serve()` → 8484, `serve(8080)` → explicit port, `serve(8080, 4)` →
  `listen_pool`. The `listen*` family remains as advanced modes.
- **Short names (old names still work):** `cookies(req)` (`wings_cookies`),
  `ws_upgrade/ws_send/ws_close/ws_pong` (`wings_ws_*`), `sse_headers/sse_event`
  (`wings_sse_*`), `metrics_prom()` (`wings_metrics_prom`), `gzip(min?)`
  (`enable_gzip`), `delete(path, h)` (`del`), `accepts(schema)` (`body_schema`),
  `returns(schema)` (`response_model`).
- New regression suite `tests/wings_dx.test.tpr` (10 cases: route wiring,
  only/except filters, schema derivation, CRUD envelope statuses, aliases).

### Changed — tooling & docs
- LSP builtin table (`src/lsp/builtins.cpp`): added the wings DX layer, the
  previously unregistered `patch/head/options`/`body_schema`/`response_model`
  and request readers (`param`/`query`/`form`), and the **entire ORM v2
  surface** (ORM had zero LSP presence before) — everything now shows up in
  completion/hover.
- `examples/wings_notes_db.tpr` modernized: bound-parameter SQL (its comments
  wrongly claimed parameterized queries don't exist — stale since v3.3.0),
  function-ref handlers, `accepts()`/`delete()`; repositioned as the
  "hand-rolled SQL" teaching counterpart to `wings_orm_resource.tpr`.
- `examples/api_wings_crud.tpr` modernized: `req` parameter + `req.json`
  instead of the `_request` global, function-ref handlers, `accepts()` schema,
  `serve(3000)`.
- README: modern 8-line hello (function refs + `serve`), new 7-line
  persistent-CRUD showcase, `serve()` documented as the front door.
- New docs: `WINGS_DX.md` (design study), `WINGS_CHEATSHEET.md` (one-page
  UFCS-first API map).

## [v3.6.0]

Backwards-compatible feature round on top of v3.5.0: **wings completeness**
(the framework-parity follow-up from HANDOFF §2). No breaking changes.

### Added — wings (lib/wings.tpr, pure Tulpar)
- **Cookie SET side:** `set_cookie(res, name, val, opts)` /
  `delete_cookie(res, name)` build the `Set-Cookie` response header
  (opts: `path`, `domain`, `max_age`, `same_site`, `secure`, `http_only` —
  HttpOnly + SameSite=Lax + Path=/ by default). One cookie per response
  (response headers are a dict; last call wins).
- **Signed cookies:** `set_signed_cookie(res, name, val, secret, opts)` /
  `get_signed_cookie(req, name, secret)` — value carries an
  `hmac_sha256(secret, name + "." + value)` MAC (name-bound, so cookies
  can't be swapped); tampered/absent → `""`. MAC comparison is
  double-HMAC'd so string-compare timing reveals nothing.
- **Server-side sessions:** `session_start(req, secret)` (existing valid
  signed `tsid` cookie or fresh `secure_token(32)` id),
  `session_attach(res, sid, secret)`, `session_set/get(sid, key[, val])`,
  `session_destroy(sid)`. In-memory (`_wings_sessions` global, auto-persist);
  process-lifetime only.
- **Configurable CORS:** `cors(origin, {credentials, methods, headers,
  expose, max_age})` replaces the static wildcard defaults; sets
  `Vary: Origin` for specific origins and supports
  `Access-Control-Allow-Credentials: true` (the wildcard can't — the exact
  gap the registry frontend hit). Startup-only, covers the automatic
  OPTIONS/preflight 204.
- **Rate limiting:** `rate_limit(max, window_s)` — fixed-window,
  `use()`-based middleware keyed on `X-Forwarded-For`/`X-Real-IP` (first
  hop) with a global-bucket fallback; over-limit → 429 + `Retry-After`.
  Table resets each window, so memory stays bounded.
- **JWT guard:** `jwt_guard(secret)` — bearer-token middleware
  wire-compatible with the `wings_jwt` package (HS256, base64url segments,
  hex-MAC signature). Missing/bad/expired → 401; valid → claims injected as
  `req["jwt"]`. `jwt_public(path)` exempts paths (exact or trailing-`*`
  prefix); `/healthz`, `/metrics`, `/docs`, `/openapi.json` exempt by
  default. Also flags bearer auth in the OpenAPI doc.
- **HTML + templates:** `html(body)` (text/html response) and
  `render(tpl, vars)` — `{{key}}` substitution over a vars dict.
- **Typed path params:** `param(req, name, fb)` / `param_int` /
  `param_bool` — mirrors of the query helpers for `:name` route params.
- **ETag / conditional requests:** `cached_get` routes now serve a strong
  body-derived `ETag` and answer a matching `If-None-Match` with an empty
  `304` instead of the cached body.
- **Response compression:** `enable_gzip(min_bytes)` — transparent gzip for
  responses ≥ threshold when the client sends `Accept-Encoding: gzip`
  (adds `Content-Encoding: gzip` + `Vary: Accept-Encoding`; skips
  `cached_get` routes, whose pinned bytes are shared by all clients; skips
  when compression doesn't shrink the body).
- **OpenAPI completeness:** `response_model` schemas now document the 200
  response body in `/openapi.json`; `jwt_guard()` (or
  `docs_security("bearer")`) advertises a `bearerAuth` security scheme so
  Swagger UI shows Authorize.

### Added — runtime / language
- **`gzip_compress(s: str) -> str`** — gzip (RFC 1952) stream of the input
  bytes via a new **in-tree DEFLATE** (`runtime/tulpar_gzip.cpp`: fixed
  Huffman + greedy LZ77 over a 32K window + CRC-32). No zlib dependency —
  AOT user binaries stay self-contained. Binary-safe both directions
  (length-tracked strings). Wired through runtime, AOT codegen, typeinfer,
  LSP. Verified byte-exact against Python `gzip.decompress` and
  `curl --compressed` (CRC checked) on text, full-byte-range binary and
  random payloads; 16.4 KB HTML compresses to 274 B (1.7%).

### Fixed
- **`response_model` no longer drops `_headers`** — the output filter now
  preserves the `_headers` envelope key, so `set_cookie(...)` /
  `redirect(...)` headers survive response-model filtering.

### Tests / examples
- `tests/wings_features.test.tpr` — 13/13 (cookie builder, signed-cookie
  round-trip + tamper, sessions, CORS, rate-limit buckets, path params,
  JWT verify + middleware, html/render, `_headers` preservation, OpenAPI
  extensions, gzip).
- `examples/wings_features_api.tpr` — live showcase of the whole round
  (compile-only in CI; every endpoint verified with curl: sessions
  visits 1→2, tamper → 401, 100×200 → 429 + per-IP isolation, ETag → 304,
  gzip byte-exact).

## [v3.5.0]

Backwards-compatible feature on top of v3.4.0. No breaking changes.

### Added
- **`hmac_sha256(key: str, msg: str) -> str`** — keyed message
  authentication (HMAC-SHA256, RFC 2104) as a lowercase 64-char hex digest,
  built on the in-tree SHA-256 (no OpenSSL). The signing/verification
  building block for signed cookies, webhook signatures and JWT-style
  tokens — verify by recomputing the MAC and comparing. Wired through
  runtime, AOT codegen, typeinfer and the LSP builtin table. Validated
  against the RFC 4231 test vectors.
- **First registry package: `wings_jwt`** (`packages/wings_jwt/`) — HS256
  signed session tokens for wings apps (`sign` / `sign_ttl` / `verify` /
  `decode` / `from_header`), zero dependencies, 8/8 tests. Built on
  `hmac_sha256` + `base64_encode`. The first real, installable content for
  the `api.pkg.tulparlang.dev` registry beyond smoke packages.

### Fixed
- **`db_execute` typecheck return type** restored to `bool` (was wrongly
  changed to `int` in v3.3.0 when the parameterized-SQL overload was added).
  The runtime always returned `VM_BOOL(rc == SQLITE_OK)`, so the catalog lied
  — under `strict = true` this rejected the idiomatic `bool ok = db_execute(…)`
  with "expected bool, got int", which silently broke strict-mode builds of
  the `tulpar-be` registry. `int ok = db_execute(…)` still works (AOT bool→int
  decl coercion is unaffected).

### Docs
- New **Crypto & security** section in the built-ins reference (EN + TR)
  documenting `sha256` / `hmac_sha256` / `password_hash` / `password_verify`
  / `secure_token` / base64 with guidance on which to use where.

## [v3.4.0]

Backwards-compatible feature on top of v3.3.0. No breaking changes.

### Added
- **`secure_token(n: int) -> str`** — cryptographically secure random base62
  string of length `n`, backed by `std::random_device` (OS CSPRNG / `/dev/urandom`),
  unbiased via rejection sampling. Use this — **not** `randint`/`random` (the
  non-crypto `rand()` seeded with `time()`) — for session tokens, salts and any
  other security-sensitive randomness. Wired through runtime, AOT codegen,
  typeinfer and the LSP builtin table.

## [Unreleased] — v3.3.0 (candidate)

Backwards-compatible features on top of v3.2.1. No breaking changes.

### Added (v3.3.0)
- **Parameterized SQL queries.** `db_query(db, sql, params)` and
  `db_execute(db, sql, params)` now accept an optional array of bound values for
  `?` placeholders (`sqlite3_bind_*`), so user input never touches the SQL text —
  injection-safe without manual quote-escaping. The 2-arg forms are unchanged;
  the cached prepared-statement path is reused (constant SQL = one cache entry).
  `db_execute` returns a success bool.
- **Password hashing KDF.** New `password_hash(pw)` and
  `password_verify(pw, stored)` builtins implementing PBKDF2-HMAC-SHA256
  (100k iterations, random 16-byte salt, self-describing
  `pbkdf2_sha256$iters$salt$dk` string, constant-time verify). Built on the
  in-tree SHA-256 — no OpenSSL dependency. Use these for auth instead of bare
  `sha256`.
- **Wings `patch` / `head` / `options` route helpers.** First-class verbs
  alongside `get`/`post`/`put`/`del` (the router matches the method string
  generically, so PATCH/HEAD/OPTIONS requests dispatch correctly).

### Added (earlier, v3.1.0–v3.2.1)
- **SQLite parallel reads under WAL.** A DB handle is now a `DbConn` descriptor
  index (user API unchanged); file-backed databases open a per-thread `sqlite3`
  connection lazily so `listen_pool` workers read in parallel instead of
  serializing on one connection's mutex. Measured read-by-PK throughput
  23.8k → 35.1k RPS (~+47%); RSS stays flat (~9.9 MB). `:memory:`/temp DBs keep
  a single shared connection.
- **`db_open` server-friendly defaults.** `busy_timeout=5000` + WAL +
  `synchronous=NORMAL` on file-backed DBs (write throughput ~2.3× in the stress
  harness). Opt out with `TULPAR_DB_NO_WAL=1`.
- **Wings ergonomics (FastAPI-level).** Function-reference handlers
  (`get("/users", list_users)`), `req` parameter (`req.params.id`, `req.json`),
  response helpers (`ok`/`created`/`not_found`/…), automatic JSON body parse,
  invisible auto-persist (writes to globals survive the per-request arena),
  schema validation (`body_schema({...})` → automatic 422), automatic `/docs`
  (Swagger UI + `/openapi.json`), and a branded default port 8484.
- **Language: `async` `gather(...)`** (concurrent awaits) and **`match`
  destructuring** — arrays (`[head, ..tail]`), json/object fields
  (`{role: "admin", name}`), typed-struct variants (`Circle{r}`), and nested
  patterns.
- Benchmark + stress harness: Wings vs FastAPI comparison and a multi-threaded
  HTTP + SQLite load generator (`benchmarks/`).

### Fixed
- **Thread-safety audit of the AOT runtime** (affects `listen_pool` /
  `listen_async`):
  - `toString()` used a shared, non-thread-local scratch buffer; concurrent
    callers could clobber it, yielding an empty result → malformed SQL → ~1.1%
    spurious 404s. Now `thread_local`.
  - The exception-handler context (`eh_main`/`eh_cur`) was global; a pooled
    handler using `try`/`throw` could `longjmp` across worker threads
    (crash/UB). Now `thread_local`.
  - The dynamic-call cache published its slot key with a plain store — correct
    on x86 TSO but not on ARM/aarch64 (an Apple Silicon / aarch64 target could
    read a stale function pointer). Now an `std::atomic` release/acquire publish.
- **Per-request memory leak on the Wings hot path** — per-request malloc region
  + runtime write-barrier keep RSS flat (ASan clean).
- `db_last_insert_id` / `db_error` codegen signatures (LLVM module-verification
  warning on every DB program).
- Default arguments (missing trailing args pad to boxed `0`), `\e`/`\0` string
  escapes, boxed unary-minus, and a clean Ctrl+C exit (no misleading
  "compile/link failed").

### Repo hygiene
- Stop tracking accidentally-committed local dirs (`github/` dot-less duplicate
  of `.github/`, `claude/` Claude Code lock, `.opencode/` tool config).

## [3.0.0] — 2026-06-15

### Changed (breaking)
- **AOT-only architecture.** The bytecode VM interpreter, the AST→bytecode
  compiler, and the REPL were removed — Tulpar now follows the C/Rust/Go model
  with a single AOT/LLVM execution path. `--vm`/`--run` are ignored with a
  warning; `--repl`/`-i` print a removal notice. An AOT failure is now a hard
  error (no VM fallback).

### Added
- **`async`/`await` v1** — stackful coroutines + event loop (POSIX `ucontext` /
  Windows fibers), non-blocking `sleep_async`, coroutine-aware exception context.
- **`match` v1.1** — literal / `_` / `|`-alternatives / inclusive ranges.
- Cross-platform async build (macOS `ucontext`, Windows fibers, MinGW).

## [2.2.0] — 2026-06-01

### Changed
- CI switched to **stable-only versioning** — releases are cut only on `v*` tag
  pushes (no more rolling per-commit releases).

### Fixed
- VM typed-struct params pass by value (mirrors AOT semantics); bool→int
  coercion at typed local var declarations; wired 8 utility builtins (arena,
  cpu/time, input, `string_pin`).

[Unreleased]: https://github.com/hamer1818/TulparLang/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/hamer1818/TulparLang/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/hamer1818/TulparLang/releases/tag/v2.2.0

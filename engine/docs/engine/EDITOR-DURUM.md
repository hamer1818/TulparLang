# Tulpar Editör — Durum, Boşluklar ve Yol Haritası

**Ölçüm tarihi:** 2026-09-18 · **Ölçülen ağaç:** yerel `master` + Faz 0 dalı (`editor/faz0-olu-arayuz`)

> **Durum (2026-09-19):** Faz 0, A, B ve C uygulandı. PR
> [#332](https://github.com/hamer1818/TulparLang/pull/332) CI'da **yeşil** —
> `build-linux` ve `build-macos` ikisi de geçti. Bu, motorun uzun süredir
> **ilk kez derlendiği** koşu: §8'e bakın.

Bu belge editörün *iddia ettiği* ile *yaptığı* arasındaki farkı ölçer ve kapatma sırasını verir.
Hiçbir madde tahmin değil; her birinin altında onu üreten ölçüm var. Bir madde "ölçülmedi"
diyorsa, kodlamaya başlamadan önce ölçülmesi gerekir — varsayımla kod yazmak bu belgenin
listelediği sorunların birinci sebebi.

**Temel kural:** hiçbir şey sıfırdan yazılmaz. Önce depoda duran koda, sonra doğrulanmış
açık kaynağa bakılır. Her yapılacak maddesinin yanında kaynağı yazılıdır.

---

## 0. Nasıl ölçüldü (tekrarlanabilir)

```bash
python tools/layer_check.py .     # katman ihlali        -> bugün 0 / 334 dosya
python tools/icon_check.py .      # çizilemeyen ikon     -> bugün 0 eksik glif
python tools/layout_check.py .    # CPU-GPU yerleşim     -> 39 blok / 16 shader, 0 uyuşmazlık
./build.sh test                   # 475 test, tavan 512  (tests/test.hpp:20)
```

Bu belgedeki tablolar şu üç kaynağın kesişiminden çıkarıldı:

| Soru | Nerede bakılır |
|---|---|
| Arayüzü var mı? | `app/editor_app.cpp` → `component_header(...)` / `prop_*(...)` |
| `.sahne` metnine yazılıyor mu? | `content/scene.cpp` → `kSceneXxx` ve alan adları |
| `.sahneb` blob'una giriyor mu? | `content/scene_blob.cpp` + `content/scene_blob.hpp` → `SceneBlobXxx` |
| Çalışma zamanında etkisi var mı? | `content/scene_runtime.cpp` → `SceneBlobXxx` okunuyor mu |

---

## 1. Bileşen matrisi — belgenin omurgası

18 bileşen bitinin dört halkası. `+` var, `−` yok.

| # | Bileşen | Panel | `.sahne` | `.sahneb` | Runtime | Sonuç |
|---|---|:---:|:---:|:---:|:---:|---|
| 0 | Model | + | + | +¹ | + | **tam** (PBR alanları hariç, bkz. §2.D) |
| 1 | Anim | + | + | + | + | **tam** (klip seçici yok) |
| 2 | Light | + | + | + | + | **tam** |
| 3 | Body | + | + | + | + | **tam** |
| 4 | Camera | + | + | **−** | **−** | editörde çalışır, **oyunda kaybolur** |
| 5 | Audio | + | + | **−** | **−** | hiçbir yerde ses çalmaz |
| 6 | Script | + | + | **−** | **−** | `.tpr` hiçbir yorumlayıcıya gitmez |
| 7 | Character | **−** | + | + | **−** | paneli yok, blob'a yazılır, **okunmaz** |
| 8 | Particle | **−** | + | + | +² | paneli yok, **çizilmiyor** (§2.C) |
| 9 | Terrain | + | + | + | + | **tam** |
| 10 | Voxel | **−** | + | + | + | **paneli yok**, gerisi çalışıyor |
| 11 | Water | **−** | + | + | + | **paneli yok**, gerisi çalışıyor |
| 12 | Wind | **−** | + | + | **−** | paneli yok, blob'a yazılır, **okunmaz** |
| 13 | NavAgent | + | **−** | **−** | **−** | **kaydedilmez** |
| 14 | Joint | + | **−** | **−** | **−** | **kaydedilmez** |
| 15 | Skybox | + | **−** | **−** | **−** | **kaydedilmez**, alanı bile yok |
| 16 | RefProbe | + | **−** | **−** | **−** | **kaydedilmez** |
| 17 | Reverb | + | **−** | **−** | **−** | **kaydedilmez** |

¹ Model'in ayrı tablosu yok; `SceneBlobDraw` taşıyor. ² Yayılıyor ve simüle ediliyor ama ekrana gelmiyor.

**Özet:** 18 bileşenin **4'ü** uçtan uca tam. 5'inin paneli yok, 5'i diske hiç yazılmıyor,
3'ü blob'a yazılıp okunmuyor.

---

## 2. Bulgular — şiddet sırasıyla

### A. SESSİZ VERİ KAYBI — beş bileşen `.sahne`'ye hiç yazılmıyor

```
content/scene.cpp'de geçiş sayısı:
  kSceneNavAgent  0      kSceneRefProbe  0
  kSceneJoint     0      kSceneReverb    0
  kSceneSkybox    0
```

Bu beşinin **tam çalışan inspector paneli var**. Kullanıcı değerleri giriyor, kaydediyor,
dosyayı açıyor — hepsi gitmiş. Hata yok, uyarı yok.

Etkilenen alanlar `SceneEntity` içinde duruyor ama serileştirilmiyor:

| Bileşen | Alanlar |
|---|---|
| NavAgent | `ai_target`, `ai_speed`, `ai_turn_speed` |
| Joint | `joint_target`, `joint_axis`, `joint_limit_min`, `joint_limit_max`, `joint_motor_speed` |
| RefProbe | `ref_probe_radius`, `ref_probe_intensity` |
| Reverb | `reverb_decay`, `reverb_room_size` |
| Skybox | **hiç yok** — panelde sabit bir `Button("hdri_sky.hdr")` var, arkasında alan da yok, işleyici de |

> **Neden bu birinci sırada:** diğer bütün sorunlarda kullanıcı "çalışmıyor" der ve devam eder.
> Bunda kullanıcı **işini kaybeder** ve neden kaybettiğini anlamaz.

### B. PANEL KABUKLARI — görünüyor, hiçbir şey yapmıyor

| Panel | Ölçüm | Maliyeti |
|---|---|---|
| **Materyal Graph** | Tek **sabit** düğüm (`ImNodes::BeginNode(1)`), 3 giriş pini, bağlantı yok, hiçbir malzemeye dokunmuyor | `imnodes` 3.273 satır vendor'landı |
| **Animasyon Sequencer** | Gövdesi tek satır: `TextDisabled("ImSequencer entegrasyonu ... çizilecek")` + yorum satırı hâlinde çağrı | `ImSequencer` 695 + `ImCurveEdit` 466 satır derleniyor; **`ImCurveEdit` app/'de 0 kez geçiyor** |
| **Arazi Fırçası** | `brush_radius`, `brush_density`, `brush_mode` üçü de `static` yerel; panel dışında **hiç okunmuyor** | — |
| **Girdi Yöneticisi** | `BulletText("Klavye: W")`, `"Gamepad: Sol Analog Y"` sabit metin; `Button("+ Yeni Atama Ekle")` işleyicisiz | — |
| **Skybox paneli** | `Button("hdri_sky.hdr")` — işleyici yok, alan yok | — |

### C. PARÇACIKLAR SİMÜLE EDİLİYOR AMA ÇİZİLMİYOR

```c
// content/scene_runtime.cpp:373
if (particles_.alive_count() > 0 && prims_[0].valid()) {
```

`prims_[0]` **hiçbir zaman doldurulmuyor**. `content/primitives.cpp` şu slotları kurar:
`8, 10, 11, 20, 21, 22, 23, 24`. Yorum "Primitive Cube ile" diyor ama **küp slot 10'da**.
Koşul hiç sağlanmıyor → emit + update çalışıyor, tek piksel çizilmiyor.

### D. VERİ MODELİ BOŞTA — 80 alanın 49'unun widget'ı yok

```
SceneEntity alan sayısı      : 80
bir prop_* widget'i olan     : 31
widget'ı olmayan             : 49
```

En değerli alt küme — **motor gerçekten kullanıyor, editörde arayüzü yok:**

| Grup | Alanlar | Motor tarafı |
|---|---|---|
| **PBR** | `metallic`, `roughness`, `reflectance`, `emissive`, `emissive_strength` | `scene_runtime.cpp` + `renderer.cpp` → **gerçekten shade ediyor** |
| Model | `primitive` | `scene_runtime.cpp` + `editor_app.cpp` (Faz 0'da bağlandı) |
| Anim | `clip` | runtime kullanıyor; editörde `e.clip = 0` sabit |
| Body | `shape`, `dynamic` | Jolt'a gidiyor |
| Camera | `cam_fov` | kısmen |
| Terrain | `terrain_freq`, `terrain_octaves`, `terrain_seed` | `content/terrain.cpp` |
| Water | `wave_length`, `wave_amplitude`, `wave_steepness`, `wave_speed`, `wave_direction` | `content/water_wave.cpp` |
| Voxel | `voxel_size_x/y/z`, `voxel_cell` | `content/voxel.cpp` |
| Audio | `audio_loop`, `audio_spatial` | — (runtime yok) |
| Script | `script_file`, `script_enabled` | — (runtime yok) |
| Joint | `joint_limit_min`, `joint_limit_max` | — |
| RefProbe | `ref_probe_intensity` | — |

**PBR beşlisi bu listenin en kârlısı:** motor tarafı bitmiş, yalnız beş `prop_*` satırı eksik.
Faz 0'da post açıldığı için `emissive` artık gerçekten parlayabilir.

### E. DERLENMEYEN KAYNAK — 6 shader yazıldı, hiç build edilmedi

```
SPIR-V'si olmayan ve renderer'dan referansı olmayan:
  color_grading.frag      ssao.frag
  deferred_gbuffer.frag   ssr.frag
  dof.frag                motion_blur.frag
```

Altısının da `*_spv.h` karşılığı yok ve `renderer/` + `rhi/` içinde **0 referansı** var.
Yani GLSL kaynağı depoda duruyor, derlenmiyor, hiçbir boru hattı kullanmıyor.

### F. ÇİFT PANEL — tek bayrak iki pencere açıyor

```c
// app/editor_app.cpp
2433:  if (show_console) ShowConsolePanel(&show_console);                          // yeni (editor_console.hpp)
2518:  if (show_console && ImGui::Begin(kPanelKonsolLabel, &show_console)) ...     // eski (console_panel)
```

Konsolu açınca **iki konsol** geliyor; `Ctrl+~` ve Görünüm menüsü ikisini birden açıp kapatıyor.

Varlık tarayıcı da ikili: `assets_panel(...)` (hep çizilir) + `AssetBrowserPanel(...)` (menüden).
İkisi ayrı durum tutuyor, ayrı tarama yapıyor.

### G. HAZIR AMA ERİŞİLEMEZ

| Ne | Durum | Eksik olan |
|---|---|---|
| `layout_save` / `layout_load` / `layout_parse` | Tam, sürümlü, deterministik | Yalnız headless kapı yolundan çağrılıyor; **menüde girişi yok** → editör her açılışta düzeni unutuyor |
| `rebind_shortcut` | Çakışma denetimiyle hazır | Arayüzü yok |
| `conflicts`, `in_category`, `bound` (editor_commands) | Tanımlı | Çağıran yok |
| `component_header`'ın `enabled` parametresi | Widget tarafı tam (soluklaştırma dahil) | 13 çağrının 13'ünde `nullptr` |

---

## 3. Bağlantı haritası — "neye bağlanacak" (ölçüldü)

Bir bileşeni canlandırmadan önce motor tarafında karşılığının **var olduğu doğrulanmalı**.
Aşağıdaki sütun tahmin değil, dosya araması sonucu.

| Bileşen | Motor tarafı | Durum |
|---|---|---|
| Particle | `content/particles.hpp` | ✅ var, `scene_runtime` zaten emit/update ediyor — tek eksik çizim slotu (§2.C) |
| Terrain | `content/terrain.cpp/.hpp` | ✅ var, bağlı |
| Water | `content/water_wave.cpp/.hpp` | ✅ var, bağlı |
| Voxel | `content/voxel.cpp/.hpp` | ✅ var, bağlı |
| Character | `sim/physics.cpp` → `CharacterVirtual` | ✅ var, **bağlanmamış** |
| NavAgent | `sim/navmesh.cpp/.hpp` | ✅ var, **bağlanmamış** |
| Audio / Reverb | `audio/mixer.cpp`, `audio/spatial.hpp`, `audio/dsp.hpp` | ✅ var, **bağlanmamış** |
| Wind | `content/wind.cpp/.hpp` | ✅ var, **tüketici shader yok** |
| Skybox | — | ❌ `renderer/` + `rhi/shaders/` içinde skybox yok |
| RefProbe (IBL) | — | ⚠️ **ölçülmedi** — `content/gi.hpp` var ama IBL/prefilter yolu doğrulanmadı |
| Script | `bridge/engine_api.cpp` (169 builtin) | ✅ C ABI var, bileşene bağlanmamış |

> **Düzeltme:** bu belgenin önceki taslağında "Rüzgâr için `rhi/shaders/foliage.vert` zaten
> bekliyor" yazıyordu. **Öyle bir shader yok.** Rüzgârın görsel bir tüketicisi yazılmadan
> bileşeni canlandırmak anlamsız — `wind.cpp` değer üretir, kimse okumaz.

---

## 4. Yapılacaklar

Sıra **şiddet × maliyet** ile kuruldu: önce veri kaybı, sonra yalan söyleyen arayüz,
sonra kârlı bağlantılar, en sona pahalı yeni sistemler.

### Faz 0 — tamamlandı (dal: `editor/faz0-olu-arayuz`)

Bağlayıcı hatası · ölü menü kalemleri (20–24) · 16 kod noktası / 34 çağrıda çizilmeyen ikon ·
`kSceneLocked`/`kSceneHidden` uygulanması · `rc.post` (ışıma + bloom + 5 kaydırıcı) ·
tel kafes düğmesi · demo arka planının görünmez çarpışma kutuları.

### Faz A — veri kaybını durdur *(en yüksek şiddet, düşük maliyet)*

| # | İş | Kaynak / desen |
|---|---|---|
| A.1 | NavAgent, Joint, RefProbe, Reverb alanlarını `.sahne` yaz/oku yoluna ekle | `content/scene.cpp`'deki mevcut bileşen desenleri (Terrain/Water birebir örnek) |
| A.2 | Skybox'a gerçek bir alan ver (`skybox_asset[kScenePathLen]`) ya da bileşeni menüden çıkar | — |
| A.3 | Bu dört bileşen için `.sahne` **yaz→oku→yaz bayt-eşit** testi | `tests/test_scene.cpp` deseni |
| A.4 | **Kapı:** her `kSceneXxx` biti için metin yaz + oku + eşitlik var mı; yoksa build kırmızı | `tools/layer_check.py` / `tools/icon_check.py` deseni |

> A.4 olmadan A.1 tekrar bozulur. Bu sınıf bu projede **üç kez** oluştu (ölü bileşen, ölü menü
> kalemi, çizilmeyen ikon) ve üçü de sessizce oluştu.

### Faz B — yalan söyleyen arayüzü temizle *(düşük maliyet)*

| # | İş |
|---|---|
| B.1 | Çift konsolu tekle (`ShowConsolePanel` mi `console_panel` mi — biri kalır, öteki silinir) |
| B.2 | Çift varlık tarayıcıyı tekle |
| B.3 | Materyal Graph / Sequencer / Arazi Fırçası / Girdi Yöneticisi panelleri: ya gerçek işleve bağlanır ya **"henüz bağlanmadı"** rozetiyle işaretlenir ya kaldırılır. Sabit metin gösteren panel kalmaz |
| B.4 | Kullanılmayan `ImCurveEdit` CMake'ten çıkar (B.3'te Sequencer gerçekten bağlanmayacaksa `ImSequencer` + `imnodes` de) |
| B.5 | 6 derlenmeyen shader: `tools/compile_shaders.py`'ye alınıp renderer'a bağlanır ya da silinir |
| B.6 | **Kapı:** `rhi/shaders/*.frag|vert|comp` için `*_spv.h` var mı ve renderer'dan referansı var mı |

### Faz C — kârlı bağlantılar *(motor tarafı hazır)*

| # | İş | Neden kârlı |
|---|---|---|
| C.1 | **Parçacık çizim slotu** — `prims_[0]` → `prims_[10]` (küp) ya da parçacığa özel slot | **Tek satır**, çalışan bir sistemi görünür yapıyor |
| C.2 | **PBR paneli** — `metallic`/`roughness`/`reflectance` `prop_float`, `emissive` `prop_color`, şiddet `prop_float` 0..20 | Motor tarafı bitmiş; Faz 0'da post açıldığı için ışıma artık gerçekten parlıyor |
| C.3 | **Eksik 5 panel** — Karakter, Partikül, Voksel, Su, Rüzgâr | Alanların hepsi `SceneEntity`'de ve `.sahne`'de var |
| C.4 | **Anim klip seçici** + **Model ilkel seçici** | Runtime ikisini de okuyor |
| C.5 | **Panel düzeni menüye** — `layout_save`/`layout_load` için komut + menü girişi | Altyapı tam; **en düşük emek / en yüksek his farkı** |
| C.6 | **Bileşen başına aç/kapa** — `SceneEntity::components_enabled` maskesi | `component_header`'ın `enabled` parametresi zaten hazır |
| C.7 | Blob: Camera / Audio / Script / Character / Wind tablolarını **okuyan** taraf | Yazan taraf var; tek sürüm bump (`kSceneBlobVersion` 5 → 6) hepsini kapsar |

### Faz D — yeni çalışma zamanı *(pahalı, sırayla)*

| # | İş | Bağlanacağı yer |
|---|---|---|
| D.1 | Karakter kontrolcüsü | `sim/physics.cpp` → `CharacterVirtual` |
| D.2 | Ses kaynağı → mixer voice | `audio/mixer.cpp` + `audio/spatial.hpp` |
| D.3 | NavAgent → yol bulma | `sim/navmesh.cpp` |
| D.4 | Reverb | `audio/dsp.hpp` |
| D.5 | Rüzgâr — **önce tüketici shader** (foliage/vertex animasyonu), sonra bileşen | `content/wind.cpp` değer üretiyor, okuyan yok |
| D.6 | Skybox + IBL — **önce renderer'da yol açılmalı**, bugün hiç yok | ölçülmeli |
| D.7 | Betik (`.tpr`) çalışma zamanı | `bridge/engine_api.cpp` C ABI |

### Faz E — editör kabuğu *(sektör seviyesi)*

Depoda **zaten derlenen** `third_party/imgui/imgui_demo.cpp` (MIT) içinden uyarlanır:

| # | İş | Kaynak |
|---|---|---|
| E.1 | Konsolda komut girişi + geçmiş + Tab tamamlama | `ExampleAppConsole` (`imgui_demo.cpp:9174`) |
| E.2 | Varlık tarayıcı: klasör ağacı + küçük resim + 64 dosya tavanının kalkması | `ExampleAssetsBrowser` (`:11262`) + `third_party/stb/stb_image.h` |
| E.3 | Shift+tık aralık seçimi, Ctrl+tık, tümünü seç | `BeginMultiSelect` + `ImGuiSelectionBasicStorage` (`imgui.h:784` — depoda, **0 kullanım**) |
| E.4 | İç içe özellik ızgarası | `ExampleAppPropertyEditor` (`:9791`) |
| E.5 | Görünüm kipleri: tel kafes + unlit + overdraw + çarpışma | Barycentric tek geçiş (`fillModeNonSolid` istemez — mobil GPU'ların çoğunda yok) |
| E.6 | Proje ayarları paneli (gölge kalitesi + render ölçeği diske) | `editor_layout.cpp`'nin sürümlü metin yazıcısı |
| E.7 | `rebind_shortcut` arayüzü; `F` ve `Ctrl+D`'nin komut tablosuna alınması | `editor_commands.cpp:343-352` hazır |
| E.8 | Prefab: alt ağacı dosyaya yaz + tarayıcıda listele + örnekle | `.sahne` yazıcısı |

### Faz F — depo yapısı *(bakımcı kararı gerekiyor)*

PR #331 motor dosyalarını `engine/` altına değil **deponun köküne** yazdı ve kök
`CMakeLists.txt`'i (TulparLang'in kendi dil derlemesi, `project(TulparLang VERSION 3.13.1)`)
motorunkiyle **ezdi**. Kök CMake artık `audio/mixer.cpp`, `core/memory/arena.cpp`,
`rhi/device.cpp`, `renderer/renderer.cpp`, `app/editor_viewport.cpp` istiyor — **hiçbiri
kökte yok**. Yani kök build her iki yönde de kırık.

Ayrıca `build.yml` yalnız `main`/`master`'a açılan PR'larda koşuyor:

```yaml
pull_request:
  branches: [ main, master ]
```

`engine/editor-ui`'ye açılan PR'lar **derlenmiyor** — #327…#331'in beşi de tek bir derleme
görmeden girdi. Bu, kod kalitesinden bağımsız bir CI açığı.

| # | İş | Kim |
|---|---|---|
| F.1 | Kök `app/`, `content/`, `renderer/`, `rhi/` kopyalarını `engine/` altına taşı | PR ile olur |
| F.2 | Kök `CMakeLists.txt`'i TulparLang'inkine döndür (içerik `f628ebf`'te) | PR ile olur |
| F.3 | `build.yml`'nin `pull_request.branches` listesine `engine/editor-ui` ekle | **bakımcı kararı** (CI dakikası) |

> Motorun köke taşınması **kasıtlıysa** F.1'in tersi doğrudur. Sormadan tahmin etmek 18 bin
> satırı ikinci kez yanlış yere taşımak olur.

---

## 5. Kapılar — bu sınıflar geri gelmesin

Bugün çalışanlar:

| Kapı | Ne ölçer | Bugün |
|---|---|---|
| `tools/layer_check.py` | Katman bağımlılığı + hex kaçış taşması | 334 dosya, 0 ihlal |
| `tools/icon_check.py` | Her ikon kod noktası yüklü fontta var mı | 0 eksik glif *(pozitif kontrolle sınandı)* |
| `tools/layout_check.py` | CPU struct ↔ SPIR-V blok yerleşimi | 39 blok / 16 shader, 0 uyuşmazlık |

Eklenecekler:

| Kapı | Ne ölçer | Hangi bulguyu yakalardı |
|---|---|---|
| **Bileşen canlılık** | Her `kSceneXxx` için metin yaz + oku + eşitlik + blob yazımı var mı | §2.A (5 bileşen kaydedilmiyor) |
| **Ölü menü kalemi** | `CreateMenuItem::code`'un `do_add()` karşılığı; her bitin paneli | Faz 0'daki 5 ölü kalem |
| **Shader canlılık** | Her `.frag/.vert/.comp` için `*_spv.h` + renderer referansı | §2.E (6 derlenmeyen shader) |
| **Panel kabuğu** | Bir panelin gövdesi yalnız `Text*`/`Button` sabitlerinden mi oluşuyor | §2.B (4 kabuk panel) |

Her kapı **pozitif kontrolle** doğrulanır: kasten bozulan bir örnekte kırmızıya döndüğü
gösterilmeden yeşil olması bir şey kanıtlamaz.

---

## 6. Doğrulama

Her faz sonunda:

1. `python tools/layer_check.py .` → 0 ihlal
2. `python tools/icon_check.py .` → 0 eksik glif
3. `python tools/layout_check.py .` → 39/16, 0 uyuşmazlık *(shader değiştiyse önce `compile_shaders.py`)*
4. `./build.sh test` → yeşil *(bugün 475 test, tavan 512)*

Kırılması **beklenen** kapılar: `tests/test_scene.cpp:226` (kanonik sahne — Faz A.1 alan
eklediğinde), `tests/test_scene_blob.cpp` sürüm sabiti (Faz C.7'de 6'ya çıkar).

Gözle sınanacaklar:

- Boş sahne → sağ tık → 3B Nesne → **Kapsül görünmeli**
- Kamera / Ses / Betik ikonları **kutu değil ikon** olmalı
- Emissive şiddeti 5 → **parlamalı** (bloom)
- Parçacık bileşeni ekle → **ekranda parçacık görünmeli**
- NavAgent/Joint/RefProbe/Reverb değerleri gir → kaydet → aç → **durmalı**
- Editörü kapat/aç → **panel düzeni korunmalı**

---

## 8. Uygulama kaydı (2026-09-19) — CI ilk kez yeşil

Faz 0/A/B/C uygulandı ve PR #332'de CI **ilk kez tüm motoru derledi**. O ana
kadar `build.yml` yalnız `main`/`master`'a açılan PR'larda koştuğu için
#327–#331 arası **hiç derlenmeden** birleşmişti. Derleyici devreye girince
**~40 derleme hatası** çıktı — hiçbiri bu turda yazılan kod değildi.

### Derleyicinin bulduğu, daha önce hiç görülmemiş hatalar

| Sınıf | Nerede | Ne |
|---|---|---|
| Bildirim/tanım ayrışması | `editor_widgets.hpp` | `component_add_button`'ın 2 argümanlı sürümü bildirilmiş, tanımı yok → `engine_editor` **ve** `engine_tests` bağlanmıyor |
| Tip uyuşmazlığı | `scene.cpp` | `o.vec(Vec3)`'e `Vec2` veriliyor (su/rüzgâr yönü); aynı hata eşitlik yolunda da var |
| Aşırı yükleme yok | `scene.cpp` | `p.vec(t[1],t[2],t[3],&…)` — 4 argümanlı böyle bir şey yok |
| Çift bildirim | `scene_runtime.cpp` | `e` ve `m` aynı kapsamda iki kez |
| Ad alanı | `scene_runtime.cpp`, `editor_app.cpp` | `core::Arena` yok; `Arena` `tulpar::engine` içinde (6 yer) |
| API uydurma | her ikisi | `Renderer::draw_mesh` diye bir üye yok (6 çağrı); `Arena::init` void döner ve ad ister; `Arena::base_` private; `Rng`'nin varsayılan kurucusu ve `seed()`'i yok |
| Tip dönüşümü | her ikisi | `MeshHandle` bool bağlamında (3 yer); `return 0` ile `MeshHandle` döndürme (2 yer) |
| Eksik sabit | `editor_commands.cpp` | `kKeyF`/`kKeyD` hiç tanımlanmamış → dört `static_assert` birden patlıyor |
| Başlık ezilmesi | `editor_console.hpp` | Eski API (`ConsoleLevel`, `console_log`…) üzerine yazılmış ama `.cpp` hâlâ onu tanımlıyor ve 14 çağrı yeri kullanıyor |
| Eksik include | `editor_app.cpp` | `imnodes.h` hiç include edilmemiş, `ImNodes::CreateContext()` hiç çağrılmamış (bağlamsız `BeginNodeEditor` çöker) |
| Test API'si | `test_scene_blob.cpp` | `scene_write` `uint8_t*` ile çağrılıyor (metin yazar), olmayan `scene_read` kullanılıyor |
| Açgözlü kaçış | `editor_app.cpp`, `editor_commands.cpp` | `"\xC4\x9F"` + `a/c/e` → `\x9Fa` = `0x9FA`, `char` aralığı dışı (12 yer) |

### Testlerin bulduğu

- **SIGSEGV** — `scene_runtime_applies_baked_gi_ambient`. Sebep bu turda
  yazılan kod: `build_primitive_meshes` koşulsuz çağrılıyordu ama kapılar
  **kurulmamış** bir `Renderer` veriyor (`renderer::Renderer ren;`, `init` yok)
  ve `create_mesh` orada çöküyor. Artık tablo yalnız blob'da `primitive >= 0`
  olan bir çizim varsa kuruluyor. Aynı sınıf: 32 MB'lık prosedürel arena da
  koşulsuz ayrılıyordu, artık arazi/voksel/su varsa ayrılıyor.
- **Kanonik sahne** — `tests/assets/editor.sahne` eski `model N r g b`
  biçimindeydi, yazıcı artık `model kaynak N r g b` üretiyor. Dosya yeni
  biçime alındı.
- **Blob sürümü** — sabit 6'ya çıkmış, test hâlâ 5 bekliyordu.
- **Bağlanmamış 6 shader** — `rhi/shaders/wip/` altına alındı (§2.E).

### Süreç dersi: `tools/syntax_check.py`

Yukarıdaki hataların ~39'u `g++ -fsyntax-only` ile **saniyeler içinde**
bulunabilirdi; bunun yerine yedi CI turuna (her biri ~5–15 dk) mal oldu.
Sebep: bu makinede derleyici yoktu ve CI derleyici olarak kullanıldı.

`tools/syntax_check.py` bu boşluğu kapatır: her çeviri birimini tek tek
denetler, Windows/POSIX farklarını eler. **Şu an 99 çeviri birimi, 0 hata.**
Sınırı dosyanın başında yazılı: **bağlayıcı hatalarını görmez** — yukarıdaki
ilk satır (`component_add_button`) tam olarak o sınıftandır.

Kalıcı çözüm yine de **Faz F.3**: `build.yml`'nin `pull_request.branches`
listesine çalışma dalları eklenmeli. Yoksa bir sonraki büyük birleşme de
derlenmeden girer.

---

## 7. Kayıt: bu belgede düzeltilen kendi hatalarım

- "Rüzgâr için `rhi/shaders/foliage.vert` zaten bekliyor" — **öyle bir shader yok** (§3).
- "13 bileşen var" — **18**; #331 NavAgent/Joint/Skybox/RefProbe/Reverb ekledi.
- "9 bileşen blob'a yazılmıyor" — #331 sonrası Particle/Terrain/Water/Wind/Voxel/Character
  tabloları **eklendi**; bugünkü sorun yazmak değil, **okumak** (§1).

# Editör Planı (L7) — "işin %90'ı" dediğimiz ama planlamadığımız yer

> PLAN.md §son: *"Editor ve tooling mimarisi | Zayıf (L7 ince) | Yüksek — 'işin %90'ı'
> demiştik ama planlamadık"*. Bu belge o boşluğu kapatıyor.

Tarih: 2026-09-17 · Dal: `engine/editor-ui` · Temel: main `6803a35`

---

## 1. Bugün ne var (ölçüldü, tahmin değil)

`engine_editor` **çalışıyor** ve sıfırdan başlamıyoruz. Toplam **1.499 satır**:

| dosya | satır | ne |
|---|---:|---|
| `app/editor_app.cpp` | 886 | döngü, paneller, seçim, gizmo, kaydet/derle |
| `app/editor_ui.cpp` | 399 | ImGui + Vulkan backend, girdi köprüsü |
| `app/editor_ui.hpp` | 124 | `Selection`, `SceneHistory`, `AssetFile` |
| `app/editor.cpp` | 53 | giriş noktası, headless bayrakları |

Çalışan ve **korunacak** olanlar:

* **"The Truth"** — `content/scene.hpp::SceneDesc` tek veri kaynağı (PLAN §The Truth).
* **Geri al/yinele** — `SceneHistory`, bir sürükleme = bir işlem (ImGui activate/deactivate).
* **Çoklu seçim** — bir kullanıcı eylemi = bir geri alma grubu.
* **Tıklamayla seçim** — `scene_pick`, ışın–AABB.
* **ImGuizmo** — taşı/döndür/ölçekle, `T·Rz·Ry·Rx·S` sırası kapıyla çakılı.
* **Dört panel** — Sahne, Özellikler, Dünya, Kaynaklar.
* **Derleme** — Ctrl+B → `.sahneb`; headless koşumda kapı.
* **Headless kapı** — `editor_imgui_draws_into_offscreen_pass`.

## 2. Üç YAPISAL boşluk (hepsi ölçüldü)

Bunlar "eksik özellik" değil; sektör tarzı bir editörü **imkânsız kılan** mimari kısıtlar.

### B1 — Docking YOK
Vendored ImGui **1.92.9b master dalı**; `IMGUI_HAS_DOCK` ve `DockBuilder` **0 eşleşme**.
Unity/Unreal/Godot/Blender'ın tamamında panel düzeni kullanıcı tarafından sürüklenip
yeniden düzenlenebilir. Bizde paneller ekranda yüzen sabit pencereler.

**Risk düşük:** ImGui'de yerel yama YOK (`TULPAR PATCH` araması boş); tek özelleştirme
`imconfig.h`'deki `IM_VEC2/VEC4_CLASS_EXTRA`. Docking dalına geçiş = dosyaları
yeniden vendor et + `imconfig.h`'yi koru. ImGuizmo docking'e bağımlı değil (0 eşleşme).

### B2 — 3B sahne DOKUYA çizilmiyor
`editor_app.cpp:50-52`: `r->record(cb)` → `r->ui_record(cb)` → `ui->record(cb)` —
üçü de **aynı renk subpass'i**. Yani 3B tam ekran, ImGui üstünde yüzüyor. Bu
"debug kaplamalı oyun" modeli; editör değil.

Sonuçları: sahne bir panelin içine konamaz, en-boy oranı panele göre ayarlanamaz,
tıklama koordinatları alt-dikdörtgene çevrilemez, ikinci bir görünüm (üstten/kamera
önizleme) hiç mümkün değil.

### B3 — Veri modeli DÜZ
`SceneDesc`: **256 varlık**, **16 kaynak**, hiyerarşi **yok** (ebeveyn/çocuk alanı yok),
bileşenler bit maskesi + gömülü sabit alanlar (model/anim/ışık/gövde), prefab yok.

Sektörde sahne ağacı birinci sınıftır: dönüşüm kalıtımı, klasörleme, örnekleme
(instancing). Bunlar olmadan 20 varlıktan sonra sahne yönetilemez.

## 3. Sektör referansı — ne alınır, ne alınmaz

| kaynak | alınan | gerekçe |
|---|---|---|
| **Unity** | Dockable düzen + **düzen ön ayarları**, Play-in-editor (durum geri yükleme) | Düzen ön ayarı ("Layout: Default/2 by 3/Tall") ucuz ve etkisi büyük |
| **Godot** | **Sahne ağacı birinci sınıf**: ebeveynlik, örnekleme, "sahne içinde sahne" | Bizim düz `SceneDesc`'e en ucuz oturan model; blob dostu |
| **Unreal** | İçerik tarayıcı + içe aktarma hattı, viewport araç çubuğu (gizmo kipi/snap) | Zaten `engine_texpack`/`engine_sahnec` var; tarayıcıya bağlanacak |
| **Blender** | Kipsizlik (modal olmayan), tutarlı kısayollar | Kipli araçlar öğrenmesi pahalı; bizde kip yok, öyle kalsın |

**Bilinçli ALMIYORUZ:**

* **Çoklu OS penceresi (ImGui viewports).** Mobil öncelikli bir motorda ikinci bir
  swapchain + pencere yönetimi; maliyeti getirisinden büyük. Docking YETER.
* **Düğüm grafiği editörleri** (Blueprint / shader graph). Faz 8'in konusu.
* **Reflection güdümlü inspector.** PLAN §11: derleme zamanı reflection Tulpar alt
  kümesiyle gelecek. O gelene kadar inspector **elle yazılmış ama veri güdümlü**
  (alan tablosu), reflection gelince tablo üretime döner.
* **Editörde script derleme/hot-reload.** Köprü zaten `.sahneb` sıcak yüklemeyi
  yapıyor; oyun kodu Tulpar ve AOT — editöre gömmek ayrı bir iş.

## 4. Fazlar ve KAPILAR

Her fazın kapısı var; kapı yoksa faz bitmiş sayılmaz. Kapılar headless koşar
(`--headless N --out x.ppm`), pencere açılmaz.

### Faz E1 — Kabuk (docking + viewport) ⟵ **bu PR'ın hedefi**

1. **E1.1 ImGui docking dalı.** Yeniden vendor, `imconfig.h` korunur, `imgui_impl_vulkan`
   docking sürümü. Kapı: mevcut `editor_imgui_draws_into_offscreen_pass` hâlâ yeşil.
2. **E1.2 Sahne dokuya.** Editör kendi offscreen hedefine çizer; panel `ImGui::Image`
   ile gösterir. Tıklama/gizmo koordinatları panel dikdörtgenine çevrilir.
   Kapı: panel içindeki bir noktadan `scene_pick` doğru varlığı seçmeli (headless,
   **kontrol**: panel dışındaki tık hiçbir şey seçmemeli).
3. **E1.3 Kabuk düzeni.** Menü çubuğu, araç çubuğu (gizmo kipi/snap/oynat), dockspace,
   durum çubuğu. Düzen `imgui.ini` yerine **bizim** dosyamıza yazılır (belirlenimli).
   Kapı: düzen kaydedilip yüklendiğinde panel dikdörtgenleri **bit bit** aynı.

**E1 durumu (2026-09-17, dal `engine/editor-ui`):** üçü de kapalı, kapılar headless koşuda:
`panel secim kapisi` (panel içi piksel → `vp.map_mouse` → ışın → varlık 0; kontrol: panel dışı
piksel geçersiz), `duzen kapisi` (kaydet→yükle bit-tam; kontrol: SizeRef'i %30 büyütülmüş mutant
dosya farklı; geri yükle bit-tam). Komut tablosu bağlandı (13/13; elle kısayol ve ham GLFW T/R/S
okuması silindi), HiDPI işaretçi ölçeği bağlı (`Window::window_size`).

**E1.4 — Profesyonel yüzey (2026-09-17, üç ajan, dosya sahipliğiyle):** kullanıcının "profesyonel
seviyede görünmüyor" geri bildirimi üzerine. Ölçülen kusurlar: menü çubuğu düğme + debug dökümüydü,
Özellikler ham `DragFloat3` (etiket sağda), Sahne düz liste + debug satırları, Görünüm kaplamasız,
Kaynaklar ham metin. Çözüm dört yeni birim, hepsi `editor_tone`/stil paletinden (ham renk yok):
* `app/editor_chrome.*` — gerçek menüler (komut tablosundan üretilir, kısayol sütunlu), araç çubuğu
  (oynat/durdur, Taşı/Döndür/Ölçekle segmentli, Yakala + adım, Gizmolar, Kaydet/Derle), alt durum
  çubuğu (mesaj + sağdan düşen ölçümler). Üçü `BeginViewportSideBar` ile WorkRect'i daraltır;
  dockspace aralarına oturur (ImGui bir kare gecikmeli uygular — kapı ölçüyor). 9 kapı.
* `app/editor_widgets.*` — özellik ızgarası (etiket solda, X/Y/Z rozetli vec3, renk, combo, kaynak),
  Unity tarzı bileşen başlığı (✕ kaldır) + "Bileşen ekle", hiyerarşi (arama, tür simgesi, bileşen
  rozetleri, ＋/− araç satırı). **`PropItem` sözleşmesi:** bileşik widget'ta ImGui "son öge"si yalnız
  Z alanıdır; Y sürüklenirken `IsItemActivated()` false kalır ve günlüğe işlem düşmezdi — `track_edit`
  artık `PropItem` alır (kapı sentetik sürüklemeyle ölçüyor). 7 kapı.
* `app/editor_overlay.*` — viewport kaplaması (yalnız çizim listesi: Perspektif/kip/istatistik
  rozetleri, sağ üstte derinlik-sıralı eksen gizmosu, kamera rozeti, odak çerçevesi, ipucu) ve
  Kaynaklar paneli (soldan kısaltılan yol, ↻, ızgara/liste, karo boyutu, arama; geniş panelde yan
  yana "Sahnedeki kaynaklar" + kart ızgarası; çift tık ekler). 9 kapı.
* `editor_ui.cpp` tema: dock sekmeleri (aktif sekme pencereyle kaynaşır, accent üst çizgi), gölge
  hacmi ve seçili olmayan ışık kutuları inceltildi/soluklaştırıldı (kullanıcının gördüğü "kocaman
  kırmızı kutu" `lamba_kirmizi`'nin 8 birimlik yarıçap gizmosuydu).
* Sekme etiketleri Türkçe (`Görünüm###Gorunum`): kimlik ASCII kalır, düzen dosyası `###` sonrasını
  yazar. Komut adları da diyakritikli.
* Doğrulama altyapısı: `tests/editor_probe.*` (gerçek font + tema ile offscreen sonda, PPM) +
  `tools/ppm2png.py` — ajanlar kendi çıktısına **baktı**. Toplam editör kapısı 40; `engine_tests`
  431/431. Kare: 1600x900, 8 varlık, p50 19.6 ms (E1 hedefi "<8 ms 1080p boş sahne" henüz kendi
  koşullarında ölçülmedi — açık).

### Faz E2 — Sahne ağacı (veri modeli)
`SceneEntity`'ye `int32_t parent` + **ebeveyn-önce sıralama değişmezi** (blob dostu,
`.sahne` metninde girinti ile okunur). Dönüşüm kalıtımı, sürükle-bırak yeniden
ebeveynleme, klasör varlıkları. Kapasite 256 → ölçülerek artırılır.
Kapı: kalıtımlı dönüşüm ile düz dönüşüm **bit bit** aynı dünya matrisi üretmeli
(kontrol: ebeveyni döndürünce çocuk da dönmeli).

**E2 durumu (2026-09-17):** kapandı. `SceneEntity::parent` (+ `flags`: gizli/kilitli), döngü ve
derinlik (16) doğrulaması satır numaralı hata olarak, `scene_entity_world_matrix` zinciri besteler,
`scene_tree_order` ön-sıra, `scene_reparent` **dünya dönüşümünü koruyarak** yeniden bağlar (sapma
ölçüldü: 4.8e-07), silme çocukları büyükbabaya bağlar ve `parent` indekslerini kaydırır
(undo bit-tam, `SceneOp::child_mask`). Metin formatı geriye uyumlu: `ebeveyn`/`bayrak` yalnız
sıfırdan farklıysa yazılır — `editor.sahne` baytları değişmedi. **Faz E2 kapısı:** hiyerarşik
`.sahneb` ile elle düzleştirilmiş ikizinin blob'u **bayt bayt aynı** (özet `203ed1610265cad9`);
kontrol A ebeveyni oynatınca özet değişiyor, kontrol B naif düzleştirme farklı. Editör tarafı:
Sahne paneli gerçek ağaç (girinti, ▾/▸, sürükle-bırak ebeveynleme, kök bırakma bölgesi, sağ tık
menüsü, F2 yerinde adlandırma, göz/kilit), gizmo dünya uzayında çalışıp `scene_world_to_local_matrix`
ile yerel alanlara yazıyor, gizli varlık çizilmiyor. Editör kapısı: bağla → dünya sınırı yerinde
kalıyor (sapma 0.0000), ebeveyn +5 → çocuk +5.00, **kontrol** bağsız varlık +0.00.

**E1.5 — Gezinme ve kabuk servisleri (2026-09-17, üç ajan):**
* `app/editor_camera.*` — saf `camera_update`: yörünge + **kaydırma** (orta tuş / Shift+sağ tık),
  **RMB+WASD serbest uçuş** (dt-bağımsız; 30 fps ↔ 120 fps farkı 3.5e-05), **F ile odaklanma**,
  eksen görünümleri, **ortografik/perspektif** (orto ışınları paralel). Bu sırada gerçek bir hata
  düzeldi: kamera koşulu `!ui.wants_mouse()` idi, 3B bir panelin içine taşındığından
  `WantCaptureMouse` görüntü üzerindeyken zaten true oluyordu — kamera görüntüde hiç dönmüyordu.
* `app/editor_overlay.*` — eksen göstergesi **tıklanabilir** (yalnız disk alanında öge ekler;
  dışı `ImGui::Image`'e düşer), Perspektif/Yörünge/Gizmo-uzayı çipleri, **kutu (marquee) seçim**
  (kamera arkasındaki kutular eleniyor).
* `app/editor_console.*` — halka tamponlu Konsol paneli (seviye/etiket/kare, süzgeç, tekrar
  toplama, düşen sayacı) ve motorun stdout/stderr'ini `pipe`+`dup2` ile yakalama: Vulkan doğrulama
  iletileri artık editörün içinde. Headless'ta yakalama AÇILMAZ (kapı satırları akmalı).
* `app/editor_files.*` — editör içi dosya seçici (sıralı `dirent`, kırıntı, uzantı süzgeci, üzerine
  yazma şeridi), son dosyalar (bit-tam gidiş-dönüş), kaydedilmemiş değişiklik onayı; Yeni/Aç/
  Farklı kaydet komutları ve Dosya > "Son dosyalar" alt menüsü.
* Ana oturum: **pano** (Ctrl+X/C/V, tek geri-al grubu), **duraklat/kare ilerlet** (F6/F10, F5 oynat),
  `Konsol` düzen tablosunda (6 panel, düzen kalıcılığı bit-tam).

**E1.6 — Yeniden boyutlandırma / tam ekran (2026-09-18, kullanıcı bildirimi):** editör tam ekran
yapılınca içerik "aynı oranda büyüyüp bozuluyordu". Sebep swapchain'in pencereyi hiç takip
etmemesiydi: yeniden kurma tek sinyale bağlıydı (`needs_recreate`, yani `VK_ERROR_OUT_OF_DATE_KHR`)
ve **Wayland'de o sinyal hiç gelmez** — yüzey `currentExtent`i `0xFFFFFFFF` döner, ölçüsü süren
taraf uygulamadır (görünmez pencere sondasıyla ölçüldü: 640x360 → 1600x900, `currentExtent`
iki ölçümde de tanımsız). Swapchain 1280x720'de kalıyor, kompozitör görüntüyü pencereye geriyordu.
Çözüm `rhi/swapchain.hpp`'de: saf `swapchain_resize_action` (istenen ölçü ≠ pencere ölçüsü →
yeniden kur; OUT_OF_DATE *ek* sebep; 0 ölçü hiçbir şey) + `Swapchain::sync_size`, **karenin
başında** (kayıttan önce) çağrılır — böylece ImGui `DisplaySize`'ı ile hedefin ölçüsü aynı karede
ayrışmaz; editör, demo ve köprü döngüleri artık tek karar noktasından geçiyor. Editöre
**Görünüm > Tam ekran (F11)** komutu eklendi (`platform::Window::set_fullscreen`; host yeteneği
yoksa menü ögesi soluk). Kapı: `rhi_resize_follows_window_size_not_only_out_of_date` (senaryo
tablosu; pozitif kontrol olarak ESKİ kural, tam ekran senaryosunu kaçırdığı **ölçülür**).
Görsel doğrulama kullanıcıda (pencere açma kuralı). Ayrıntı: [[Tuzaklar]] 8bw.

### Faz E3 — Inspector + içerik
Bileşen ekle/çıkar, alan tablosu güdümlü özellik editörü, her özellikte "geri döndür",
kaynak içe aktarma (glTF/PNG → `engine_texpack`/`sahnec`), malzeme düzenleme.
Kapı: bir alanı değiştir → geri al → `SceneDesc` **eşitlik** ile ilk hâline dönmeli.

### Faz E4 — İş akışı
Editörde oynat/duraklat/kare-ilerlet + **durum geri yükleme** (oynatmadan çıkınca sahne
bozulmaz), kamera (yörünge/uçuş/odaklan), arama-filtre, çoklu görünüm.
Kapı: oynat → 300 tick → durdur ⇒ `SceneDesc` oynatma öncesiyle **eşit**.

### Faz E5 — Cila
Tema, kısayol tablosu, düzen ön ayarları, çökme kurtarma (otomatik kaydetme).

## 5. Ölçülebilir hedefler (kapı sayıları)

| ölçü | bugün | E1 sonu | E4 sonu |
|---|---:|---:|---:|
| panel düzeni kullanıcıca değiştirilebilir | hayır | **evet** | evet |
| sahne bir panelin içinde | hayır | **evet** | evet |
| hiyerarşi derinliği | 0 | 0 | **sınırsız** |
| editör kare süresi (1080p, boş sahne) | ölçülmedi | **< 8 ms** | < 8 ms |
| oynat/durdur sonrası sahne bütünlüğü | yok | yok | **bit bit eşit** |

## 6. Çalışma kuralları

* Pencere AÇILMAZ; doğrulama `--headless N --out x.ppm` + kapılar.
* Her kapının **kontrolü** olacak (negatif durum da ölçülecek).
* `SceneDesc` değişirse `.sahne` yazıcı/okuyucu, `.sahneb` blob, `scene_runtime`,
  köprü ve altın testler **aynı değişiklikte** güncellenir.
* Editör motoru **kütüphane olarak** kullanır, tersi değil (PLAN §278).

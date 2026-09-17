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

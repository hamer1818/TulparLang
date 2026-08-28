---
tags: [moc, editor, 3d, games]
---

# TameEngine — Sahne Editörü

`examples/scene3d_editor.tpr` → `./tulpar build examples/scene3d_editor.tpr TameEngine`
→ **`./TameEngine [sahne.json]`**. Motorun İÇİNDE, saf Tulpar ile yazılı
([[Scene3D]]'in bir parçası, `lib/scene3d.tpr`); ayrı bir uygulama değil.

> **Editör asıl uygulama, oyun onun içinde koşan bir ÖNİZLEME.** Bu cümle
> birçok davranışın gerekçesi: OYNAT sahnenin KOPYASINI koşturur, DUR kopyayı
> atar, oyun-bitti ekranında "Çıkış" programı kapatmaz — düzenlemeye döner.

## Mimari
- **Anlık-kip (immediate mode) GUI**: durum yok, her kare yeniden çizilir,
  tıklama aynı çağrıda döner. Widget kimlikleri ÇAĞIRANIN verdiği tam sayılar
  → **çakışan iki kimlikten ikincisi ÖLÜ kalır** (bkz. kimlik blokları).
- **Sahne görünümü GERÇEK motor**: sahne bir render texture'a çizilip panele
  yerleştiriliyor. Doğrudan çizip kırpmak yetmez — kamera izdüşümü pencerenin
  en-boy oranını kullanır, görüntü ezik ve merkezi kaymış çıkar.
- **Kaynak doğruluk sahne JSON'unda**: editörün ürettiği her şey
  `scene_json3d()` / `scene_json_levels3d()` ile yazılır, `scene_load3d()` ile
  okunur. Editör ayrı bir veri modeli tutmuyor.

## Dock — panellerin yeri
Üç panel × dört yuva: `PANEL_HIER3` / `PANEL_INSP3` / `PANEL_CONS3` ×
`DOCK_LEFT3` / `DOCK_RIGHT3` / `DOCK_BOTTOM3` / `DOCK_OFF3`.

- Panel **kendi yerini hesaplamıyor**: dikdörtgenini `_dk_rect3(p, W, H)`'ten
  alıyor (dörtlü global ile döner — Tulpar'da çoklu dönüş yok).
- **Boş yuva yer kaplamıyor** → paneli kapatmak alanını sahne görünümüne
  bırakıyor; üçünü de kapatmak görünümü tam ekran yapıyor.
- Aynı yuvadaki paneller yuvayı **eşit paylaşıyor** (yan yuvalarda alt alta,
  alt yuvada yan yana). **Son panel artan pikselleri alıyor** — tam bölünmeyen
  ölçüde kalan boşluk göze çarpıyor.
- Alt yuva **tam genişlik**, yan yuvalar araç çubuğundan alt yuvaya kadar.

### Jestler
| Jest | Ne yapar |
|---|---|
| Panel **başlığından** sürükle | yuvayı değiştirir; hedef bölge maviyle vurgulanır |
| Başlığın sağ ucundaki **x** | paneli kapatır |
| Panel **sınırını** sürükle | yuvanın ölçüsünü değiştirir |
| Menüde **PANELLER** | listeden aynı işler + varsayılana dönüş |

Bırakma bölgeleri ekran geometrisinden: sol üçte bir SOL, sağ üçte bir SAĞ,
alt çeyrek ALT. **Ortası geçerli bir yer DEĞİL** (bırakmak iptal eder) —
kazayla başlayan bir sürükleme paneli rastgele bir yere atmamalı.

> **Sıra taşıyıcı.** Ayraç (±4 px) başlıktan ÖNCE sınanıyor: daha DAR hedef
> önce kazanmalı, yoksa sınırı yakalamak imkânsız olur. Alt bant sol/sağ
> üçte birlerden ÖNCE sınanıyor: alt yuva tam genişlik olduğu için sol alt
> köşe yoksa "sol" olurdu.

**Kapalı panel kendini geri açamaz** (başlığı çizilmiyor) → PANELLER listesi
bu yüzden duruyor, süslü değil zorunlu.

## Yerleşimin kalıcılığı
`save_data("tameengine_layout.txt", "ölçek|solW|sağW|altH|yuva0|yuva1|yuva2")`.
Bırakınca yazılıyor (her karede değil, yalnız çıkışta da değil).

- Kayıt adı **değiştirilebilir** (`editor_layout_key3d`) — sabit ad, testlerin
  kullanıcının kendi yerleşimini ezmesi demekti.
- **Eski (dört alanlı, dock öncesi) kayıtlar hâlâ okunuyor**: alan sayısına
  bakılıyor, sürüm numarası yok. Geçersiz yuva değeri kaydın TAMAMINI değil
  yalnız yuvaları düşürüyor.
- Ölçek değişimi **özelleştirmeyi oranlayarak taşıyor**, varsayılana atmıyor.

## Dosya işleri
| Ne | Nasıl |
|---|---|
| Aç | menüde **Aç** → diskteki `.json`'ları listeleyen pencere |
| Farklı kaydet | **F. Kaydet** (CTRL+SHIFT+S) — şablonu alıp kendi oyununa çevirmenin yolu |
| Geri yükle | listede AÇIK dosyaya tıklamak |
| Kurtarma | kapatırken kaydedilmemiş iş `<dosya>.kurtarma.json`'a yazılır |

**Geri-al geçmişi dosya DEĞİŞİNCE sıfırlanıyor.** Sıfırlanmasa CTRL+Z önceki
dosyanın hâlini getirir, CTRL+S onu yeni dosyaya YAZARDI. Aynı dosyayı
yeniden açmak (geri yükleme) geçmişi KORUYOR — orada geçmiş bir güvenlik ağı.
Tek kural iki davranışı da veriyor.

**Kurtarma neden düğme, neden sessiz değil:** raylib'de kapanışı iptal edecek
bağlama yok (`WindowShouldClose()` bir kez true dönünce geri alınamıyor), yani
"emin misin?" sorulamıyor. Sessizce geri yüklemek de yanlış — kullanıcı
kaydetmemeyi bilerek seçmiş olabilir. Kurtarma **geri alınabilir** ve
kurtarılan iş **hâlâ kaydedilmemiş** sayılıyor (yıldız yanmaya devam eder).

## Paneller
- **HIYERARSI** — bölüm şeridi, ekleme düğmeleri, ada/etikete göre süzme,
  DUNYA satırı (Godot'nun WorldEnvironment'ı gibi), varlıklar, bölgeler.
- **OZELLIKLER** — seçime göre: varlık (dönüşüm/görünüm/fizik/davranışlar),
  DUNYA (denetim, gökyüzü, dünya, **kamera**, ışıklar, su, arazi, gündüz-gece,
  eğim, kurallar) ya da bölge.
- **KONSOL** — motorun tanılama kaydı.

## Kamera paneli
Mod (takip / yörünge / 1. şahıs), **hedef** (sırayla gezen düğme; "hedefsiz"
de döngüde), uzaklık/yükseklik/fov, kamera-engel.

> ⚠️ **`CAM_FIXED` "sabit kamera" DEĞİL** — motorun ilk günkü TAKİP kamerası
> (açı sabit, hedefi izler). Biçimdeki karşılığı `"follow"` ve doğrusu bu.
> Hiçbir şeyi izlemeyen kamera = hedefi olmayan kamera, ayrı bir mod değil.

Hedefe editör **AD veriyor**: biçim varlıklara adla atıfta bulunuyor, adsız
bir hedef kaydedince sessizce koparadı.

Yörünge yarıçapı ↔ yatay mesafe ters dönüşümü **tek fonksiyonda**
(`_cam_dist_logical3`); serileştirici de panel de onu çağırıyor. İki yerde
ayrı yazılınca kamera her kaydet/yükle turunda bir adım geri kayıyordu
(16 → 18.87 → 21.35).

## Bölgeler
Sağ tık → "+ bolge". **GIRINCE eylemi** var: kazan, kaybet, gireni yok et
(çukur/lav), hasar ver, skor ekle, sonraki bölüm. Artı **ses**.

Eylem listesi çarpışma kurallarınınkinden **AYRI ve bilerek**: kural eylemleri
bir ÇİFT üzerinden konuşuyor ("ötekini öldür"), bölgede öteki yok — giren
gövde var. `ACT_*` kullanmak yarısı sessiz boş işlem olan bir liste üretirdi.

Editörde bölgeler **çiziliyor** (oyunda değil) ve **rengi durumunu söylüyor**:
turuncu = eylemsiz (denetimin "hiçbir şey yapmıyor" dediği), gri = kapalı,
yeşil = iş gören. Tıklanabiliyorlar; öncelik ENTITY'de (bölge genellikle
içinde cisimler barındıran büyük bir kutu).

## Kısayollar
Tam liste editörün İÇİNDE: **H** ya da menüdeki **?**. Liste 24+ satır olduğu
için **sütun sayısı genişlikten türüyor** (2x'te iki sütun, 3x'te tek sütun +
kaydırma) — bir başvuru listesini kaydırtmak onu okumaktan çıkarıyor.

Durum çubuğu artık kısayol SAYMIYOR ("H = kisayollar" diyor): o satır
kırpılıyor, dar pencerede hiç çizilmiyor ve her mesajda yerini kaybediyordu.

> 1/2/3/4 sırası Q/W/E/R ile **AYNI DEĞİL**: E=döndür/R=ölçek ama
> 3=ölçek/4=döndür. Liste ikisini ayrı ayrı ve doğru yazıyor.

## Widget kimlik blokları
Çakışan iki kimlikten **ikincisi ölü kalır** — sessiz hata. Bloklar KAYNAKTAN
okunup ayrıklığı sınanıyor (sabitleri teste kopyalamak bir çakışmayı
gizlemişti).

| Blok | Aralık | Not |
|---|---|---|
| Sabit arayüz | 9001–9611 | araç çubuğu, hiyerarşi düğmeleri, dünya paneli |
| Kamera | 9780+8 | `_ed_cam_idbase3` |
| Hiyerarşi listesi | 11990–13000+ | satır başına 1 |
| Bağlam menüsü | 22001–22016 | |
| Bölge paneli | 23001–23043 | |
| Davranış blokları | 30000, adım 12 | `_ed_bh_idbase3` |
| Model tarayıcısı | 50000–50052 | |
| Ses tarayıcısı | 51000–51051 | **paylaşılan**: kural VE bölge |
| Doku tarayıcısı | 52000–52051, +52900 | |
| Işıklar | 53000, adım 10 | `_ed_lt_idbase3` |
| Kurallar | 60000, adım 10 | `_ed_rl_idbase3` |
| Dosya penceresi | 70000+ | `_ed_fd_idbase3` |
| Kısayol örtüsü | 71000+ | `_ed_help_idbase3` |
| PANELLER örtüsü | 72000+ | `_ed_dock_idbase3` |

Ses tarayıcısının paylaşımı güvenli çünkü kural paneli DUNYA sekmesinde,
bölge paneli BÖLGE sekmesinde çiziliyor — ikisi aynı karede asla çizilmiyor.

## Veri kaybı ailesi — en çok hata çıkan yer
Bulunan ve düzeltilenler (hepsi **sessizdi**):

| Hata | Belirti |
|---|---|
| "Yeni" sahne eski bölümleri bırakıyordu | boş istenen dosya, terk edilen sahnenin varlıklarıyla doluyordu |
| Geri alma çok bölümlü sahneyi yok ediyordu | tek CTRL+Z bölüm sayısını 2'den 0'a düşürüyordu (kendi düzeltmemin regresyonu) |
| Anlık görüntü hangi bölüme ait olduğunu bilmiyordu | 1. bölümde işaret, 2. bölümde CTRL+Z → iki bölüm birden bozuluyordu |
| Oynat-Dur düzenlenen bölümün işini siliyordu | "BOLUM 2" yazıyor, ekranda 1. bölümün içeriği duruyordu |
| Çok bölümlü sahnede kamera hiç çalışmıyordu | hedef her yerde -1: yükleme sırası + bölüm değişimi |
| Oyuncuyu silmek kamerayı ölü handle'a bağlıyordu | yerine yeni oyuncu eklemek onarmıyordu |
| Modeller süreç-içi handle olarak yazılıyordu | başka çalıştırmada model kayboluyordu |
| Ground texture ve `anim_fps` hiç yazılmıyordu | kaydet/aç sonrası sessizce sıfırlanıyordu |

**Ortak ders:** `_ed_apply_json3` neyi koruyup neyi atacağını ÇAĞIRANDAN
öğreniyor (`yapisal` parametresi), JSON'a bakıp tahmin etmiyor. Geri alma
sahne İÇİNDE gezinmektir; Dur ve dosya açmak belge DEĞİŞTİRMEKTİR.

## İlgili
[[Scene3D]] · [[Tame]] · [[Testing]] · [[Tuzaklar]] · [[Decisions]] · [[Roadmap]]

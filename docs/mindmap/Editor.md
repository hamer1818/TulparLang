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

## Tutamaklar — taşı/ölçek OK, döndür HALKA
TAŞI ve ÖLÇEK kiplerinde üç eksen oku; matematiği saf (ışın ile eksen doğrusu
arasındaki en yakın yaklaşım — ekran uzayında piksel mesafesine bakmak, tepeden
bakınca dikey oku bir noktaya çökertirdi).

DÖNDÜRME 2026-08-28'e kadar **klavyeye mahkûmdu**: ok tuşları, 15°'lik adımlar,
yani 37°'ye dönmenin yolu yoktu. Artık **halka**:
- Motorun tek dönme ekseni Y (`set3yaw`) → **tek halka**. Üç halka çizmek,
  ikisi hiçbir şey yapmayan tutamak demekti.
- Tutunma: ışın ↔ cismin yüksekliğindeki **yatay düzlem**; kesişim merkeze
  `r` uzaklıktaysa yakalandı. Sürüklerken tutunma **sınanmıyor**, yalnız açı
  okunuyor — imleç halkadan çıkınca dönüşün kopması hiçbir editörde yok.
- **Yarıçap cismin dışında** kalmak zorunda: kocaman bir duvarın içindeki
  halkaya tıklamak duvarı seçer.
- Halkadaki çizgi cismin **baktığı yön** — onsuz simetrik bir daireden kaç
  derece döndüğü anlaşılmıyor.
- **Izgara açıkken 15°'ye oturuyor**, ok tuşunun adımıyla aynı: iki yol aynı
  açı kümesini üretmeli.
- **Çoklu seçim grup merkezi etrafında** dönüyor (yaw + yörünge). Yalnız
  yaw'ı döndürmek kutuları yerinde çevirir, dizilişi bırakırdı.
- **Ölçek de seçimin tamamına** gidiyor ve uygulanan şey MUTLAK uzunluk
  (tutamak dünya birimi veriyor), oran değil. Konumlar kıpırdamıyor:
  "şu beşini genişlet" ≠ "beşini birbirinden ayır". Döndürmede yörünge
  var, çünkü orada dizilişin dönmesi jestin kendisi.
- Dönüş **yakalanan hâlden** hesaplanıyor, bir öncekinden değil: üstüne
  eklemek yuvarlamayı biriktirir ve sürükleme uzadıkça cisim kaçardı.

> ⚠️ **İKİ YAW SÖZLEŞMESİ var** (bkz. [[Scene3D]]): gövde `(sin, cos)`,
> kamera `(sin, −cos)`. Halka **gövde** sözleşmesini kullanıyor ve testi
> beklentiyi `patrol3d`'den — bağımsız bir kod yolundan — alıyor.

## Menü şeridi — açılır menüler
Şerit düz bir düğme sırasıydı (`Yeni / Ac / Kaydet / F. Kaydet / Geri / Ileri
/ PANELLER / ?`). İki somut derdi vardı:
- **Şerit doluyordu.** Her yeni komut bir düğme demek ve sığmayan düğme hiç
  çizilmiyordu — yani komut dar pencerede **erişilemez** oluyordu.
- **Kısayol hiçbir yerde yazmıyordu**; CTRL+D'yi öğrenmenin tek yolu H
  listesiydi.

Şimdi dört başlık: **Dosya / Duzen / Gorunum / Yardim**. Başlık sayısı sabit,
yani komut eklemek şeridi büyütmüyor.

- Öğeler **konumla değil KODLA** anılıyor (`MNA_*`). Bir öğe duruma göre
  gizlenebiliyor (web'de "Indir"); dağıtıcı indekse baksaydı gizlenen öğe
  ötekilerin ne yaptığını **kaydırırdı**.
- **Kısayol her satırın sağında.** Menüde yazan kısayolun gerçekten bağlı
  olduğunu bir test **tuş işleyicisinin kaynağından** doğruluyor — testin
  içine kısayol kopyalamak, menünün iddiasını kendisiyle doğrulamak olurdu.
  ("Yeni"/"Ac"ın kısayolu **yok** ve bu yüzden yazmıyor.)
- **Soluk ≠ gizli.** Geçici olarak iş görmeyen komut (boş panoda "Yapistir")
  duruyor ve soluk çiziliyor; var oluşu duruma bağlı olan (web'in "Indir"i)
  hiç konmuyor. Kaybolan öğe komutun varlığını da unutturur.
- Açık menü varken **başka başlığın üstüne gelmek ona geçiyor**; ESC kapatıyor.
- **"Yeni" iki tık:** ilki öğeyi "EMIN MISIN?"e çeviriyor ve **menüyü açık
  tutuyor** (ikinci tık aynı yerde olsun diye), ikincisi siliyor.
- Aç/kapa öğeleri menüyü **açık bırakıyor** (üç paneli tek açılışta
  kapatabilmek için), komutlar kapatıyor — kararı `_ed_mn_do3`'ün dönüşü veriyor.
- **KURTAR menüde değil**, şeritte ve vurgulu: o duran bir komut değil geçici
  bir *teklif*, ve menüde saklanan teklif görülmez.
- Kapalı panel **kendi varsayılan yuvasına** geri açılıyor (`_dk_def_slot3`) —
  hepsini sola atmak, konsolu geri açana onu bir daha taşıtırdı.

> **`text_width()` penceresiz 0 döner**, yani yalnız ona bakan bir genişlik
> hesabı testte "her şey sığıyor" der. Menü genişliği `_g_wmax3` ile
> **gerçek ölçü ile karakter tahmininin büyüğünden** geliyor.

## Dock — panellerin yeri
Üç panel × dört yuva: `PANEL_HIER3` / `PANEL_INSP3` / `PANEL_CONS3` ×
`DOCK_LEFT3` / `DOCK_RIGHT3` / `DOCK_BOTTOM3` / `DOCK_OFF3`.

- Panel **kendi yerini hesaplamıyor**: dikdörtgenini `_dk_rect3(p, W, H)`'ten
  alıyor (dörtlü global ile döner — Tulpar'da çoklu dönüş yok).
- **Boş yuva yer kaplamıyor** → paneli kapatmak alanını sahne görünümüne
  bırakıyor; üçünü de kapatmak görünümü tam ekran yapıyor.
- Aynı yuvadaki paneller yuvayı **paylaşıyor** (yan yuvalarda alt alta, alt
  yuvada yan yana) ve **aralarındaki sınır çekilebiliyor**. **Son panel artan
  pikselleri alıyor** — tam bölünmeyen ölçüde kalan boşluk göze çarpıyor.
- Alt yuva **tam genişlik**, yan yuvalar araç çubuğundan alt yuvaya kadar.

### Jestler
| Jest | Ne yapar |
|---|---|
| Panel **başlığından** sürükle | yuvayı değiştirir; hedef bölge maviyle vurgulanır |
| Başlığın sağ ucundaki **x** | paneli kapatır |
| Yuvanın **dış sınırını** sürükle | yuvanın ölçüsünü değiştirir |
| İki panelin **arasını** sürükle | o yuvadaki **paylaşımı** değiştirir |
| Menüde **PANELLER** | listeden aynı işler + varsayılana dönüş |

Bırakma bölgeleri ekran geometrisinden: sol üçte bir SOL, sağ üçte bir SAĞ,
alt çeyrek ALT. **Ortası geçerli bir yer DEĞİL** (bırakmak iptal eder) —
kazayla başlayan bir sürükleme paneli rastgele bir yere atmamalı.

> **Sıra taşıyıcı.** Sınırlar (±4 px) başlıktan ÖNCE sınanıyor: daha DAR
> hedef önce kazanmalı, yoksa sınırı yakalamak imkânsız olur. Yuvanın DIŞ
> sınırı yuva İÇİ sınırdan önce: ikisi yuvanın köşesinde çakışıyor ve dış
> sınır kullanıcının önceden öğrendiği jest. Alt bant sol/sağ üçte
> birlerden ÖNCE sınanıyor: alt yuva tam genişlik olduğu için sol alt köşe
> yoksa "sol" olurdu.
>
> Sıra hem SAF bir seçici fonksiyonda (`_dk_div_pick3`) hem de çağrı
> sırasında yaşıyor; ikincisi **kaynağı okuyan** bir testle bağlı, çünkü
> fare gerektiren bir çağrıyı penceresiz süremiyoruz.

### Yuva içi paylaşım
Her panelin bir **PAYI** var (tam sayı, varsayılan 1000); yuvadaki yer paylara
göre bölünüyor. Pay **orandır, piksel değil** — pencere büyüyünce paneller
birlikte büyüyor.

- Sınırı çekmek yalnız **o çiftin** paylarını değiştiriyor, toplamları sabit:
  üç panelli bir yuvada ortadaki sınır üçüncüyü oynatsaydı kullanıcı
  düzelttiğini sandığı yerleşimi bozardı.
- **Piksel kelepçesi tek başına yetmiyordu:** pay→piksel çevrimi aşağı
  kırpıyor, tam en küçük ölçüye çekilen panel bir piksel eksik çıkıyordu
  (ölçüldü: 60 istenirken 59). Payın kendisinin de bir tabanı var.
- Yuvaya **yeni gelen** panel hedefin **ortalamasını** alıyor — kendi eski
  payını taşısaydı bir yuvada kıymık olan panel yeni yuvasına da kıymık
  düşerdi, oysa kullanıcı onu oraya GÖRMEK için taşıdı. Kural tek çoktan
  geçilen noktada (`_dk_set_slot3`), çağıranlara dağıtılmış değil.
- Tam sayı tutuluyor çünkü kayıt biçimi metin ve `toString(1.0)` bu dilde
  `"1e+00"` yazıyor: ondalık pay her kaydet/aç turunda okunamaz hâle gelirdi.

**Kapalı panel kendini geri açamaz** (başlığı çizilmiyor) → PANELLER listesi
bu yüzden duruyor, süslü değil zorunlu.

## Yerleşimin kalıcılığı
`save_data("tameengine_layout.txt",
"ölçek|solW|sağW|altH|yuva0|yuva1|yuva2|pay0|pay1|pay2")`.
Bırakınca yazılıyor (her karede değil, yalnız çıkışta da değil).

- Kayıt adı **değiştirilebilir** (`editor_layout_key3d`) — sabit ad, testlerin
  kullanıcının kendi yerleşimini ezmesi demekti.
- **Eski kayıtlar hâlâ okunuyor** (dört alanlı dock öncesi, yedi alanlı
  paysız): alan sayısına bakılıyor, sürüm numarası yok. Geçersiz yuva ya da
  pay değeri kaydın TAMAMINI değil yalnız o alan ailesini düşürüyor.
- **Paylar yuvalardan SONRA okunuyor**: yuva değişimi payı hedefin
  ortalamasına çekiyor, yani önce yazılan pay o kuralla silinirdi.
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
- **KONSOL** — motorun tanılama kaydı; **kaydırılıyor, süzülüyor,
  temizleniyor** (aşağıda).

## Konsol
Panel uzun süre "son N satır" demekten ibaretti ve bir konsolun yapması
gereken üç şeyi yapmıyordu: geriye bakmak, süzmek, temizlemek.

- **Kaydırma DİPTEN sayılıyor** (`_ed_cons_scroll3`, 0 = takip). Tepeden
  saysaydık yeni satır geldikçe aynı sayı başka bir yeri gösterirdi ve takip
  eden konsol kendi kendine kayardı. Dipteyken yeni satır görünür, yukarıda
  kaldıysan aynı satırda kalırsın — **tek kural, iki davranış**.
- **Yukarıdayken bunu SÖYLÜYOR** ("12 satir asagida"): kaydırılmış bir konsol
  yeni satırları göstermiyor ve bunu bilmeyen "kayıt durdu" sanıyor.
- **Sayaçlar süzgeçten BAĞIMSIZ**: "üç hata var ama süzgeci kapatmışsın"
  görünür olmalı, yoksa kullanıcı gizlediği şeyi aramaya devam eder.
- **İZ ile BİLGİ tek süzgeçte** — ikisi de gürültü kefesinde; ayrı bir düğme
  kimsenin istemediği bir ayrım için yer harcardı.
- **TEMIZLE sahneye dokunmuyor.** Tampon sahnenin değil OTURUMUN durumu.
- Etiketler kısa (`x 3` / `! 12` / `i 40`) çünkü konsol yan yuvaya
  taşınabiliyor; TEMIZLE'nin genişliği metninden türüyor (`_g_wmax3`), sabit
  bir yüzde üç haneli sayaçta taşıyordu.

> ⚠️ **Halka tamponda dizi sırası kronolojik sıra DEĞİL.** `_lg_push`
> dolduktan sonra en eskinin üstüne yazıyor. Panel diziyi ham sırayla
> okuyordu: 240. satırdan sonra kayıt karışık görünüyordu, hiçbir hata
> vermeden. Sıra artık tek yerde: `_lg_idx3` (`log_dump3d` de onu kullanıyor).

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
| Menü başlıkları | 9600–9603 | eski düğme sırasından kalan blokta |
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
| Menü öğeleri | 73000+ | `_ed_mn_idbase3`, satır başına 1 |
| Konsol çubuğu | 74000–74003 | `_ed_cons_idbase3` |

Ses tarayıcısının paylaşımı güvenli çünkü kural paneli DUNYA sekmesinde,
bölge paneli BÖLGE sekmesinde çiziliyor — ikisi aynı karede asla çizilmiyor.

## Veri kaybı ailesi — en çok hata çıkan yer
Bulunan ve düzeltilenler (hepsi **sessizdi**):

| Hata | Belirti |
|---|---|
| "Yeni" sahne eski bölümleri bırakıyordu | boş istenen dosya, terk edilen sahnenin varlıklarıyla doluyordu |
| Geri alma çok bölümlü sahneyi yok ediyordu | tek CTRL+Z bölüm sayısını 2'den 0'a düşürüyordu (kendi düzeltmemin regresyonu) |
| Anlık görüntü hangi bölüme ait olduğunu bilmiyordu | 1. bölümde işaret, 2. bölümde CTRL+Z → iki bölüm birden bozuluyordu |
| Yineleme yapısal işi geri getirmiyordu, üstüne sahneyi siliyordu | "bölüm ekle → CTRL+Z → CTRL+Y": bölüm gelmiyor, 1. bölümün içeriği de gidiyordu |
| Oynat-Dur düzenlenen bölümün işini siliyordu | "BOLUM 2" yazıyor, ekranda 1. bölümün içeriği duruyordu |
| Çok bölümlü sahnede kamera hiç çalışmıyordu | hedef her yerde -1: yükleme sırası + bölüm değişimi |
| Oyuncuyu silmek kamerayı ölü handle'a bağlıyordu | yerine yeni oyuncu eklemek onarmıyordu |
| Modeller süreç-içi handle olarak yazılıyordu | başka çalıştırmada model kayboluyordu |
| Ground texture ve `anim_fps` hiç yazılmıyordu | kaydet/aç sonrası sessizce sıfırlanıyordu |

**Ortak ders:** `_ed_apply_json3` neyi koruyup neyi atacağını ÇAĞIRANDAN
öğreniyor (`yapisal` parametresi), JSON'a bakıp tahmin etmiyor. Geri alma
sahne İÇİNDE gezinmektir; Dur ve dosya açmak belge DEĞİŞTİRMEKTİR.

## Geri al / yinele — takas simetrisi
Yığın anlık görüntü tabanlı ve görüntüler **iki biçimde** olabiliyor: tek
bölümün içeriği (`scene_json3d`) ya da tam belge (`scene_json_levels3d`).
Biçimi etiket taşıyor — **işareti biçimi, büyüklüğü bölümü** söylüyor
(`_ed_snap_tag3` / `_ed_snap_lvl3`; negatif = yapısal).

> ⚠️ **Geri alma ile yineleme durum TAKAS eder: biri tam belgeyse öteki de
> tam belge olmalı.** `ed_undo3d`/`ed_redo3d` yığına bıraktıkları KARŞI
> durumu her zaman tek bölüm olarak saklıyordu. Ölçülen sonuç:
> "bölüm ekle → CTRL+Z → CTRL+Y" yineleme yığınına o an açık olan BOŞ
> bölümün içeriğini koyuyor, yineleme o boşluğu geri gelen sahnenin ÜSTÜNE
> yazıyor ve bölüm de geri gelmiyordu — **yinelemenin kendisi sahneyi
> siliyordu**, hiçbir hata vermeden. Kural artık tek yerde: karşı görüntünün
> biçimini, uygulanacak görüntünün etiketi seçiyor (`_ed_karsi_bicim3`).

İki yan kural aynı yerden çıkıyor:
- **Tam belge yakalamadan önce yaşayan bölüm diziye yazılıyor**
  (`_ed_snap_take3` → `_ed_level_store3`); yoksa görüntü o an ekranda olan
  işi hiç görmez ve yineleme götürdüğünden AZINI geri getirir.
- **Yapısal etiket bölümü de taşıyor**; taşımasaydı 2. bölümde "+ BOLUM EKLE"
  deyip geri alan kullanıcı 1. bölüme düşer, çalıştığı yeri kaybederdi.

Bu ailenin ilk hatası (`_ed_capture3`'ün ikinci kez tanımlanması) **aynı adlı
iki fonksiyon** tuzağıydı — sessizce derleniyor, biri ölüyor. Süitteki
`t_no_duplicate_function_names` adı vererek yakaladı; bkz. [[Tuzaklar]].

## İlgili
[[Scene3D]] · [[Tame]] · [[Testing]] · [[Tuzaklar]] · [[Decisions]] · [[Roadmap]]

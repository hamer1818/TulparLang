---
tags: [moc, games, 3d]
---

# Scene3D — 3B Oyun Motoru

`import "scene3d"` — [[Tame]]'in `tm3_*` bağlamaları üzerine kurulu, **saf Tulpar**
preset motoru (`lib/scene3d.tpr`, **~14 500 satır**). C tarafı yok: düzenlemek için
`./build.sh clean` yeterli (gömülü lib yeniden üretilir).

[[Arcade]]'in 3B kardeşi ama ondan **bağımsız**: entity deposu, çarpışma çözümü
ve kamera sistemi ayrı.

> Dosyanın **yarısından fazlası artık SAHNE EDİTÖRÜ** (`_ed_*`, `_g_*`, `_dk_*`
> — 300+ fonksiyon). Ayrı not: [[Editor]]. Motor tarafını ararken `_s3_*`,
> `_e3`, `_cam_*`, `_trg_*`, `_rl3`, `_bh3` öneklerine bak.

## Entity modeli
- `struct Ent3` dizisi (`_e3`) — arcade'in paralel dizilerinin aksine **struct-in-array**.
- **Handle = generation-etiketli**: `_e3gen[slot] * 1048576 + slot`, `_slot_of3()` ile çözülür.
  Ham index'le dizi indeksleme YASAK; bayat handle tespiti buna dayanıyor.
- Slot'lar geri kullanılır → `length(_e3)` yanıltır, canlı sayı için `alive_count3d()`.

## Çarpışma
Kare akışı: `_s3_physics(dt)` → `_s3_collision()` → `_s3_triggers()` → bölüm geçişi.

`_s3_collision()` sırası **taşıyıcı** (bozulursa oyuncu duvara gömülür):
1. `_s3_resolve_walls3(n)` — hareketli gövde ↔ statik duvar (MTV)
2. hareketli ↔ hareketli (ayırma **yarı yarıya** paylaşılır)
3. **`_s3_resolve_walls3(n)` TEKRAR** — duvarlar son sözü söyler
4. duvara giren mermi ölür
5. kayıtlı `carpisinca3` kancaları

> **Neden 3. adım var:** 2. adım oyuncuyu duvara doğru yarım adım geri itiyordu ve
> o kare bir daha düzeltilmiyordu. Ölçüldü: 4 düşmanda 0.15, 8 düşmanda 0.30 birim
> **kalıcı batma**. Oyuncu duvarın içinde durunca kamera ışını geometrinin içinden
> başlıyor → kamera bozulması, duvarın öbür tarafını görme, ulaşılmaz yerlere çıkma.
> Oyunlarda tanıdık bir sınıf; sebep genelde kamera değil, **gövdenin gömülmesi**.

Şekil farkındalığı: küre-küre, küre-kutu, silindir (dikey kapsül), **dönük kutu SAT**
(`_obb_overlap3`, 4 eksen). Geniş faz: kapsayan küre elemesi (`_bp_far3`), 200 entity'de
15.4 → 1.12 ms. Asimptot hâlâ O(n²) → [[Roadmap]].

**Tırmanma yok:** engele yürümek üstüne çıkarmaz (0.4/1.0/3.0 yükseklikte ölçüldü),
zıplamak gerekir. MTV en az batma eksenini seçtiği için bu kolayca bozulabilir → test altında.

## Kamera
Üç mod: `CAM_FIXED` / `CAM_ORBIT` / `CAM_FPS`. Yörünge kamerası engelle üç aşamalı başa çıkar:

1. **Röntgen (xray)** — kamerayla oyuncu arasına giren cisim **saydam** çizilir
   (`_s3_mark_fade3`, varsayılan AÇIK).
2. **Yükselt** — engelin üstünden bak, gerektiği kadar (üst sınır **45°**,
   yumuşatılmış). Yükselmek yakınlaşmaya YEĞLENİR: oyuncu ekranda aynı boyda
   kalır. Sınır bir ara 74°'ydi ve kamerayı kuş bakışına atlatıyordu.
3. **Yakınlaştır** — son çare. Taban `_cam_near3 = 5.0` **dünya birimi**
   (2.2'de 45° FOV'da gövde ekranı taşırıyordu; daha öncesi ORAN'dı — %12,
   yani 16'lık yörüngede 1.9 birim).

> **Öncelik sırası taşıyıcı.** Bir ara kamera ÖNCE içeri çekiliyordu; çekilince
> arada cisim kalmadığı için saydamlaştıracak bir şey de kalmıyordu — yani
> saydamlık tam ihtiyaç anında kendini devre dışı bırakıyordu.

Fare bakışı yörünge modunda **varsayılan açık** (imleç kilidi). Menü/duraklat
açılınca döngü imleci serbest bırakıyor, oyuna dönünce kilitliyor.

> **Saydamlık kamera çarpışmasının YERİNE geçmez.** Bir ara duvar taraması röntgen
> açıkken kapatılmıştı: kamera duvarın arkasına, dünyanın dışına çıkıyordu. Saydamlık
> kamerayla oyuncu **arasındaki** cismi çözer, kameranın **nerede durduğunu** değil.

**İki geçişli çizim zorunlu:** saydam cisim opaklardan önce çizilirse derinlik
tamponuna yazar ve arkasındaki oyuncu elenir → duvar saydam değil *delik* görünür.

## Nişan / ateş mekanikleri
Her oyunun ateş hissi farklı; motor bunu **mod** olarak veriyor (`nisan_modu3d`):

| Mod | Yön | Tipik oyun |
|---|---|---|
| `AIM_FLAT` (varsayılan) | kameranın **yatay** yönü | üçüncü şahıs aksiyon |
| `AIM_LOOK` | kameranın **tam** yönü (eğim dahil) | nişancı / FPS |
| `AIM_BODY` | gövdenin baktığı yön | twin-stick, klasik |
| `AIM_LOCK` | menzildeki **en yakın** hedef | boss dövüşü, oto-nişan |

`AIM_FLAT` varsayılan çünkü üçüncü şahısta kamerayı eğmek çoğunlukla manzarayı
görmek içindir — yere bakmak "yere ateş et" demek değil. `AIM_LOCK` menzilde hedef
yoksa `AIM_FLAT`'e düşer ve hedefin **yüksekliğini** de tutar.

Yanında: `nisan_sacilma3d(derece)` konik sapma (pompalı, geri tepme) ve
`pompali3d(sahip, hiz, omur, n)`. Saçılmada **dikey** bileşen yalnız dikeyin
anlamlı olduğu modlarda uygulanır — yatay nişanlı oyunda mermilerin yere
saçılması istenen şey değil.

> ⚠️ **İKİ AYRI YAW SÖZLEŞMESİ** (bir kez karıştırıldı, mermiler tam ters gitti):
> gövde yönü `(sin y, +cos y)` (move3d'nin `atan2(wx, wz)`'si), kamera ileri
> yönü `(sin y, −cos y)` (`_s3_camera`). Bu yüzden `_aim_dir3` açı değil
> doğrudan **yön vektörü** kuruyor.
>
> Bu hatanın testten kaçmasının sebebi öğreticiydi: test beklentisini nişan
> formülünün **kendisinden** türetiyordu, yani hatayı onaylıyordu. Yeni test
> beklentiyi bağımsız bir kaynaktan alıyor — `move3d`'nin ürettiği dünya hızı
> ("W'ye bassam nereye giderdim") — ve nokta çarpımını sınıyor. → [[Testing]]

## Alt sistemler
| Konu | API | Not |
|---|---|---|
| Bölüm | `bolum3d(n, fn)`, `bolum_gec3d()` | Geçiş **kare sonunda** uygulanır |
| Tetikleyici bölge | `bolge3d`/`bolge_kure3d`, `girince3d`/`cikinca3d`/`icindeyken3d` | Entity DEĞİL; giriş/çıkış **kenarı** |
| Bölge EYLEMİ (veri) | `bolge_eylem3d(t, ZACT_*, miktar)`, `bolge_ses3d` | Kod kancası olmadan: kazan/kaybet/gireni yok et/hasar/skor/**sonraki bölüm** |
| Kural eylemi | `ACT_COLLECT/DAMAGE/KILL/HURT/WIN/LOSE/**NEXT**` | `ACT_NEXT` olmadan veri sahnesi bölüm ilerletemiyordu |
| Yol bulma (veri) | `kovala` davranışında **"duvarlari dolas"** anahtarı | A* artık koddan değil SAHNEDEN de erişilebilir |
| Kalıcılık | `kayit_ac3d()` (OPT-IN), `rekor3d()` | Diske yazdığı için opt-in |
| UI | `baslangic3d`, duraklat, oyun-bitti, **bölüm seçme**, **ayarlar** | Menüde imleç + kol A/B; düğmeler TÜRE göre dağıtılıyor |
| Ses seviyesi | `ses_seviye3d(v)`/`volume3d`, `ana_ses(v)` (tame) | ANA seviye; diske yazma opt-in |
| Konumsal ses | `ses3d(tutamak, x, y, z)`, `ses_yukle(yol)` | mesafe + stereo yön; örnek: `scene3d_arena` |
| Ses tanısı | `ses_son_seviye3d()`, `ses_son_kaydirma3d()`, `ses_calma_sayisi3d()` | "çalmadı" ile "seviye 0 ile çaldı"yı ayırır; örnek: `scene3d_ses_testi` ([[Tuzaklar]] §3b) |
| Arazi | `arazi3d`, `arazi_dogal3d`, `arazi_katmani3d` | Katman boyama tepe rengiyle |
| Gündüz-gece | `gunduz_gece3d(sn)`, `saati_ayarla3d` | Gölgeler güneşle döner |
| Girdi | klavye + dokunmatik + **gamepad** | Üçü aynı anda açık |
| Fare bakışı | yörünge modunda **varsayılan açık** | `fare_bakis3d(false)` ile sağ-tuş moduna döner |
| Tanılama | **F1** örtü, **F2** dosyaya döküm | gözcüler + seviyeli kayıt |
| Can | `can3d`, `hasar3d`, `iyilestir3d` | `heal3d` invuln penceresine yazmaz |

## Menü katmanı — başlık düğmeleri DURUMA göre
Kayıt sistemi bitirilen bölümleri diske yazıyordu ve kaldığın yeri
(`unlocked_level3d`) hesaplıyordu, ama **hiçbir yer o cevabı sormuyordu**:
beş bölüm bitiren oyuncu ertesi gün yine 1. bölümden başlıyordu. Fonksiyon
yalnız testlerden çağrılıyordu — kayıt sisteminin başlığındaki söz tutulmuyordu.

- Başlık ekranı artık **Başla / [Devam (Bölüm N)] / [Bölümler] / Çıkış**.
  Köşeli parantezliler koşullu: **Devam** yalnız ilerleme varsa (yoksa
  "Devam (Bölüm 1)" ile "Başla" aynı şeydir), **Bölümler** yalnız çok bölümlü
  VE kayıt açıkken (kayıt yoksa hiçbir bölüm açılmaz, ekran çıkmaz olurdu).
- Dağıtım **konuma göre değil TÜRE göre** (`_s3_title_kind3`): liste duruma
  göre uzayıp kısalıyor, sabit eşleme araya bir düğme girdiğinde sessizce
  yanlış işi yaptırırdı. ESC de sabit sayıya değil "son düğme"ye gidiyor.
- Bölüm ızgarasında **kilitli bölüm çiziliyor ama iş görmüyor**: gizlemek
  "kaç bölüm var" bilgisini de götürürdü. Açıklık kuralı tek yerde ve
  `unlocked_level3d`'den geliyor — kesintisiz, yani 3 bitip 2 bitmediyse 3
  açılmıyor.
- Seçim **ertelenmiş geçiş** kuruyor (`goto_level3d`), oyun içindeki bölüm
  değişimiyle aynı yol; menüden anında yüklemek aynı işi ikinci bir yerde
  yapmak olurdu. `goto_level3d` "bitti" işaretlemiyor, yani seçme ilerlemeyi
  uydurmuyor.

> ⚠️ **İki ayrı "bölüm sayısı" var.** `level_count3d()` sahne JSON'undaki
> diziyi sayıyor ve elle yazılmış oyunda (`bolum3d(1, "kur1")`) o dizi BOŞ;
> oynanabilir sayı `_lvlN`'de. Menü `_s3_lv_count3()` kullanıyor — ilk yazımda
> `level_count3d()` kullanıldı ve "Bölümler" düğmesi kod tabanlı oyunlarda hiç
> çıkmadı.

> **Tarama sonucu (2026-09-01):** aynı gözle bütün manşet özellikler
> tarandı. Beş tanesinin hiç örneği yoktu; üçü kapatıldı — **bölge sesi**
> (arena, zehir havuzu), **kural sesi** (`toplayici.scene.json`, tek satır
> VERİ, kod değil) ve **yüzme/kaldırma kuvveti** (terrain, suyun üstünde
> yüzen sandık — su vardı, üstünde duran hiçbir şey yoktu). Kalan ikisi
> bilerek açık: **müzik** için depoda ses dosyası yok (binary varlık kararı)
> ve **nişan modu** arena'nın otomatik ateşiyle çakışıyor. Yanlış alarm da
> çıktı: animasyon harmanlaması `scene3d_karakter`'de otomatik yoldan
> (`anim3d`) zaten gösteriliyor, tarama yalnız elle seçim API'sini arıyordu.

> ⚠️ **Motorun ses yolu HİÇBİR 3B örnekte gösterilmiyordu** (2026-09-01'e
> kadar). Kural sesi, bölge sesi ve konumsal stereo ses hepsi yazılmış ve
> testliydi, `.wav` varlıkları depodaydı — ama tek bir `scene3d_*` örneği
> onlara bağlı değildi. Sonuç: ses ayarını deneyecek bir yer yoktu ve
> "oyundan ses gelmiyor" bir gerileme gibi görünüyordu, oysa o oyun
> sessizdi. `scene3d_arena` artık isabet/bonus/ölüm seslerini konumlu
> çalıyor. **Ders: bir özelliğin testi varsa ama örneği yoksa, kullanıcı
> için var değildir.**

#### Devamı: "hâlâ hiçbir şey duymadım" → arıza değil, ÖLÇÜM PENCERESİ
Arena seslendirildikten sonra da bölge sesleri duyulmadı ve bir tur boyunca
**bütün penceresiz sondalar yeşildi** — çünkü gerçekten doğrulardı. Tahmin
etmeyi bırakıp ölçmenin yolu açıldı: motor artık çalarken UYGULANAN değerleri
kaydediyor (`ses_son_seviye3d` · `ses_son_kaydirma3d` · `ses_calma_sayisi3d`)
ve `examples/scene3d_ses_testi.tpr` beş ses katmanını ayrı ayrı sürüyor.

**Sonuç (kullanıcı doğruladı, 2026-09-01): beş katman da sorunsuz çalışıyor.**
Yani arena'da duyulmayan şey bozuk değildi; sesler kısa (`ates.wav` 0.14 sn,
`altin.wav` 0.24 sn), zehir havuzununki yalnız GİRİŞ kenarında ve hasar
parçacıklarının altında, bonus pedi ise tek atım olduğu için oturum başına
bir kez çalıyor. Duyulmamaları için hata gerekmiyordu.

**İki ders, ikisi de pahalıya alındı:**
1. **Sessizlik tek bir arıza değil** — en az beş sebep aynı belirtiyi veriyor
   (aygıt · dosya · olay olmadı · ses çağrılmadı · seviye 0). Ayırmadan
   aramak saatler yakıyor. Ayrım tablosu: [[Tuzaklar]] §3b.
2. **Bir özelliği "çalışıyor" ilan etmek için tekrarlanabilir ve
   GÖRÜLEBİLİR olması gerekiyor.** Tanı sahnesindeki hiçbir bölge tek atım
   değil ve her çalışta ekrana bildirim düşüyor — duyduğun ile gördüğün
   eşleşiyor. Ölçülemeyen bir geri bildirim, olmayan bir geri bildirimdir.

### Ayarlar — ses seviyesi
Motorda **ana ses seviyesi yoktu**: yalnız müzik başına `music_volume` vardı,
yani yayınlanmış bir oyunda sesi kısmanın yolu yoktu. Yeni tame binding'i
`tm_master_volume` (sarmalayıcı `master_volume`/`ana_ses`) ses ve müziğin
ikisini birden ölçekliyor; müzik başına ayar onun ÜSTÜNE biniyor.

- Değer **motorda** tutuluyor (`_s3_vol3`) ve tame'e itiliyor: geri okuyacak
  bir builtin yok, olsaydı da iki doğruluk kaynağı olurdu.
- Diske yazmak **opt-in** (`kayit_ac3d`), kaydın geri kalanıyla aynı kural.
  Kapalıyken ayar yine çalışıyor, yalnız oturumla sınırlı.
- **Ayarlar duraklat menüsünde**, başlık ekranında değil: dikey liste en çok
  dört düğme taşıyor ve başlık zaten dördünü kullanabiliyor. Duraklat oyunun
  her yerinden bir tuş uzakta.
- **Geri, duraklata dönüyor** — oyuna değil; ayarlar oradan açıldı ve doğrudan
  dönmek duraklatmayı da sessizce kaldırırdı.

> ⚠️ **Seviye, ses aygıtı AÇILMADAN önce de ayarlanabilmeli.** Aygıt ilk ses
> yüklendiğinde açılıyor; doğrudan `SetMasterVolume` çağırmak `setup` içinde
> yapılan ayarı sessizce düşürürdü. Değer C tarafında saklanıyor ve aygıt
> açılınca uygulanıyor. Seviye ayarlamak aygıtı AÇMAK için sebep değil —
> headless'ta açılamaz ve hata basardı.

> ⚠️ **Menü tür sabitleri konumlarla ÇAKIŞMIYOR** (`_PB_* = 10..13`,
> `_TB_* = 20..23`). 0,1,2,3 verilseydi tür eşlemesi birim fonksiyon olurdu
> ve dağıtıcıyı `a - 1`'e çeviren bir kestirme hiçbir şeyi bozmadan
> çalışırdı — soyutlama sırayı değiştiren ilk düzenlemeye kadar yalnız kâğıt
> üstünde var olurdu. (Bozma denendi ve dejenere hâlde kaçtı.)

> ⚠️ Dikey düğme listesi **en çok dört** düğme taşıyor. Adım sabit 0.15 iken
> dördüncü düğme ekranın tam alt kenarına dayanıyordu (0.44 + 3×0.15 + 0.11 =
> 1.00, sıfır boşluk). Adım artık tavandan türüyor (`_s3_btn_step3`).

## Veri odaklı A* — motorun en iyi AI'ı editöre açıldı
`chase_path3d` elle yazılan koddan erişilebiliyordu ama **sahne biçiminden
değil**: editörün ürettiği `kovala` davranışı düz kovalamaya düşüyor ve U
biçimli tuzaktan çıkamıyordu. Yani editörle oyun yapan biri motorun en iyi
düşman AI'ını kullanamıyordu.

- Ayrı bir davranış TÜRÜ değil, `kovala`'nın **bayrağı** (`Bh3.c`): iş aynı
  ("en yakın hedefi kovala"), yalnız gezinme stratejisi değişiyor. Ayrı tür,
  hedef bulma ve menzil mantığını ikinci kez yazmak olurdu. Bayrak aynı
  zamanda motorun kararını yansıtıyor: `chase3d` varsayılan, A* **opt-in**.
- Biçimde `"path": 1` ve yalnız AÇIKKEN yazılıyor.
- Editörde anahtar: **"duz git" / "duvarlari dolas"** (durumun adı, "yol bul:
  acik" değil).

> ⚠️ **Izgara TEMBEL kuruluyor** — ilk yol bulma isteğinde, dağıtımın içinde.
> Kurulmasaydı `chase_path3d` sessizce düz kovalamaya düşerdi: editördeki
> anahtar açık görünür ve hiçbir şey yapmazdı. Düz kovalama ızgara KURMUYOR
> (A*'ı opt-in tutma kararı bunu gerektiriyor), ve elle kurulmuş bir ızgara
> EZİLMİYOR (`_s3_nav_auto3` yalnız hiç ızgara yokken devreye giriyor).

> ⚠️ **Bölüm değişimi ızgarayı geçersiz kılıyor** (`nav_invalidate3d`).
> Kılmasaydı 2. bölümün düşmanı 1. bölümün labirentini dolaşırdı — duvarlar
> başka yerde, yol saçma, hiçbir hata yok.

Izgara hâlâ **statik**: duvar oynayınca kendini yenilemiyor. Bu artık sessiz
değil — sahne denetimi söylüyor: *"yol bulma acik ama duvar hareketli —
izgara bayat kalir"*. Uyarı yalnız DUVAR hareketliyse çıkıyor; devriye gezen
düşman ızgarayı bozmuyor ve ona uyarmak uyarıyı gürültüye çevirirdi.

## Test edilebilirlik — motorun tasarımını belirleyen kısıt
`tests/scene3d_engine.test.tpr` **653 test**, hepsi **pencere açmadan** koşuyor.
Bunu mümkün kılan iki desen:
- **Cihaz okuması tek yere hapsedilir** (`_read_touch3`, `_read_gamepad3`) —
  motorun geri kalanı yalnız tamponu okur.
- **Karar mantığı saf fonksiyonlara ayrılır** (`_cam_allowed3`, `_gp_curve3`,
  `_dn_mix3`, `_col_r3`) — raylib'e yazılan şey geri okunamıyor.

Doğrulanamayan iki şey dürüstçe kayıtlı: **çizim sırası** ve **alfa karışımı** (ikisi de GPU'da).

> ⚠️ **Işık shader'ı alfayı eziyordu** (2026-08-14 düzeltildi): stok raylib ışıklandırması
> opak varsayımıyla yazılmış, `colDiffuse + vec4(specular, 1.0)` alfaya 1.0 ekliyor.
> 0.27'lik tint alfası 1.54'e çıkıp kırpılıyordu → saydam çizmek **imkânsızdı**.
> Artık `finalColor.a = texelColor.a * colDiffuse.a`.

## Bölümler — veriyle ilerleme
`level3d(n, fn)` KOD bölümleri; editörün "+ BOLUM EKLE"si ise **veri**
bölümleri (`_lvl_js3`, hepsi `_s3_level_data3`'e kayıtlı).

> ⚠️ **Veri bölümlerinde kamera her bölümde yeniden hedeflenmeli.** Bölüm
> değişimi entity deposunu siliyor; kod bölümlerinde kurulum fonksiyonu
> `camera_follow`'u yeniden çağırıyor, veri bölümlerinde öyle bir fonksiyon
> yok. Hedefin ADI saklanıp `_sc_load_level3` sonunda yeniden çözülüyor.

> ⚠️ **`_sc_clear_for_load3` bölümleri de temizliyor** ama kayıt defterini
> (`_lvlN`/`_lvlF`) yalnız VERİ bölümleri varken: elle yazılmış bir oyunun
> `level3d(1, "bolum1")` kayıtları KOD, sahne değil.

## Verinin ÜÇ yolu — kaydet · yükle · KOD ÜRET
Aynı sahne üç biçimde yaşıyor ve üçünün alan listesi **elle** eşlenmiş:

| yol | fonksiyon | ne veriyor |
|---|---|---|
| kaydet | `sahne_json3d()` | sahne → JSON |
| yükle | `sahne_yukle3d(js)` | JSON → sahne |
| **kod üret** | `sahne_kod3d()` / `scene_code3d()` | sahne → çalıştırılabilir `.tpr` |

Üçüncüsü `examples/scene3d_export.tpr` ile sürülüyor: editörde kurduğun sahneyi
elle yazılmış bir oyuna dönüştürüp üstüne kod yazmanın yolu. `--dogrula`
bayrağı yüklediğini yeniden serileştiriyor.

**Denklik denetimi** (`./build.sh suites`) üretilen `.tpr`'yi derleyip
çalıştırıyor ve kurduğu sahneyi kaynakla `diff`liyor — "kod da aynı sahneyi
kuruyor" iddiasını ölçen tek şey bu; üretilen metni gözle okumak yetmiyor.

> ⚠️ **Üç yol AYRIŞABİLİYOR ve ayrışma sessiz.** Kod üretici bölgenin
> eylemini/miktarını ve sesini, kuralın da sesini hiç yazmıyordu (2026-09-01):
> JSON'da var, kodda yok. Kaydet↔yükle gidiş-dönüşü yeşil olduğu için görünmedi
> ve denetim de göremedi — denetim sahnesinde **hiç bölge yok**du. Emitter
> tamamlandı, denetim artık iki sahne koşuyor (demo + kapsamı kasten dolduran
> `tests/kod_uretimi_tam.scene.json`). **Yeni serileştirilebilir alan ekleyen
> üçünü birden dolduracak ve düzeneği büyütecek.**
> → [[Tuzaklar]] §8 (sessiz veri kaybı)

## İlgili
[[Tame]] · [[Arcade]] · [[Editor]] · [[Android]] · [[Testing]] · [[Tuzaklar]] · [[Roadmap]] · [[Standard Library]]

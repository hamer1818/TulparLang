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
| Kalıcılık | `kayit_ac3d()` (OPT-IN), `rekor3d()` | Diske yazdığı için opt-in |
| UI | `baslangic3d`, duraklat, oyun-bitti | Menüde imleç + kol A/B |
| Arazi | `arazi3d`, `arazi_dogal3d`, `arazi_katmani3d` | Katman boyama tepe rengiyle |
| Gündüz-gece | `gunduz_gece3d(sn)`, `saati_ayarla3d` | Gölgeler güneşle döner |
| Girdi | klavye + dokunmatik + **gamepad** | Üçü aynı anda açık |
| Fare bakışı | yörünge modunda **varsayılan açık** | `fare_bakis3d(false)` ile sağ-tuş moduna döner |
| Tanılama | **F1** örtü, **F2** dosyaya döküm | gözcüler + seviyeli kayıt |
| Can | `can3d`, `hasar3d`, `iyilestir3d` | `heal3d` invuln penceresine yazmaz |

## Test edilebilirlik — motorun tasarımını belirleyen kısıt
`tests/scene3d_engine.test.tpr` **623 test**, hepsi **pencere açmadan** koşuyor.
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

## İlgili
[[Tame]] · [[Arcade]] · [[Editor]] · [[Android]] · [[Testing]] · [[Tuzaklar]] · [[Roadmap]] · [[Standard Library]]

# TODO — açık işler

Bu dosya **işlenebilir bir kontrol listesidir**: sırayla yukarıdan aşağı gidilir,
biten madde `[x]` işaretlenip tek satırlık sonucuyla bırakılır.

İlişkisi: [STATUS.md](STATUS.md) projenin "nerede duruyoruz" **anlatı** referansı
olmayı sürdürüyor (biten işlerin gerekçesi, ölçümler, v1.0 ölçütleri) —
oradaki "Faz 11+ backlog" bölümü bu listenin gerekçeli uzun hâli.
[CHANGELOG.md](CHANGELOG.md) ise ne değiştiğini anlatır. Burada **yalnız açık
işler** durur; biten madde buradan silinip STATUS/CHANGELOG'a geçer.

Sıra öneriyle yazıldı: önce oyunu "bozuk" gösteren şeyler, sonra görüntü,
sonra dil/altyapı borcu.

---

## 0 — Sahne editörü (görsel oyun yapımı)

Hedef: isteyen herkes arayüzden, hızlıca oyun yapabilsin. Beş faz; ilk ikisi
bitti.

Mimari kararlar ve gerekçeleri:
- **Editör MASAÜSTÜ bir uygulama** (`./editor`, motorun kendisiyle aynı ikili
  yığın). Başta "tarayıcıda çalışsın" planlanmıştı — gerekçe kurulum
  gerektirmemesi ve headless Chrome ile doğrulanabilmesiydi — ama editör
  motorun İÇİNDE Tulpar ile yazılınca web sürümü ayrı bir uygulama değil,
  AYNI kodun wasm'a derlenmiş hâli oluyor. Bu, JS tarafında bir taklit
  yazmaktan kesinlikle daha iyi: aynalayan bir izleyici zamanla sapar.
  Bedeli görsel doğrulamanın kullanıcıya kalması (Xvfb kurulu değil).
- **Görüntü penceresi GERÇEK motor** — sahne bir render texture'a çiziliyor ve
  panele yerleştiriliyor, yani editördeki görüntü oyunun gördüğünün aynısı.
- **Kaynak doğruluk tek dosyada:** üretilen `.tpr` içinde editörün sahip
  olduğu işaretli bölge + kanonik JSON yorumu. Tek dosya, tam gidiş-dönüş,
  ekstra çalışma zamanı makinesi yok.

- [x] **Faz 1 — Sahne biçimi.** ✅ 2026-08-24 — JSON sahne (dünya + varlıklar),
      `sahne_yukle3d`/`sahne_json3d`/`sahne_dosya3d`/`sahne_kaydet3d`, `bul3d`.
      Kendi sayı yazıcısı (`toString(120.0)` = "1.2e+02", üretilen Tulpar
      kodunda geçersiz). Gidiş-dönüş testi kamera mesafesinde birikimli bir
      kayma buldu. Detay: CHANGELOG.

- [x] **Faz 2 — Davranışlar + kurallar.** ✅ 2026-08-24 — move/chase/patrol/
      spin/bob/shoot ve hit/cleared kuralları. `examples/scene3d_data_game.tpr`
      tek satır oynanış kodu içermiyor. Yan ürün: katı-katı temas kancalarının
      hiç atmadığı motor hatası bulundu ve düzeltildi. Detay: CHANGELOG.

- [x] **Faz 3 — Editör kipi (motor içinde).** ✅ 2026-08-24 — TAB ile açılıyor
      (oyun donuyor), serbest uçuş kamerası, fareyle seçme, ızgaraya oturan
      sürükleme, ölçek/döndürme, sil/çoğalt/zemine-otur, F5 ile sahneyi JSON'a
      geri yazma. Işın matematiği saf ve fov TANIMINDAN sınanıyor. Detay:
      CHANGELOG.

- [x] **Tam editör arayüzü.** ✅ 2026-08-24 — araç çubuğu (Oynat / kip / ızgara /
      geri-ileri / kaydet), hiyerarşi paneli (canlı varlık listesi + ekleme
      düğmeleri), inspector (etiket, şekil, konum/boyut/yaw, renk, katılık,
      can, davranış listesi + ekle/sil, çoğalt/sil). Anlık-kip widget seti;
      sayı alanları hem sürüklenip hem yazılabiliyor. Detay: CHANGELOG.

- [x] **Editör bağımsız uygulama oldu.** ✅ 2026-08-24 — `./editor`; menü şeridi,
      araç çubuğu (OYNAT–DUR ortada), hiyerarşi | sahne görünümü | özellikler,
      konsol, durum çubuğu. Sahne görünümü render texture. TAB kaplaması
      kaldırıldı. Detay: CHANGELOG.

- [x] **Editör komut satırından dosya açıyor.** ✅ 2026-08-24 —
      `./editor benim_sahnem.json`; olmayan dosya yeni sahne demek. Dile
      `args()` eklendi (üretilen `main` artık argc/argv alıyor). Bir
      `tulpar editor` alt komutu hâlâ yapılabilir ama artık zorunlu değil.

- [x] **Editörün Unity/Unreal parçaları tamam.** ✅ 2026-08-24 — prefab dahil.
      (Hiyerarşide sürükle-bırak sıralama BİLEREK yapılmadı: bu motorda
      varlık sırasının oynanışa etkisi yok, yani sıralama kullanıcıya bir şey
      kazandırmıyor — arama/filtre o ihtiyacı zaten karşılıyor.)
      (Eksen tutamakları, dünya ayarları, varlık adı, konsol, kural
      düzenleyici, panel kaydırma, arayüz ölçeği, standart kısayollar, F ile
      çerçeveleme, hover vurgulama, kaydedilmemiş değişiklik uyarısı, yön
      göstergesi, sağ tık menüsü, araç ipuçları, arama, çoklu seçim, bölge
      düzenleme ✅ 2026-08-24.)

- [x] **Tetikleyici bölgeler sahne biçiminde.** ✅ 2026-08-24 — `"zones"`
      alanı; editörde hiyerarşi girdisi + özellik paneli + sağ tık ile ekleme.
      Kancaları biçimde taşınmıyor (kod) ama yeniden yüklemede korunuyor.

- [x] **`chr` builtin'i geldi.** ✅ 2026-08-24 — `ord`un tersi; editörün ASCII
      tablosu silindi.

- [~] **Faz 4/5 — JS köprüsü ve web kabuğu: GEREKSİZ KALDI.** Özgün plan
      editörü JS'te yazmak ve wasm'a bir komut köprüsüyle bağlamaktı. Editör
      motorun İÇİNDE Tulpar ile yazılınca web sürümü ayrı bir uygulama değil,
      AYNI kodun wasm'a derlenmiş hâli oluyor — köprüye de JS panellerine de
      gerek yok. Kalan gerçek iş aşağıda.

- [x] **Editör web'de çalışıyor.** ✅ 2026-08-25 — üç engel de kapandı.
      (a) font: `assets/ui.ttf`, `fonts/ui.ttf`, `ui.ttf` aday listesinin
      BAŞINA girdi, yani `TULPAR_WEB_ASSETS=<dizin>` ile paketlenen font
      bulunuyor; bulunamazsa konsol web'e özgü çareyi söylüyor.
      (b) dosya: `scene_save3d`/`scene_file3d` web'de localStorage'a gidiyor
      ("sahne:<yol>"), menü şeridine yalnız web'de görünen **İNDİR** düğmesi
      eklendi (`scene_export3d` → tarayıcı indirmesi).
      (c) `args()`: örnek web'de komut satırını hiç yoklamıyor, sabit ada
      düşüyor ve "depo kökünden çalıştır" mesajını vermiyor.

      Ölçüm: headless Chrome, iki ardışık sayfa yüklemesi — ilki 2 varlık
      kaydediyor, ikincisi geri okuyor; indirme CDP ile doğrulandı (dosya
      adı ve içerik birebir). GÖRSEL denetim yapılmadı.

      Kurulum:
      ```
      mkdir -p assets && cp <bir>.ttf assets/ui.ttf
      TULPAR_WEB_ASSETS=assets tulpar build --target=web \
          examples/scene3d_editor.tpr -o editor
      ```

- [x] **Kod üretimi.** ✅ 2026-08-24 — `sahne_kod3d()` + `scene3d_export.tpr`.
      Üretilen şey tek bir `kur()` FONKSİYONU (tam program değil) ki denklik
      ölçülebilsin. `build.sh suites` üretilen kodu derleyip çalıştırıyor ve
      kurduğu sahneyi kaynakla karşılaştırıyor.

- [x] **`wasm/dist` arşivleri tazelendi.** ✅ 2026-08-24 — web hedefi bağlanıyor
      (`scene3d_data_game` .html/.js/.wasm üretiyor). `android/dist` duruyor
      ama NDK bu makinede kurulu değil, yani doğrulanamadı.

- [x] **scene3d programlarında optimizasyon atlanıyordu.** ✅ 2026-08-24 —
      teşhis: LLVM 22'de iki ayrı kusur; O2+ `loop-idiom-recognize` yanlış
      mangle edilmiş `llvm.memset` üretiyor, O1 `InstCombine.foldOpIntoPhi`
      boxed-karşılaştırma merge'ünden tipsiz phi çıkarıyor. İkisi de bizim
      IR'imizde değil (modül optimizasyondan ÖNCE doğruluyor). Çözüm:
      InstCombine ve döngü geçişleri olmayan MUHAFAZAKÂR bir son basamak
      eklendi. Ölçüm: hesap ağırlıklı 3B yükte 3690 ms → 3270 ms (%12).
      LLVM yukarı sürümlerinde kusurlar düzelirse O3 kendiliğinden seçilir.

## 1 — Oyun yapımı (3B oyunun eksik hissettiren yerleri)

- [x] **Düşman yol bulma.** ✅ 2026-08-06 — `chase3d` artık engelden kaçınıyor
      (varsayılan AÇIK; `chase_direct3d()` ile eski davranış). Şişirilmiş kutu
      ışın testi + tarafa bağlanma. Yerel minimum sınırı belgelendi: tek başına
      U biçimli tuzaktan çıkamaz. Detay: CHANGELOG.

- [x] **Kayıt/yükleme 3B'ye bağlandı.** ✅ 2026-08-06 — rekor + bölüm ilerlemesi
      kalıcı (`kayit_ac3d()` ile OPT-IN, çünkü diske yazıyor). Anahtar sahne
      başlığından türüyor; `next_level3d` bölümü "bitti" işaretliyor,
      `goto_level3d` (serbest atlama) işaretlemiyor. Detay: CHANGELOG.

- [x] **Tetikleyici bölge geldi.** ✅ 2026-08-11 — `bolge3d`/`bolge_kure3d` +
      `girince3d`/`cikinca3d`/`icindeyken3d`. Giriş/çıkış KENARI hesaplanıyor
      (taklitte kanca her karede atıyordu) ve bölge entity değil. Tek atım,
      taşınabilirlik, ayıklama çizimi. Yanında `heal3d`/`iyilestir3d`: şifa
      hpmax'ı aşmıyor ve dokunulmazlık penceresine YAZMIYOR — `damage3d(-n)`
      ile şifa vermek oyuncuyu bir de hasara bağışık yapıyordu. Örnek:
      `scene3d_arena` ortasındaki şifa pedi. Detay: CHANGELOG.

- [x] **Gamepad `scene3d`'ye bağlandı.** ✅ 2026-08-13 — sol çubuk hareket
      (ANALOG: `move3d` artık büyüklüğü koruyor), sağ çubuk bakış, A zıpla,
      START duraklat, menülerde imleç + A/B. Okuma `_read_gamepad3()` içinde
      hapsedildi (dokunmatikle aynı desen) ki motor penceresiz test
      edilebilir kalsın. Detay: CHANGELOG.

- [x] **Animasyon harmanlaması geldi.** ✅ 2026-08-25 — `tm3_anim_blend`
      (`anim_harmanla()`/`anim_blend()`) iki pozu ağırlıkla karıştırıyor;
      motor ağırlığı hızdan türetip zamana yayıyor (`anim_gecis_hizi3d`,
      varsayılan 0.125 sn). Kendi iskelet kodumuz YOK: tek karelik geçici bir
      `ModelAnimation` kurulup raylib'in kendi skinning yolundan geçiliyor.
      Geçiş DOĞRUSAL — üstel yumuşatmada ağırlık uca hiç varmaz, yani "boşta"
      pozu sonsuza kadar biraz koşu taşırdı (test bunu ölçüyor).
      Örnek: `examples/scene3d_karakter.tpr` (robot.glb, scene3d).

- [ ] **`anim3d` hâlâ İKİ klip biliyor** (boşta + koşu). Yürüme/koşma/çömelme
      gibi üçüncü bir durum ya da rastgele boşta klipleri için ağırlık
      tek bir sayı olmaktan çıkmalı. Harmanlama altyapısı (`tm3_anim_blend`)
      N klibe hazır; eksik olan scene3d'nin durum makinesi.

---

## 2 — Görsel

- [x] **Arazi katman boyama geldi.** ✅ 2026-08-13 — yükseklik çim→toprak→kar,
      eğim kaya. Renk mesh'in TEPE NOKTALARINA yazılıyor (arazi sıradan bir
      model olarak kalıyor). Işık shader'ında `texelColor *= fragColor`
      gerekti: stok raylib ışık shader'ı tepe rengini fragment'a taşıyıp
      kullanmıyordu. `arazi_dogal3d(tepe)` tek satırlık palet,
      `arazi_katmani3d(x,z)` oyun mantığına keskin cevap. Detay: CHANGELOG.

- [x] **Gündüz-gece döngüsü geldi.** ✅ 2026-08-13 — `gunduz_gece3d(saniye)`,
      `saati_ayarla3d`, `saati_dondur3d`, `gunun_saati3d`, `gece_mi3d`.
      Gökyüzü + güneş rengi/yönü + ortam ışığı + sis rengi birlikte değişiyor;
      gölgeler bedava dönüyor (gölge haritası güneşin yönünden türüyor).
      Tamamen Tulpar tarafında — tek satır C eklenmedi. Detay: CHANGELOG.

- [x] **Su yüzeyi geldi.** ✅ 2026-08-14 — `su3d(y)` dünya çapında yatay düzlem,
      `su_altinda3d(id)` sorgusu, yüzme fiziği (azalmış yerçekimi + sürtünme +
      `zipla3` = yüzme vuruşu, zemin gerektirmez). Su EN SONDA çiziliyor: opak
      cisimlerden önce çizilirse derinlik tamponu altındaki her şeyi eler ve su
      düz bir levhaya döner. Kaldırma kuvveti (yüzdürme) bilerek modellenmedi.
      Detay: CHANGELOG.

- [x] **Suda kaldırma kuvveti geldi.** ✅ 2026-08-24 — `yuz_davranis3d(id,
      guc, derinlik)`. Dünya ayarı değil DAVRANIŞ (yüzmek cismin özelliği),
      yani serileşiyor ve editörden eklenebiliyor. Detay: CHANGELOG.

- [x] **Prosedürel bulutlar geldi.** ✅ 2026-08-24 — `bulut3d(kapsama)`.
      Yıldızlarla aynı çözüm (gökyüzü shader'ı), gündüz-gece ile sönüyor,
      `scene3d_terrain` örneğinde açık. Detay: CHANGELOG.

- [x] **Parçacıklarda dönme ve doku atlası geldi.** ✅ 2026-08-25 —
      `tm3_billboard_pro` (döndürülebilir + atlas kareli billboard) ve
      `tm3_atlas_grid`. Izgara ÇİZİMİN değil DOKUNUN özelliği: her karede
      söylenseydi 12 argümanlık bir builtin gerekirdi (tavan 8).
      Parçacıklar `parcacik_doku3d(doku, sutun, satir)` ile flipbook oynatıyor
      (ömür ilerledikçe kare ilerliyor, son karede KALIYOR — sarsaydı patlama
      ölürken yeniden başlardı) ve `parcacik_donme3d(derece_sn)` ile dönüyor.
      **Dönme varsayılan KAPALI**: dokusuz parçacık dolu bir karedir, dönünce
      silueti de döner — varsayılanı açmak yayınlanmış oyunların görünüşünü
      haber vermeden değiştirirdi. Regresyon testi bunu koruyor.
      Örnek: `scene3d_karakter.tpr` (zıplayınca toz pufu; `smoke.png` 4×4
      atlası bu depoda üretildi).

---

## 3 — Dil / derleyici borcu

Bunlar 2026-08-06 oturumunda motorun içinde çalışırken **bizzat çarpılan**
boşluklar; backlog'da yoklardı.

- [x] **Builtin tablosu ↔ codegen ayrışması denetime bağlandı.** ✅ 2026-08-14 —
      `tests/builtin_audit.py` üç listeyi (codegen / typeinfer / LSP) karşılaştırıyor,
      `build.sh suites` ve CI'da koşuyor. Ayrışmanın iki yönü de ÖLÇÜLDÜ:
      7 builtin tabloda vardı ama çağrılınca "fonksiyon bulunamadı" veriyordu;
      96'sı codegen'de vardı ama tabloda yoktu, yani o çağrılar **hiç
      denetlenmiyordu** (`str s = len("abc")` yakalanıyor, `str s = pow("a","b")`
      sessizce geçiyordu). 14 matematik builtin'i tabloya, 17'si LSP'ye eklendi.
      Kalan 47 boşluk `KNOWN_GAPS` içinde **takip ediliyor** — görünmez değil.
      Detay: CHANGELOG.

- [ ] **Builtin denetimindeki 47 bilinen boşluk kapatılmalı.**
      `tests/builtin_audit.py` → `KNOWN_GAPS`. İki grup:
      - **Kırık vaat (7):** `clock`, `toBool`, `toLower`, `toUpper`, `values`,
        `socket_recv`, `socket_select` — tabloda var, codegen'de yok. Ya
        uygulanmalı ya tablodan çıkarılmalı. (`toLower`/`toUpper` zaten
        `lower`/`upper` olarak var; muhtemelen ölü takma ad.)
      - **Denetimsiz (40):** `print`, `random`, `min`, `max`, `mod`, `join`,
        `is*` ailesi, `base64_*`, tarih/saat yardımcıları… Her biri bir imza
        satırı; `min`/`max` gibi dönüşü argümana bağlı olanlar dikkat ister
        (yanlış imza, imzasızlıktan kötüdür).

- [ ] **Tipli dizi yok.** `float[] x = []` ayrıştırma hatası veriyor; yalnız
      `array` var. Geniş fazı yazarken çarpıldı — hem performans hem typecheck
      kaybı.

- [ ] **`var` yerine `int` sessiz tuzağı.** `int fn_ref = 0` yazılınca fonksiyon
      referansı kırpılıyor ve `call()` hiçbir şey yapmıyor; derleyici uyarmıyor.
      typecheck "fonksiyon referansını int'e yazıyorsun" diyebilir.

- [x] **Argüman sınırında `bool`→`int` geldi.** ✅ 2026-08-25 — çağrılan
      tarafın parametre önsözü artık bildirimle AYNI yardımcıyı çağırıyor
      (`llvm_coerce_bool_tag_to_int`), yani ikisi ayrışamıyor.
      Hata SESSİZDİ ve bu yüzden görünmedi: aritmetik yol değeri zaten
      zorluyordu (`x + 10` doğru çıkıyor), ama karşılaştırma ÖNCE tip
      etiketine bakıyor — `f(true)` sonrası gövdedeki `x == 1` HER ZAMAN
      false idi. Ölçüldü. typecheck'in çağrı sınırındaki reddi de kalktı
      (artık çalışan bir şeyi reddediyordu).
      Yeni süit: tests/bool_to_int_arg.test.tpr + typeinfer pass/06.
      TİPSİZ parametrede bool bool kalıyor — dönüşüm `int` BİLDİRİLDİĞİ için
      oluyor, her yerde değil.

- [x] **`%` operatörü geldi.** ✅ 2026-08-24 — `mod()` builtin'i duruyor ve
      ikisi AYNI sonucu veriyor (işaret bölünenden, C ile aynı). Önceliği
      `*` ve `/` ile aynı. Ondalıkta `fmod`. Yeni süit: tests/modulo.test.tpr.

- [x] **Küre ↔ DÖNÜK kutu düzeldi.** ✅ 2026-08-25 — `_sph_box3` küre
      merkezini kutunun yerel çerçevesine taşıyor; dönüş yönü `_seg_aabb3`
      ile AYNI (çarpışma ile kamera/ışın ayrışamaz). 3 test eklendi.
      90°'de kutu simetrik olduğu için yön bozması kaçıyordu — ölçüldü,
      testler 45°'de iki köşegeni ayrıştırıyor ve ışın testini bağımsız
      referans alıyor.

---

## 4 — Altyapı ve belge

- [ ] **Çarpışma hâlâ O(n²).** Geniş faz sabiti 14× küçülttü (200 entity
      15.4 → 1.12 ms), ama asimptot duruyor. Uniform grid artık ucuz: düz
      konum/yarıçap dizileri grid'in zaten isteyeceği zemin. ~800 entity
      üstüne çıkılmadıkça getirisi yok.

- [ ] **`wasm/dist` ve `android/dist` arşivleri bayat kalıyor.** Katman boyama
      `tame_impl.c`'ye yeni sembol ekledi; bu arşivler elle yeniden
      derlenmedikçe (`wasm/build_tame_web.sh`, `android/build_tame_android.sh`)
      web/Android hedefi link hatası verir. Masaüstünde görünmeyen, yalnız o
      hedeflerde patlayan bir sınıf — arşiv tazeliğini denetleyen bir kontrol
      (kaynak zaman damgası karşılaştırması) ucuz olur.
      ✅ Denetim geldi (2026-08-25): web ve Android link'inden ÖNCE arşiv
      zaman damgası `runtime/tame_impl.c` ve kardeşleriyle karşılaştırılıyor,
      eskiyse tazeleme komutunu söyleyen bir uyarı çıkıyor. Uyarı, hata
      değil: bayat arşivde gereken semboller varsa link tutar.
      Kalan borç, arşivleri TAZELEMEK hâlâ elle: `android/dist` duruyor
      (NDK bu makinede kurulu değil).

- [x] **`lib/test.tpr` artık BÜTÜN hataları gösteriyor.** ✅ 2026-08-24 —
      mesaj eziliyordu, yani ilk kırılan iddia (asıl sebebi söyleyen o)
      kayboluyordu. İlk 5 tanesi yazılıyor, sonrası "(+N daha)" olarak
      özetleniyor — bir döngü içindeki assert yüzlerce satır dökerdi.

- [x] **`packages/` testleri artık `build.sh suites` içinde.** ✅ 2026-08-24 —
      paketin kendi klasöründen koşuyorlar (`import` çalışma dizinine göre
      çözülüyor) ve özet satırı yoksa BAŞARISIZ sayılıyorlar.

- [x] **Kökteki bayat `.a` arşivleri artık gölgelemiyor.** ✅ 2026-08-24 —
      bağlayıcı arama sırası, exe dizinindeki arşiv geliştirme ağacındakinden
      ESKİYSE ters çevriliyor. `cmake --build` ile artımlı derleyen biri için
      sinsi bir "undefined reference" kaynağıydı (bu oturumda bir kez yakaladı).

- [ ] **arcade ve tame'in kendi belge sayfaları yok.** `games/overview` ikisine
      de değiniyor ama API referansları yok; `games/scene3d` deseni izlenerek
      `games/arcade` ve `games/tame` yazılmalı. (10 yayınlanmış tarayıcı oyunu
      arcade kullanıyor, yani okuyucu kitlesi hazır.)

- [ ] **3B oyun `tulparlang.dev/oyunlar`'a konulmadı.** Teknik engel kalmadı
      (Faz 7 şartı sağlandı, menü/duraklat/yeniden-başla geldi).

- [ ] **3B örneklerin İngilizce ikizi yok.** `examples/en/` altında arcade
      oyunlarının ikizleri var, 3B'nin yok.

- [ ] **Windows shim'leri ölü kod.** Natif Windows 3.13.0'da bırakıldı;
      `PLATFORM_WINDOWS` dalları bilerek yerinde ama bakımsız ve test edilmiyor.

- [ ] **Rampa artık gereksiz olabilir.** Arazi geldiğine göre çoğu kullanım
      arazinin işi. Ya gerçek kama mesh'i üretilmeli ya da rampa "arazi yokken
      kullanılan basit yol" olarak konumlandırılmalı.

- [x] **Konumsal seste yön geldi.** ✅ 2026-08-25 — `sound3d`/`ses3d` artık
      mesafe zayıflatmasının yanında stereo kaydırma da uyguluyor
      (`ses_yon_gucu3d(0..1)` ile ayarlanır, 0 kapatır).
      raylib'in anlamı TERS (`left = pan; right = 1 - pan`, yani pan=0 SAĞ);
      çeviri scene3d katmanında, builtin raylib'in anlamını taşıyor.
      Sağ ekseni `move3d`'nin girdi döndürmesiyle AYNI ifadeden geliyor —
      ayrışsalar "sağa yürü" ile "sağdan duy" farklı yönler gösterirdi;
      test tam bunu ölçüyor. Değer çalmadan ayrı (`_snd_pan_val3`), yani
      ses aygıtı olmadan sınanabiliyor.

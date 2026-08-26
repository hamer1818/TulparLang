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

- [x] **`anim3d` artık N klip biliyor.** ✅ 2026-08-25 — geçiş (a, b, w)
      üçlüsü üzerinden yürüyor; "iki klip" hiç özel değildi, yalnız hedefi
      hesaplayan kural iki seçenekliydi.
      `animasyon_sec3d(i, klip)` / `anim_set3d` klibi oyunun seçmesini
      sağlıyor, `anim_auto3d` hızdan türeyen locomotion'a döndürüyor,
      `anim_now3d` görünen klibi veriyor.
      Makinenin üç dalı ayrı ayrı ölçülüyor: aynı hedef geçişi SIFIRLAMAZ
      (sıfırlasaydı ağırlık hiç ilerlemez, "harmanlama hiç çalışmıyor" gibi
      görünürdü), yarıda GERİ dönüş geçişi ters çevirir (w = 1-w) sıfırdan
      başlatmaz, üçüncü klibe geçiş BASKIN pozdan başlar.
      Dört bozma denendi, dördü de yakalandı.

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

- [x] **Builtin denetimindeki 47 boşluğun HEPSİ kapandı.** ✅ 2026-08-25 —
      `KNOWN_GAPS` artık boş, yani imzasız yeni bir builtin denetimi kırmızıya
      çevirir.
      - **Kırık vaat (7):** `values`, `toBool`, `toUpper`, `toLower`
        UYGULANDI. `clock` (karşılığı `time_ms`/`timestamp`), `socket_recv`
        (gerçek ad `socket_receive`) ve `socket_select` (karşılığı
        `socket_poll`) tablodan ÇIKARILDI — `socket_poll` de imzasızdı,
        aynı anda imzalandı.
      - **Denetimsiz (40):** hepsi imzalandı. Dönüş tipleri OKUNDU ve ayrıca
        çalıştırılıp `isInt/isFloat/...` ile doğrulandı; iki sürpriz çıktı ve
        ikisi de tahminle yanlış yazılırdı: `min`/`max` int argümanda bile
        FLOAT döndürüyor, `path_match` bool değil `{matched, params}` JSON'u
        döndürüyor (router'ın yol eşleyicisi, yüklem değil).
      - Denetim ayrıca 16 eksik LSP girdisi buldu; onlar da eklendi.
      - Fixture'lar: typeinfer fail/10, pass/09. pass fixture'ı dönüşleri
        TİPLİ DEĞİŞKENE bağlıyor — yalnız `print()`'e geçirmek yetmiyordu,
        `min`'i STRING yapan bir bozma oradan sessizce geçti (ölçüldü).

- [x] **`float[]` sözdizimi geldi.** ✅ 2026-08-25 — ölçüm TODO'nun
      tarifini düzeltti: tipli diziler dilde ZATEN vardı (`arrayFloat` /
      `diziOndalık`), eksik olan yalnız köşeli ayraç YAZIMIydı — C/Java/Go/
      TypeScript'ten gelen herkesin ilk deneyeceği biçim.
      `parse_type` artık taban tipten sonra `[]` soneklerini tüketiyor;
      `[` görülmezse taban tip olduğu gibi dönüyor, yani hiçbir mevcut
      bildirim etkilenmiyor.
      İki yazımın AYNI tipe çözüldüğü typeinfer fixture'ıyla ölçülüyor —
      çalışma zamanı bunu göremez (diziler her hâlükârda kutulu), yani
      eleman tipinin sessizce kaybolması hiçbir belirti vermezdi. Nitekim
      o bozma, çalışma zamanı süiti YEŞİLKEN yalnız fixture'la yakalandı.
      Yeni süit: tests/typed_array_syntax.test.tpr + typeinfer fail/09,
      pass/08.

- [x] **Fonksiyon referansı artık modelleniyor.** ✅ 2026-08-25 — ölçüm,
      TODO'nun tarifinden farklı bir gerçek gösterdi: referans çalışma
      zamanında fonksiyonun ADI (bir string), yani `int f = selam; call(f)`
      ÇALIŞIYOR. Bozuk olan TANI idi: typecheck bildirilen `int`'i doğru
      sayıp hatayı bir satır sonra `call(f)` üzerinde "expected str, got int"
      diye veriyordu — masum olan `call`'ı gösteriyordu.
      Artık bildirimin/atamanın kendisi işaretleniyor, çare adıyla söyleniyor
      (`var` kullan) ve sembol GERÇEKTE tuttuğu tiple kaydediliyor, yani
      ardından yanıltıcı ikinci hata gelmiyor.
      Asıl kazanç `sayi_al(selam)` gibi çağrılar: eskiden SESSİZDİ, çalışma
      zamanında çöp üretiyordu (ölçüldü: 140276196302865). Artık hata.
      Fixture'lar: typeinfer fail/07, fail/08, pass/07.

- [x] **typeinfer fixture'ları artık MESAJI da denetliyor.** ✅ 2026-08-25 —
      `// EXPECT: <parça>` satırları. Yalnız çıkış koduna bakmak yetmiyordu:
      fonksiyon referansı tanısını kasten bozan iki deneme, YANLIŞ ama yine de
      sıfırdan farklı çıkış veren bir hata sayesinde fixture'lardan kaçtı.

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

- [x] **Çarpışma artık ızgaralı — ve asıl darboğaz başka yerdeydi.**
      ✅ 2026-08-26. Maddenin "~800 entity üstüne çıkılmadıkça getirisi yok"
      tahmini ÖLÇÜMLE ÇÜRÜDÜ: gerçek eşik ~300, çünkü 400 entity'de kare
      15.9 ms sürüyor ve 60 fps bütçesi (16.7 ms) zaten aşılıyor.

      İki ayrı iş çıktı:

      1. **`_ramp_floor3` n² imiş.** Zemin sorgusu entity başına BÜTÜN
         entity'leri geziyor ve her adımda bir `Ent3` struct kopyası
         alıyordu. Rampa dizini (kare başına bir kez, O(n)) bunu doğrusala
         indirdi — `_s3_physics` tek başına, ms/kare:
         100: 0.99→0.17 · 400: 15.9→0.66 · 800: 69.4→1.28 · 1600: 470→2.55
         (1600'de **184×**). Bu maliyet ÇARPIŞMADA değildi; ızgara ona hiç
         dokunmuyordu ve ilk ölçümüm bu yüzden "ızgara işe yaramadı" dedi.

      2. **Uniform ızgara** (sarmalı, 64×64, kare damgalı kovalar) dört
         taramaya da uygulandı: duvar çözümü, hareketli-hareketli, mermi-duvar,
         kanca taraması ve veri-kural taraması.
         `_s3_physics` + `_s3_collision` birlikte, kancalı, ms/kare:
         100: 1.22→0.49 · 200: 4.23→1.27 · 400: 15.6→3.14 · 800: 60.0→8.92
         Yani 800 entity artık bütçenin içinde.

      Hücre kenarı = en büyük kapsayan küre ÇAPI; bu 3×3 taramasını korumalı
      yapıyor. Sarma yalnız uzak cisimleri aynı kovaya düşürebilir (fazladan
      dar-faz sorgusu, kaçırma değil).

      Doğruluk KABA KUVVETLE karşılaştırılıyor: hem kanca hem kural yolu için
      ızgaranın bulduğu çakışma kümesi, bütün çiftleri gezen bir taramayla
      birebir aynı olmak zorunda. Hızlı ve yanlış bir geniş faz, yavaş ve
      doğru olandan kötüdür — kaçırılan çarpışma sessizdir.

      Kalan: `_grid_near3` her çağrıda yeni bir dizi ayırıyor. 800'ün
      üstünde tekrar bakılabilir.

- [x] **Arşiv tazeliği denetimi tamam; `android/dist` bu makinede
      tazelenemiyor.** ✅ 2026-08-26.

      Denetim İKİ yerde:
      1. Web/Android **link'inden önce** (sürücü). Android tarafında sıra
         düzeltildi: kontrol artık NDK aramasından ÖNCE — eskiden NDK'sız bir
         makinede sürücü "NDK gerekir" deyip çıkıyordu ve uyarı o yola hiç
         varmıyordu, üstelik bayatlığın en kolay gözden kaçacağı makine tam
         olarak orası.
      2. **`build.sh suites`** içinde, her test koşumunda. Yalnız zaman
         damgası karşılaştırması, hiçbir araç zinciri gerektirmiyor. Uyarı,
         hata değil: arşiv yoksa o hedef zaten kullanılmıyor demektir ve
         emsdk'sı olmayan birini kırmızıya boğmak yanlış olur.
      Bozma ile sınandı: kaynak dosyaya dokununca `wasm/dist` de bayat
      bildiriliyor, tazeleyince listeden çıkıyor.

      Ölçülen durum: **`wasm/dist` TAZE** (bu oturumda dört kez yeniden
      derlendi). **`android/dist` 11 Ağustos'tan kalma** ve kullanılamaz
      olduğu KANITLANDI — `nm` ile bakıldı, 25-26 Ağustos'ta eklenen altı
      sembolün (`aot_tm3_sky_clouds_ptr`, `aot_tm_download_ptr`,
      `aot_tm_is_web_ptr`, `aot_tm3_anim_blend_ptr`,
      `aot_tm3_billboard_pro_ptr`, `aot_tm_sound_pan_ptr`) HİÇBİRİ yok.

      Tazelemek NDK istiyor ve bu makinede NDK YOK: `~/Android/android-ndk-*`,
      `ndk-build`, `sdkmanager` — üçü de arandı, hiçbiri bulunamadı,
      `TULPAR_ANDROID_NDK` boş. Yani bu, yapılmamış bir iş değil, bu makinede
      yapılamayan bir iş; artık her `build.sh suites` koşumunda tazeleme
      komutuyla birlikte bildiriliyor. Dosyalar gitignore'lu derleme çıktısı
      olduğu için silinmedi — kullanıcının başka bir makineden senkronlaması
      mümkün.

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

- [x] **arcade ve tame belge sayfaları ZATEN VAR.** ✅ ölçüldü 2026-08-26 —
      `origin/master`'da `games/arcade.mdx` (246 satır) ve `games/tame.mdx`
      (146 satır), TR ikizleriyle birlikte. Madde YANLIŞTI: yerel
      `tulpar-lang-web` kopyası `docs/debugger-and-tls` adlı ESKİ bir dalda
      duruyor ve o daldan bakınca dosyalar yok görünüyor.

- [x] **3B oyunlar `tulparlang.dev/oyunlar`'da ZATEN YAYINDA.** ✅ ölçüldü
      2026-08-26 — `origin/master`, commit `b1808b8`:
      `public/oyunlar/3d_collector`, `3d_robot`, `3d_models`, `3d_primitives`
      (.html/.js/.wasm, robot için .data). Bu madde de eski dal yüzünden
      eksik görünüyordu.

      Bu oturumda dördü de yeniden derlendi (`web_demo/`) ve headless
      Chrome'da açıldığı doğrulandı: raylib GL bağlamını kuruyor, abort yok.
      **Görsel denetim YAPILMADI.**

      Yöntem notu: bu sayfalarda `--dump-dom` ASILIYOR (sonsuz oyun döngüsü,
      ASYNCIFY) ve boş çıktı veriyor — yani "hata metni bulunamadı" ile
      "hiçbir şey okunamadı" ayırt edilemiyor. Kanıt, akış hâlinde gelen
      KONSOL satırı (raylib'in kendi GL uyarısı), DOM değil.

- [x] **Web belgeleri güncellendi ve commit edildi.** ✅ 2026-08-26 —
      `games/{overview,scene3d,editor}.mdx` (TR+EN) commit edilmemiş
      duruyordu ve motorun bu haftaki değişikliklerini anlatmıyordu.
      `scene3d.mdx`'e üç bölüm eklendi (karakter ve animasyon, parçacıklar,
      konumsal ses), `editor.mdx`'teki "planlanan tarayıcı editörü" dili
      düzeltildi ve "Tarayıcıda" bölümü yazıldı.
      Belgelerdeki **197 API adının hepsi motora karşı doğrulandı** — uydurma
      ad yok. Site derleniyor (72 sayfa).
      Yerel dalda commit edildi (`f955252`); **push YAPILMADI**.

- [ ] **`tulpar-lang-web` dal birleştirme kararı KULLANICININ.** Yerel dal
      `docs/debugger-and-tls`, `origin/master`'ın atası değil ve ikisi
      AYRIŞIK içerik taşıyor: master'da `games/{arcade,tame,build,quickstart}`
      var, bu dalda yok; bu dalda `games/{overview,scene3d,editor}` var,
      master'da yok. Ayrıca bu dalın `astro.config.mjs` kenar çubuğu yalnız
      kendi üç sayfasını listeliyor, master'ın dördünü değil — birleşince
      kenar çubuğu elle uzlaştırılmalı.
      Bu bir kod işi değil, bir dal stratejisi kararı; ayrı depoda push/merge
      da dışa dönük bir işlem. Ölçüm ve seçenekler kayıtlı, karar kullanıcıda.

- [x] **3B örneklerin İngilizce ikizi geldi.** ✅ 2026-08-25 — 15 dosya:
      8 `scene3d_*` (arena, camera, character, collector, data_game, editor,
      export, terrain) + 7 `tame3d_*`.

      Bu iş sırada BAŞKA bir boşluk buldu: `examples/en/` HİÇ test edilmiyordu
      — koşucu yalnız `examples/*.tpr` üzerinde geziyordu, yani ikizler
      kaynaktan ayrışsa (yeniden adlandırılmış API, kaldırılmış builtin)
      kimse görmezdi. Artık hepsi compile-only olarak koşuyor.

      İkizleri yazarken İngilizce API adlarının bir kısmını yanlış tahmin
      ettim (`zone3d`, `self3`, `title3d`…); gerçekte hepsi VAR ama başka
      yazımla (`trigger3d`, `me3d`, `menu3d`). Yani iki dilli API sözü
      tutulmuş — eksik olan yalnız ikizlerdi.

- [x] **Windows shim'leri: KARAR VERİLDİ, iş yok.** Natif Windows 3.13.0'da
      bırakıldı; `PLATFORM_WINDOWS` dalları BİLEREK yerinde. Bunu bir görev
      olarak taşımak yanlış — CLAUDE.md kararı zaten kayıtlı: sökmek
      soketler/thread'ler/dl/yollar boyunca büyük ve riskli bir refactor,
      desteklenen platformlarda görünür bir kazancı yok, durmasının maliyeti
      ise sıfır. Bakımsız ve test edilmemiş sayılıyorlar; yeni kod yine shim
      başlıkları üzerinden geçiyor ama Windows dalı isteğe bağlı.

- [x] **Rampa artık GERÇEK kama mesh'i.** ✅ 2026-08-25 — iki seçenekten
      birincisi seçildi (konumlandırma değil, düzeltme): raylib'de kama
      primitifi yok, mesh'i tame kuruyor (`gen_wedge` / `tm3_gen` kind 7).
      Eskiden 12 kademeli kutu çiziliyordu; fizik analitik eğimi kullandığı
      için yürüyüş pürüzsüzdü ama GÖRÜNTÜ merdivendi.
      Sarma yönü elle sıralanmıyor: her üçgen istenen normale göre kendini
      düzeltiyor — ters sarılmış bir yüz arkayüz ayıklamasıyla sessizce
      GÖRÜNMEZ olur, yani hata "hata yok" gibi durur.
      Denetim: `tests/wedge_mesh_check.py` (build.sh suites içinde) üçgenleri
      C KAYNAĞINDAN okuyup sarma yönünü, kapalılığı ve eğimin `_ramp_h3` ile
      aynı tanımda olduğunu ölçüyor. İlk yazımında eğim normalini kendisi
      TÜRETİYORDU ve o yüzden normali bozan deneme kaçtı — normal de artık
      kaynaktan okunuyor, üç bozma da yakalanıyor.
      Pencere yokken (headless test) mesh üretilemiyor ve çizim eski kademeli
      yola düşüyor; motorun penceresiz sınanabilirliği korunuyor.

- [x] **Konumsal seste yön geldi.** ✅ 2026-08-25 — `sound3d`/`ses3d` artık
      mesafe zayıflatmasının yanında stereo kaydırma da uyguluyor
      (`ses_yon_gucu3d(0..1)` ile ayarlanır, 0 kapatır).
      raylib'in anlamı TERS (`left = pan; right = 1 - pan`, yani pan=0 SAĞ);
      çeviri scene3d katmanında, builtin raylib'in anlamını taşıyor.
      Sağ ekseni `move3d`'nin girdi döndürmesiyle AYNI ifadeden geliyor —
      ayrışsalar "sağa yürü" ile "sağdan duy" farklı yönler gösterirdi;
      test tam bunu ölçüyor. Değer çalmadan ayrı (`_snd_pan_val3`), yani
      ses aygıtı olmadan sınanabiliyor.

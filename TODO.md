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

- [ ] **Editörü web'de çalıştır.** `tulpar build --target=web
      examples/scene3d_editor.tpr`. Bilinen engeller: (a) `load_font` sistem
      yolu arıyor, web'de dosya sistemi yok → bitmap fonta düşer, (b) sahne
      dosyası açma/kaydetme tarayıcıda `read_file`/`write_file` değil
      localStorage/indirme olmalı, (c) `args()` web'de anlamsız. Üçü de
      çözülebilir; hiçbiri mimari değil.

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

- [ ] **Animasyon geçişi/harmanlama yok.** `anim3d` boşta↔koşu arasında sert
      geçiyor. **Önce örnek lazım:** `anim3d`'yi hiçbir örnek kullanmıyor
      (tek animasyonlu model örneği `tame3d_anim.tpr` ham tame ile yazılmış,
      scene3d değil). Harmanlama yazılsa da bakılacak bir sahne olmadığı için
      iş, `robot.glb` kullanan bir scene3d örneğiyle birlikte yapılmalı.

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

- [ ] **Suda kaldırma kuvveti (yüzdürme) yok.** Cisim suda yavaşça batıyor,
      yüzeye çıkmak için yüzme vuruşu gerekiyor. Basit ve öngörülebilir ama
      tahta sandık/varil gibi "kendiliğinden yüzen" nesneler yapılamıyor.
      Batma derinliğiyle orantılı yukarı ivme yeterdi.

- [x] **Yıldızlar geldi.** ✅ 2026-08-14 — gökyüzü shader'ında PROSEDÜREL
      (bakış yönü hash'lenip eşiği geçen hücre yıldız oluyor): sıfır çizim
      çağrısı, sıfır asset ve örtüşme kendiliğinden doğru — kubbe en arkada
      çizildiği için dağlar yıldızları örtüyor. 2B çizilseydi önlerine
      düşerlerdi. Gündüz-gece döngüsüyle otomatik açılıp kapanıyor;
      `yildiz3d_sabit(x)` ile elle sabitlenebiliyor. Detay: CHANGELOG.

- [ ] **Bulut yok.** Yıldızlar prosedürel çözümün işe yaradığını gösterdi;
      aynı shader'a gürültü tabanlı bulut katmanı eklenebilir (yön + zaman
      uniform'u yeterdi). Gökyüzü hâlâ bulutsuz bir degrade.

- [ ] **Parçacıklarda dönme ve doku atlası yok** — tek boy düz billboard.

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

- [ ] **Argüman sınırında `bool`→`int` dönüşümü yok.** `int x = true` ve
      `x = true` dönüşüyor, `f(true)` → `int` parametresi dönüşmüyor.
      `assert` hatası tam bu boşlukta doğmuştu. Artık typecheck yakalıyor
      (sessiz değil), asıl çözüm codegen'de parametre bağlamayı store ile aynı
      hale getirmek.

- [ ] **`%` operatörü yok** — `mod()` var.

- [ ] **Rezerve kelime çarpması teşhis edilemiyor.** `dene` (= `try`) adında bir
      fonksiyon tanımlayıp çağırınca hata "sözdiziminde bir eksiklik var" diyor;
      "`dene` rezerve bir kelime" demiyor. 2026-08-14'te bir ölçüm betiği bu
      yüzden dakikalarca yanlış yerde arattı. `move`/`don` için CLAUDE.md'de not
      var ama derleyici sessiz. Tanımlayıcı beklenen yerde bir anahtar kelime
      görüldüğünde mesaj bunu SÖYLEMELİ — ucuz ve doğrudan teşhis kazancı.

- [x] **AOT optimizasyonu `scene3d_arena`'da geçersiz IR üretiyordu.**
      ✅ 2026-08-24 — doğrulayıcıya bakıldı ve teşhis kondu: sorun ölçekte
      değil, LLVM 22'deki iki kusurda (O2+ `loop-idiom-recognize` yanlış
      mangle edilmiş `llvm.memset`; O1 `InstCombine.foldOpIntoPhi` tipsiz
      phi). Yalnız en büyük programda görünmesinin sebebi tetikleyen kod
      şeklinin orada bulunması. Muhafazakâr bir optimizasyon basamağı
      eklendi; ölçüm ~%12 hızlanma. Detay: CHANGELOG.

- [ ] **Küre ↔ DÖNÜK kutu yaklaşık.** `_sph_box3` kutuyu eksen-hizalı
      varsayıyor; kutu-kutu çifti tam SAT'tan geçiyor, küre-kutu geçmiyor.

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

- [x] **`lib/test.tpr` artık BÜTÜN hataları gösteriyor.** ✅ 2026-08-24 —
      mesaj eziliyordu, yani ilk kırılan iddia (asıl sebebi söyleyen o)
      kayboluyordu. İlk 5 tanesi yazılıyor, sonrası "(+N daha)" olarak
      özetleniyor — bir döngü içindeki assert yüzlerce satır dökerdi.

- [ ] **`packages/` testleri hiçbir otomasyonda koşmuyor.** `build.sh suites`
      benzeri bir hedef gerekiyor; ayrıca paket dizininden koşulmaları şart
      (`import` CWD'ye göre çözülüyor).

- [ ] **Kökteki bayat `.a` arşivleri taze derlemeyi gölgeliyor.** AOT link
      arama sırası önce çalıştırılabilirin dizinine bakıyor; iki ayrı oturumda
      yanıltıcı link hatasına yol açtı. `build.sh` her çalıştırmada
      tazeleyebilir.

- [x] **Oyun belgeleri geldi.** ✅ 2026-08-14 — `tulpar-lang-web`'de yeni
      **Oyun Geliştirme** bölümü: `games/overview` (üç katman + web/Android
      hedefleri) ve `games/scene3d` (tam API referansı), ikisi de EN + TR.
      Not: maddenin iddiasının aksine arcade'in de sayfası YOKTU — sitede
      oyunla ilgili hiçbir sayfa yoktu. Site 70 sayfa olarak derlendi.

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

- [ ] **Konumsal seste yön yok** — yalnız mesafe zayıflatması var, stereo
      kaydırma (panning) yok.

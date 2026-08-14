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

- [ ] **Su yüzeyi yok.** Saydam bir düzlem + "su altında mıyım" sorgusu;
      arazi geldiğinden beri en çok eksik hissettiren şey (vadiler kuru).

- [ ] **Gökyüzü dokusu yok** — degrade + gündüz/gece var, yıldız/bulut yok.

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

- [ ] **AOT optimizasyonu `scene3d_arena`'da geçersiz IR üretiyor.**
      `[AOT] Warning: optimization produced invalid IR at every level; using
      the unoptimized module` — yani örnek OPTİMİZASYONSUZ derleniyor.
      2026-08-12'de ölçüldü: tetikleyici bölge işinden ÖNCE de vardı (eski ve
      yeni `lib/scene3d.tpr` ile birebir aynı), yani mevcut borç. Diğer üç
      scene3d örneği ve arcade temiz — yalnız en büyük program tetikliyor,
      bu da bir eşik/ölçek hatasına işaret ediyor. Uyarı görünür olduğu için
      teknik olarak "sessiz" değil, ama sonuç öyle: en büyük 3B örnek
      optimizasyonsuz koşuyor ve kimse bakmıyor. Doğrulayıcının (verifier) ne
      dediğine bakılmalı.

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

- [ ] **`lib/test.tpr` yalnız SON hatayı gösteriyor.** Bir test fonksiyonunda
      birden çok assert düşerse rapor edilen mesaj sonuncusu oluyor; ilk kırılan
      yer kayboluyor. 2026-08-13'te bir bozma denemesinde çarpıldı: menü imleci
      testi doğru şekilde kızardı ama mesaj alakasız bir satırı işaret ediyordu.
      Teşhis kalitesi bu projede ucuz bir konu değil — `assert` hatası tam da
      "test doğru şeyi söylemiyor" ailesindendi.

- [ ] **`packages/` testleri hiçbir otomasyonda koşmuyor.** `build.sh suites`
      benzeri bir hedef gerekiyor; ayrıca paket dizininden koşulmaları şart
      (`import` CWD'ye göre çözülüyor).

- [ ] **Kökteki bayat `.a` arşivleri taze derlemeyi gölgeliyor.** AOT link
      arama sırası önce çalıştırılabilirin dizinine bakıyor; iki ayrı oturumda
      yanıltıcı link hatasına yol açtı. `build.sh` her çalıştırmada
      tazeleyebilir.

- [ ] **`scene3d` API belgesi yok.** arcade'in Starlight sayfası var, 3B'nin
      yok. Motoru başkasının kullanabilmesinin ön koşulu.

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

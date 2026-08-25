# Changelog

All notable changes to TulparLang are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/): MAJOR for breaking
language/stdlib/ABI changes, MINOR for backwards-compatible features, PATCH for
fixes. Releases are cut by pushing a `v*` tag (see [RELEASING.md](RELEASING.md));
`tulpar --version` reports the tag at release time and `<version>-dev` otherwise.

## [Unreleased]

### Added — sahne artık VERİ: JSON sahne biçimi + davranışlar + kurallar

Bir sahne buraya kadar yalnız KOD olarak vardı: `setup()` içinde elle yazılmış
`spawn3(...)` çağrıları. Bu, motoru kullanmanın tek yolunu "önce Tulpar öğren"
yapıyordu ve konumları gözle ayarlamak imkânsızdı — sayıyı tahmin et, derle,
bak, düzelt. Görsel bir editörün ön koşulu sahnenin veri olması.

**Sahne biçimi.** `scene_load3d`/`sahne_yukle3d`, `scene_file3d`, `scene_json3d`,
`scene_save3d`, `find3d`/`bul3d`. Anahtarlar İngilizce ve tek yazımlı: API'de
TR+EN ikizler var ama bir VERİ biçiminde iki yazım belirsizlik üretir ve
ayrıştırıcıyı ikiye katlar.

Kendi sayı yazıcısı var (`_sc_num3`): `toString(120.0)` `"1.2e+02"` veriyor —
JSON bunu kabul eder ama üretilen Tulpar kodu etmez, insan da okumaz. 3
ondalıkta yuvarlamak gidiş-dönüşü ayrıca DURAĞAN yapıyor.

Gidiş-dönüş testi gerçek bir hata buldu: `camera_orbit(d,h)` `_cam_dist`'e
yörünge YARIÇAPINI (`sqrt(d²+h²)`) yazıyor, geçilen yatay mesafeyi değil. Ham
değeri kaydeden serileştirici her tur kamerayı biraz daha geri itiyordu
(16 → 18.87 → 21.35). Ters dönüşüm tam: `r²-h² = d²`.

**Davranışlar** — `move`, `chase`, `patrol`, `spin`, `bob`, `shoot`. Hepsi
motorda ZATEN var olan işlevlerin üstüne biniyor; yeni fizik yazılmadı, yalnız
veriden sürülüyor. Böylece davranışla kurulmuş oyun ile elle yazılmış oyun aynı
kodu koşturuyor. KOD VERİYİ EZER: davranışlar `update()`ten önce işliyor.

**Kurallar** — `{"on":"hit","a":"player","b":"item","do":"collect","n":50}` ve
`{"on":"cleared","tag":"item","do":"win"}`. Tek dağıtıcı mevcut `on_hit3d`'nin
üstüne biniyor, kurallar için ikinci bir çarpışma taraması yok.

`examples/scene3d_data_game.tpr` + `examples/scenes/toplayici.scene.json`: tek
satır oynanış kodu içermeyen, oynanabilir bir oyun.

### Added — 3B örneklerin İngilizce ikizleri (+ `examples/en/` artık test ediliyor)

`examples/en/` altında arcade oyunlarının ikizleri vardı, 3B'nin yoktu. On beş
dosya eklendi: sekiz `scene3d_*` (arena, camera, character, collector,
data_game, editor, export, terrain) ve yedi `tame3d_*`.

Bu iş sırada başka bir boşluk buldu: **`examples/en/` hiç test edilmiyordu.**
Örnek koşucusu yalnız `examples/*.tpr` üzerinde geziyordu, yani ikizler
kaynaktan ayrışsa — yeniden adlandırılmış bir API, kaldırılmış bir builtin —
kimse görmezdi. Hepsi artık compile-only olarak koşuyor; ayrışmanın belirtisi
zaten "derlenmiyor" oluyor.

İkizleri yazarken İngilizce API adlarının bir kısmı yanlış tahmin edildi
(`zone3d`, `self3`, `title3d`…). Ölçüm düzeltti: hepsi VAR, yalnız başka
yazımla (`trigger3d`, `me3d`, `menu3d`). Yani iki dilli API sözü tutulmuş
durumda; eksik olan yalnız ikiz örneklerdi.

### Fixed — builtin denetimindeki 47 boşluğun hepsi kapandı

**Kırık vaatler (7).** `values`, `toBool`, `toUpper`, `toLower`, `clock`,
`socket_recv`, `socket_select` typeinfer tablosunda VARDI ama codegen'de
yoktu: çağıran "'values' adında bir fonksiyon bulunamadı" alıyordu. Kırık bir
vaat, imzasızlıktan kötüdür.

Dördü uygulandı: `values(o)` (`keys` ile AYNI sırada, yani `keys(o)[i]` ile
`values(o)[i]` eşleşiyor), `toBool(v)` (doğruluk kuralı `if` koşuluyla aynı —
ayrı bir kural uydurmak sessiz bir ayrışma yaratırdı), `toUpper`/`toLower`
(`upper`/`lower` takma adları; ad doğal ve insanlar onu yazıyor).

Üçü tablodan çıkarıldı: `clock` (karşılığı zaten `time_ms`/`timestamp`),
`socket_recv` (gerçek ad `socket_receive`), `socket_select` (karşılığı
`socket_poll` — o da imzasızdı, aynı anda imzalandı ve dönüşü DİZİ, int
değil).

**Denetimsiz 40.** Tabloda olmayan bir builtin yalnız argümanları denetimsiz
bırakmıyor: çağrı VOID sayılıyor, o yüzden sonucun kullanıldığı her satır da
atlanıyor. Kırkı da imzalandı.

Dönüş tipleri okundu VE çalıştırılıp `isInt/isFloat/...` ile doğrulandı. İki
sürpriz çıktı, ikisi de tahminle yanlış yazılırdı:
- `min`/`max` **her zaman float** döndürüyor, int argümanlarda bile.
- `path_match` bool değil, `{matched, params}` JSON'u döndürüyor — router'ın
  yol eşleyicisi, bir yüklem değil.

En keskin yakalama `join`: argüman sırası ayırıcı-ÖNCE (`join("-", xs)`),
Python'daki `xs.join(sep)` refleksinin tersi. Ters yazmak hiçbir uyarı
üretmiyordu.

Denetim bu sırada 16 eksik LSP girdisi de buldu (`join`, `repeat`, `is*`
ailesi, tarih yardımcıları); onlar da eklendi.

`KNOWN_GAPS` artık **boş**: imzasız yeni bir builtin denetimi kırmızıya
çevirir.

### Added — `float[] x = []` sözdizimi

Ölçüm, kaydedilmiş şikâyeti düzeltti: tipli diziler dilde ZATEN vardı
(`arrayFloat`, `diziOndalık`, `arrayInt`, …). Eksik olan yalnız köşeli ayraç
YAZIMIydı — C, Java, Go ve TypeScript'ten gelen herkesin ilk deneyeceği biçim,
ve `float[] x = []` ayrıştırma hatası veriyordu.

`parse_type` artık taban tipten sonra `[]` soneklerini tüketiyor. `[`
görülmezse taban tip olduğu gibi dönüyor, yani mevcut hiçbir bildirim
etkilenmiyor. `int[][]` gibi iç içe biçimler düz `array`'e düşüyor: motorun
iç içe dizi için ayrı bir eleman tipi yok ve yanlış bir eleman tipi
uydurmaktansa tipsiz kalmak doğru.

İki yazımın AYNI tipe çözüldüğü bir typeinfer fixture'ıyla ölçülüyor. Bunu
çalışma zamanı süiti GÖREMEZ — diziler her iki yazımda da kutulu, program
ikisinde de çalışır — yani `int[]`'in sessizce tipsiz `array`'e düşmesi
hiçbir belirti vermeden bütün typecheck kazancını götürürdü. Nitekim tam o
bozma denendiğinde çalışma zamanı süiti yeşil kaldı, hatayı yalnız fixture
yakaladı.

Yeni süit: `tests/typed_array_syntax.test.tpr` + typeinfer `fail/09`,
`pass/08`.

### Fixed — fonksiyon referansı yanlış yeri gösteriyordu

Ölçüm, beklenenden farklı bir gerçek gösterdi: bir fonksiyon referansı çalışma
zamanında fonksiyonun ADIDIR (bir string) — `f + 0` yazınca `"selam0"` çıkması
bunun kanıtı. Yani `int f = selam; call(f)` **çalışıyor**; bozuk olan TANI idi.

typecheck bildirilen `int`'i doğru sayıyor, hatayı bir satır sonra `call(f)`
üzerinde "Argument 1 of 'call': expected str, got int" diye veriyordu — masum
olan `call`'ı gösteren bir mesaj. Artık bildirimin (ve atamanın) kendisi
işaretleniyor, çare adıyla söyleniyor (`var` kullan), ve sembol GERÇEKTE
tuttuğu tiple kaydediliyor ki ardından yanıltıcı ikinci hata gelmesin.

Asıl kazanç başka yerde: fonksiyon adı artık `str` olarak çıkarıldığı için
`sayi_al(selam)` gibi bir çağrı yakalanıyor. Eskiden SESSİZDİ — çalışma
zamanında string nesnesi üzerinde ham işaretçi aritmetiği yapıp çöp
üretiyordu (ölçüldü: `140276196302865`).

### Changed — typeinfer fixture'ları artık MESAJI da denetliyor

`tests/typeinfer/fail/*.tpr` içine `// EXPECT: <parça>` satırları konabiliyor;
her parça çıktıda geçmek zorunda. Yalnız çıkış koduna bakmak yetmiyordu ve bu
varsayım değil ölçüm: fonksiyon referansı tanısını kasten bozan iki deneme,
YANLIŞ ama yine de sıfırdan farklı çıkış veren bir hata sayesinde
fixture'lardan kaçtı. Reddedilmiş olmak, DOĞRU sebeple reddedilmiş olmak
demek değil.

### Fixed — `int` PARAMETREYE geçen `bool` gövdede hâlâ bool'du

`func f(int x)` çağrılırken `f(true)` yazılınca değer gövdeye **bool etiketiyle**
ulaşıyordu. Aritmetik yol bunu zorluyor (`x + 10` doğru sonuç veriyor), o yüzden
hata görünmedi; görünen yer karşılaştırma: çalışma zamanı ÖNCE tip etiketine
bakıyor, yani `x == 1` **her zaman false** oluyordu.

`lib/test.tpr`'daki `assert`'in sessiz no-op olması tam bu boşlukta doğmuştu ve
boşluk hâlâ açıktı — o hata yalnız typecheck tarafında kapatılmıştı, codegen
tarafında değil.

Çağrılan tarafın parametre önsözü artık bildirimle AYNI yardımcıyı çağırıyor
(`llvm_coerce_bool_tag_to_int`, `llvm_values.cpp`). Çağıran tarafta değil
ÇAĞRILAN tarafta: orada tek bir yer var ve doğrudan çağrı, `call()` ile dinamik
gönderim ve modül-takma-adlı çağrılar hepsi oradan geçiyor.

typecheck'in çağrı sınırındaki reddi de kalktı — kural codegen'in ESKİ
davranışını yansıtmak için yazılmıştı ve artık provably çalışan bir şeyi
reddediyordu. `tests/typeinfer/fail/06` bu çifti "kötü argüman" örneği olarak
kullanıyordu; fixture'ın asıl koruduğu şey içe aktarılmış imzaların
denetlenmesi olduğu için örnek `str`→`int`'e çevrildi (hiçbir şey string'i
int'e çevirmiyor), amacı korunarak.

TİPSİZ parametrede bool bool kalıyor: dönüşüm `int` BİLDİRİLDİĞİ için oluyor,
her yerde değil.

Yeni süit: `tests/bool_to_int_arg.test.tpr` (7 test) + `typeinfer/pass/06`.

### Fixed — çarpışma O(n²)'den çıktı; ve asıl darboğaz çarpışma değilmiş

TODO "~800 entity üstüne çıkılmadıkça getirisi yok" diyordu. Ölçüm bunu
çürüttü: gerçek eşik ~300, çünkü 400 entity'de kare **15.9 ms** sürüyor ve
60 fps bütçesi (16.7 ms) zaten aşılıyor.

Ölçüm ayrıca darboğazın nerede OLMADIĞINI gösterdi. Uniform ızgarayı yazıp
çarpışma taramalarına uyguladım ve tablo **hiç değişmedi**. Sebep:
`_ramp_floor3`. Zemin sorgusu entity başına BÜTÜN entity'leri geziyor ve her
adımda bir `Ent3` struct kopyası alıyordu; fizik onu entity başına bir kez
çağırdığı için maliyet n² idi ve ÇARPIŞMADA değildi.

**1. Rampa dizini** (kare başına bir kez kurulan, O(n)). `_s3_physics` tek
başına, ms/kare:

| entity | önce | sonra |
|---|---|---|
| 100 | 0.99 | 0.17 |
| 400 | 15.94 | 0.66 |
| 800 | 69.43 | 1.28 |
| 1600 | 470.39 | 2.55 |

Dizin bir ÜST KÜME: ölmüş ya da slot'u yeniden kullanılmış girdiler kalabilir
ve `_ramp_floor3` her girdide `alive`/`shape` denetimini yine yapar. Böylece
dizini `kill3d` ile senkron tutma zorunluluğu yok — senkronu kaçırmak sessiz
bir hata olurdu, üst küme olmak değil.

**2. Uniform ızgara** (sarmalı, 64×64, kare damgalı kovalar) beş taramaya
uygulandı: duvar çözümü, hareketli-hareketli, mermi-duvar, kanca taraması,
veri-kural taraması. `_s3_physics` + `_s3_collision` birlikte, kancalı:

| entity | önce | sonra |
|---|---|---|
| 100 | 1.22 | 0.49 |
| 200 | 4.23 | 1.27 |
| 400 | 15.55 | 3.14 |
| 800 | 59.96 | 8.92 |

Hücre kenarı = en büyük kapsayan küre ÇAPI. Bu seçim 3×3 taramasını KORUMALI
yapıyor: çakışan iki cismin merkez mesafesi yarıçapları toplamını aşamaz, o
da bir hücre kenarını — yani en fazla komşu hücrededirler. Izgara sarmalı, o
yüzden dünya ne kadar büyük olursa olsun kova sayısı sabit; sarma yalnız uzak
cisimleri aynı kovaya düşürebilir ve bu fazladan bir dar-faz sorgusudur,
kaçırma değil.

Doğruluk KABA KUVVETLE karşılaştırılıyor — hem kanca hem veri-kural yolu
için. Hızlı ve yanlış bir geniş faz, yavaş ve doğru olandan kötüdür:
kaçırılan çarpışma sessizdir (oyuncu duvardan geçer, mermi düşmanı deler) ve
hiçbir hata mesajı çıkmaz. Kural yolunun iddiası SAYIM değil KÜME üzerinden
kurulu, çünkü `ACT_COLLECT` ötekini öldürdüğü için çağrı sayısı sıraya bağlı;
ölen eşya kümesi ise sıradan bağımsız.

### Fixed — rampa artık gerçek bir kama mesh'i (12 kademeli kutu değil)

raylib'de kama primitifi yok, o yüzden rampa yerel +Z boyunca 12 kademeli
kutu olarak çiziliyordu. Fizik analitik eğimi kullandığı için yürüyüş
pürüzsüzdü — merdiven yalnız GÖRÜNTÜDEYDİ, ama oradaydı.

Mesh'i artık tame kuruyor (`gen_wedge` / `tm3_gen` kind 7). Kutu gibi
ortalanmış ve eğim tanımı `_ramp_h3` ile birebir aynı; ayrışsalar görünen
rampa ile basılan rampa farklı yerlerde olurdu.

**Sarma yönü elle sıralanmıyor.** Her üçgen, kendisine verilen normale göre
köşe sırasını düzeltiyor. Sebebi ölçülebilirlik: bu mesh'i gözle doğrulamak
pencere açmayı gerektirir (depoda yasak) ve ters sarılmış bir yüz arkayüz
ayıklamasıyla sessizce GÖRÜNMEZ olur — yani hata "hata yok" gibi durur.

Denetim `tests/wedge_mesh_check.py` ile yapılıyor (`build.sh suites` içinde)
ve üçgen listesini C KAYNAĞINDAN okuyor, kopyasını tutmuyor: kopya tutsaydı C
değişince denetim eski hâli doğrulamaya devam ederdi. Üç şeyi ölçüyor — sarma
yönü, katının kapalı olması, eğimin fizikle aynı tanımda olması.

Denetim ilk yazımında eğim normalini KENDİSİ TÜRETİYORDU, kaynaktan
okumuyordu; o yüzden normali düz yukarı yapan bozma ondan kaçtı (üç bozmadan
yalnız ikisi yakalanmıştı). Normal de artık kaynaktan okunuyor.

Pencere yokken (headless test) mesh üretilemiyor ve çizim eski kademeli yola
düşüyor — motorun penceresiz sınanabilirliği korunuyor.

### Added — parçacıklarda dönme ve doku atlası

Parçacıklar tek boy DÜZ bir dörtgendi: yirmi tanesi de birbirinin aynısı
duruyordu, çünkü hepsi aynı anda aynı şekilde sönüyordu.

`tm3_billboard_pro` döndürülebilir ve atlas kareli billboard çiziyor,
`tm3_atlas_grid` bir dokuyu cols×rows ızgara olarak işaretliyor. Izgara
ÇİZİM ÇAĞRISININ değil DOKUNUN özelliği — her karede söylenseydi 12
argümanlık bir builtin gerekirdi (tame tavanı 8) ve aynı bilgi her çağrıda
tekrarlanırdı.

Dönme merkezi dörtgenin ORTASI; köşe olsaydı döndürme parçacığı kendi
konumundan kaydırırdı, yani dönme aynı zamanda yer değiştirme olurdu.

Parçacıklar `parcacik_doku3d(doku, sutun, satir)` ile flipbook oynatıyor:
ömür ilerledikçe atlas karesi ilerliyor ve son karede KALIYOR. Sarsaydı
patlama ölürken yeniden başlardı. Atlas olmadan aynı resim yalnız küçülüyor,
DEĞİŞMİYOR — duman/patlama sayfalarının varlık sebebi bu.

**Dönme varsayılan KAPALI.** Dokusuz parçacık dolu bir karedir ve dolu kare
dönünce silueti de döner (kare↔baklava): varsayılanı açmak yayınlanmış her
oyunun parçacıklarını haber vermeden değiştirirdi. Bir regresyon testi bunu
koruyor — ve o test ilk yazımında SIRAYA BAĞIMLIYDI, bir öncekinin bıraktığı
değeri ölçüyordu, yani varsayılanı açan bozma ondan kaçtı. Parçacık ayarları
artık `scene3d_reset()` ile sahneyle birlikte dönüyor (gerekçe `_slope_lim3`
ile aynı), test de bunu ölçüyor.

`examples/assets/smoke.png` — 4×4 duman sayfası, bu depoda üretildi.

### Added — konumsal seste stereo yön

Mesafe zayıflatması "ne kadar uzakta" diyordu ama "hangi yönde" demiyordu:
solundaki patlama ile sağındakini ayırt edemiyordun. `sound3d`/`ses3d` artık
kaydırmayı da uyguluyor.

raylib'in kaydırma anlamı TERS ve kolay kaçan bir tuzak — `raudio.c`'de
`left = pan; right = 1 - pan`, yani **pan=0 SAĞ kanaldır**. Çeviri scene3d
katmanında yapılıyor; `tm_sound_pan` builtin'i raylib'in anlamını olduğu gibi
taşıyor, çünkü ham tame kullanan kod raylib belgesine bakacak.

Kameranın sağ ekseni `move3d`'nin girdi döndürmesiyle AYNI ifadeden geliyor.
Ayrışsalar "sağa yürü" ile "sağdan duy" farklı yönleri gösterirdi ve bu kamera
döndükçe sessizce büyüyen bir hata olurdu — test iki ekseni keyfi bir açıda
karşılaştırıyor.

Kaydırma değeri çalmadan AYRI hesaplanıyor (`_snd_pan_val3`), yani ses aygıtı
olmayan bir makinede de sınanabiliyor — `_snd_vol3` ile aynı gerekçe.
`ses_yon_gucu3d(0.0)` kaydırmayı tamamen kapatır.

### Added — animasyon N klibe genelleşti

`anim3d` yalnız iki klip biliyordu (boşta + koşu). Çömelme, saldırı ya da
rastgele boşta klipleri için oyunun klibi doğrudan söyleyebilmesi gerekiyor.

"İki klip" aslında hiç özel değildi: harmanlama zaten (a, b, w) üçlüsüyle
çalışıyordu, özel olan yalnız HEDEFİ hesaplayan kuraldı. Şimdi hedef her kare
hesaplanıyor ve değişirse geçiş yeniden kuruluyor; otomatik locomotion bunun
bir özel hâli.

`anim_set3d(id, klip)` / `animasyon_sec3d` klibi oyun seçer, `anim_auto3d`
hıza geri bırakır, `anim_now3d` görünen klibi verir.

Geçiş makinesinin üç dalı da ayrı ayrı ölçülüyor, çünkü üçü de sessizce
yanlış olabilirdi:
- **Hedef zaten b ise dokunma.** Her kare sıfırlansaydı ağırlık asla
  ilerlemez ve sonuç tam olarak "harmanlama hiç çalışmıyor" gibi görünürdü.
- **Hedef a ise geçişi TERS çevir** (`w = 1-w`). Sıfırdan başlatmak,
  karakterin yarı yoldan önce tam koşuya gidip sonra dönmesi demek olurdu.
- **Üçüncü klipte BASKIN pozdan başla.** Üç yönlü harmanlama yok (tek ağırlık
  var), zayıf olandan başlamak görünür bir sıçrama olurdu.

Elle seçilen klip bir bayrakla korunuyor: otomatik kip her kare hedefi
yeniden yazdığı için, bayrak olmasa `anim_set3d` bir sonraki karede yok
olurdu.

Süitteki eski test AĞIRLIK üzerinden iddia kuruyordu; artık GÖRÜNEN KLİP
üzerinden kuruyor. N klip makinesinde ağırlık geçiş bitince sıfıra dönüyor,
yani "w == 1" iddiası makinenin iç detayına bağlı olur ve doğru davranışta
bile kırmızıya dönerdi.

### Added — animasyon harmanlaması: boşta↔koşu artık sıçramıyor

`anim3d` iki klip arasında TEK KAREDE geçiyordu; karakter yürümeye başlayınca
ya da durunca poz zıplıyordu — animasyonun "geldiğini" belli eden, oyunu ucuz
gösteren şey buydu.

Kendi iskelet kodumuz yok. raylib'in `UpdateModelAnimation`'ı pozu
`anim.framePoses[frame]` üzerinden okuyor; `tm3_anim_blend` tek karelik geçici
bir `ModelAnimation` kurup framePoses'ini harmanlanmış diziye bakacak şekilde
ayarlıyor. Skinning, kemik matrisi kurulumu ve GPU yüklemesi raylib'in kendi
(test edilmiş) yolundan geçiyor. Dönüşte SLERP: bileşen bileşen lerp kısa
yoldan gitmez ve dördeyin boyunu bozar, yani kemik uzar/kısalır.

Ağırlığı MOTOR sürüyor — oyun kodu yalnız hangi klibin boşta hangisinin koşu
olduğunu söylüyor. Geçiş **doğrusal**, üstel yumuşatma değil: üstelde ağırlık
hedefe hiç varmıyor, yani "boşta" pozu sonsuza kadar biraz koşu taşıyor.
Doğrusal olan uca tam oturuyor ve süresi tahmin edilebilir (1/hız saniye,
`anim_gecis_hizi3d` ile ayarlanır).

Harmanlamanın hız eşiği tek-poz seçicisiyle AYNI ifadeden geliyor; ayrışsalar
ağırlık koşuya yürürken seçici boştayı gösterirdi. Test bunu ölçüyor.

`examples/scene3d_karakter.tpr` — robot.glb kullanan ilk scene3d örneği.
Model bulunamazsa örnek yine de açılıyor (kutu oyuncuyla), çünkü eksik varlık
siyah ekran değil söylenen bir durum olmalı.

### Fixed — bayat `wasm/dist` / `android/dist` arşivleri artık önceden söyleniyor

Bu arşivler elle derleniyor (emsdk/NDK her makinede yok, CI'da hiç yok).
`tame_impl.c`'ye yeni bir sembol eklenip arşiv tazelenmeyince link
`undefined symbol: aot_tm3_...` diyor — sorunun ne olduğunu SÖYLEMEYEN bir
mesaj: eksik olan kodun kendisi değil, arşivin bayatlığı. Bu oturumda buna
iki kez daha çarpıldı.

Web ve Android link'inden önce arşiv zaman damgası `runtime/tame_impl.c`,
`runtime/tame_bindings.cpp`, `src/vm/runtime_bindings.cpp`, `src/vm/vm.cpp`
ile karşılaştırılıyor; eskiyse tazeleme komutunu söyleyen bir uyarı çıkıyor.
Uyarı, hata değil — bayat arşivde gereken semboller varsa link tutar ve
derlemeyi durdurmak gereksiz olurdu.

### Fixed — WEB'DE KAYIT SESSİZCE KAYBOLUYORDU

`kayit_yaz()` (ve onun üstündeki her şey: skorlar, `scene_save3d`) web'de
emscripten'in MEMFS'ine yazıyordu. MEMFS SAYFA ÖMÜRLÜDÜR: sekme yenilenince
yazılan her şey yok oluyor — ama çağrı `true` dönüyordu, yani oyun "kaydettim"
diyor, kullanıcı yenileyince ilerlemesi gitmiş oluyordu. Sessiz veri kaybı.

Ölçüldü (headless Chrome, aynı profille üç ardışık yükleme): düzeltmeden önce
her yüklemede okunan değer boş; sonra ikinci ve üçüncü yükleme yazılan değeri
geri veriyor.

Web'de artık localStorage kullanılıyor ("tulpar:" önekiyle, aynı kaynakta
duran başka uygulamalarla çarpışmasın diye). localStorage gizli sekmede ya da
site verisi kapalıyken ERİŞİMDE İSTİSNA atıyor — hepsi try/catch içinde:
kalıcılığın olmaması oyunun çökmesi anlamına gelmemeli.

Bu **bütün web oyunlarını** ilgilendiriyor, yalnız 3B editörü değil.

### Added — 3B EDİTÖR TARAYICIDA çalışıyor

`tulpar build --target=web examples/scene3d_editor.tpr`. Üç engel vardı:

**Font.** Web'de sistem font dizini yok, editör bitmap fonta düşüyordu (tam da
"okunması kolay değil" denen hâl). Aday listesinin başına varlık-göreli adlar
girdi — `assets/ui.ttf`, `fonts/ui.ttf`, `ui.ttf` — yani `TULPAR_WEB_ASSETS`
ile paketlenen font bulunuyor. Masaüstünde bu adlar normalde yok ve
`file_exists` koruyor, davranış değişmiyor.

**Dosya.** `scene_save3d`/`scene_file3d` web'de tarayıcının kalıcı deposuna
gidiyor ("sahne:<yol>" anahtarı; yol anlamlı kalıyor ki aynı sayfadaki iki
sahne birbirini ezmesin). Paketlenmiş dosya BAŞLANGIÇ hâli, kayıtlı iş onu
eziyor — yoksa her yenileme yapılanı geri alırdı.

Depo sayfanın İÇİNDE kalıyor, oysa asıl istenen sahneyi dışarı almak. Menü
şeridine yalnız web'de görünen **İNDİR** düğmesi eklendi (`scene_export3d()` /
`indir()`): web'de tarayıcı indirmesi, masaüstünde dosyaya yazma — oyun kodu
platform ayrımı yapmıyor. CDP ile doğrulandı: dosya adı ve içerik birebir.

**`args()`.** Tarayıcıda komut satırı yok; örnek web'de onu hiç yoklamıyor,
sabit bir ada düşüyor ve "depo kökünden çalıştır" mesajını vermiyor.

Yeni builtin'ler: `tm_download` (`download()`/`indir()`) ve `tm_is_web`
(`is_web()`/`web_mi()`).

**Görsel denetim yapılmadı** — pencere açılıyor ve GL bağlamı kuruluyor
(headless Chrome + swiftshader, abort yok), arayüzün görünüşü kullanıcının.

### Fixed — ayrılmış kelime PARAMETRE ADI olarak yanlış yeri gösteriyordu

`func indir(ad, metin)` — `metin` dilde `str` demek, yani ayrıştırıcı onu TİP
sanıp yutuyor ve hata bir sonraki sembolde ")" üzerinde "parametre adı
bekleniyordu" olarak patlıyordu. Suçlu kelime mesajda HİÇ geçmiyordu.

Değişken bildirimi için bu tanılama zaten vardı; parametre yolu ayrı olduğu
için kapsam dışıydı ve aynı tuzağa dördüncü kez düşüldü. Artık parametre
yerinde duran, tanımlayıcı olmayan ama harfle başlayan bir kelimeyi hemen
"," ya da ")" izliyorsa suçlu kelime adıyla söyleniyor. `build.sh suites`
ayrıca sınıyor.

### Fixed — küre ↔ DÖNÜK kutu çarpışması kutuyu eksen-hizalı sanıyordu

`set3yaw` ile döndürülmüş bir kutu, kutu-kutu çiftinde tam SAT'tan geçiyordu
ama küre-kutu çiftinde geçmiyordu: `_sph_box3` en yakın noktayı dünya
eksenlerinde kırpıyordu. Sonuç, döndürülmüş bir duvarın çarpışma kutusunun
görünen duvarla ayrışmasıydı — oyuncu duvarın içinden geçiyor ya da yanındaki
boşlukta duruyordu.

Küre merkezi artık kutunun yerel çerçevesine taşınıyor. Dönüş yönü `_seg_aabb3`
(ışın/kamera) ile AYNI ifadeden geliyor; ikisi ayrışırsa nişan aldığın yer ile
çarptığın yer farklı olurdu.

Testlerin kendisi bir kez zayıf çıktı: 90°'de kutu simetrik olduğu için dönüş
yönünü TERS çeviren bozma testlerden kaçtı. Testler 45°'ye taşındı — orada iki
köşegen ayrışıyor — ve beklenti koda değil, bağımsız yazılmış ışın testine
bağlandı (`_sph_box3` ile `_seg_aabb3` hemfikir olmak zorunda). Ayrıca iki
köşegenin gerçekten farklı cevap verdiği doğrulanıyor, yoksa test dönüşü hiç
sınamamış olurdu.

### Fixed — scene3d programları optimizasyonsuz derleniyordu

`[AOT] Warning: optimization produced invalid IR at every level` uyarısı
`scene3d` import eden HER programda çıkıyordu ve sonucu şuydu: 3B oyunlar
hiç optimize edilmeden derleniyordu. (Ölçüldü: tiny / tame / arcade /
02_basics temiz, yalnız scene3d kirli.)

Teşhis — LLVM 22'de iki ayrı kusur, ikisi de bizim IR'imizde değil (modül
optimizasyondan ÖNCE doğruluyor):
- **O2+**: `loop-idiom-recognize` yanlış mangle edilmiş bir `llvm.memset`
  üretiyor
- **O1**: `InstCombine.foldOpIntoPhi` boxed-karşılaştırma merge'ümüzden
  tipsiz bir phi çıkarıyor (kod içindeki eski yorum bu aileyi zaten tarif
  ediyordu)

Çözüm: geri düşüş merdivenine InstCombine ve döngü geçişleri olmayan
MUHAFAZAKÂR bir son basamak eklendi (`sroa, early-cse, simplifycfg,
reassociate, gvn, dce`). Ölçüm — hesap ağırlıklı 3B yük (60 düşman, 4000
kare fizik+çarpışma): **3690 ms → 3270 ms, ~%12**. LLVM yukarı sürümlerinde
kusurlar düzelirse merdiven O3'ü kendiliğinden seçer.

### Added — kod üretimi: sahne → okunabilir Tulpar kodu

`sahne_kod3d()` sahneyi Tulpar KODUNA çeviriyor. JSON çalışma zamanı biçimi;
kod ise öğrenme ve devam etme yolu — editörde kurduğun sahneyi açıp "bu böyle
yazılıyormuş" diyebilmek, sonra elle geliştirebilmek.

Üretilen şey tam bir program değil, tek bir FONKSİYON (`kur`). Sebebi
sınanabilirlik: aynı fonksiyon hem oyunun `kurulumda3` kancasına hem de "kur,
sonra sahneyi JSON olarak yazdır" biçimindeki bir doğrulayıcıya bağlanabiliyor.
Tam program üretseydik denkliği ölçmenin yolu olmazdı.

`examples/scene3d_export.tpr` aracı: `./tulpar examples/scene3d_export.tpr
sahne.json` kodu, `--dogrula` ise sahnenin JSON'unu veriyor.

**`build.sh suites` artık DENKLİĞİ ölçüyor:** üretilen kod derlenip
çalıştırılıyor ve kurduğu sahne yeniden serileştirilerek kaynakla
karşılaştırılıyor. Üretilen metni gözle okumak bu iddiayı doğrulamazdı — ilk
turda adların ve pencere bilgisinin taşınmadığı tam bu denetimle çıktı.

### Fixed — `tulpar script.tpr a b` argümanları programa iletmiyordu

`args()` eklendikten sonra ortaya çıkan tutarsızlık: `build` ile üretilen
ikili argümanları görüyordu ama `tulpar script.tpr ...` yolu görmüyordu.
Artık iletiliyor (kabuk alıntılaması tek tırnakla; boşluk ve `$` içeren
yollar bozulmuyor).

### Added — prosedürel bulutlar (`bulut3d`)

Gökyüzü bulutsuz bir degradeydi. Yıldızlarla aynı çözüm: gökyüzü kubbesinin
shader'ında üretiliyorlar — sıfır çizim çağrısı, sıfır asset ve örtüşme
kendiliğinden doğru (kubbe en arkada çizildiği için dağlar bulutları örtüyor;
2B çizilseydi dağların ÖNÜNE düşerlerdi).

Gürültü hash'lenmiş ızgara + üç oktav (fBm). Yön vektörü YATAY düzleme
izdüşürülüyor (`d.xz/d.y`), yani bulutlar sabit yükseklikte bir tabaka gibi
görünüyor ve ufka doğru sıkışıyorlar; düz `d.xz` onları kubbeye yapıştırırdı.
Zamanla sürükleniyorlar.

Gündüz-gece açıkken geceye doğru sönüyorlar — shader onları beyaza
karıştırıyor ve gece beyaz bulut yanlış görünürdü. Bu karar SAF bir
fonksiyonda (`_cloud_out3`) ki penceresiz sınanabilsin: yıldızlarda tam bu
sınanmadığı için bir bozma kaçmıştı.

### Added — suda kaldırma kuvveti (`yuz_davranis3d`)

Su fiziği cismi yavaşça BATIRIYORDU; kaldırma kuvveti yoktu, yani sandık ya
da varil gibi yüzen bir şey kurmanın yolu da yoktu.

Dünya ayarı değil DAVRANIŞ olarak eklendi, çünkü yüzüp yüzmemek cismin
özelliği: aynı suda taş batar, tahta yüzer. Dolayısıyla veri — serileşiyor,
editörden eklenebiliyor, üretilen koda giriyor.

Kaldırma yalnız cisim hedefin ALTINDAYKEN uygulanıyor. Yay gibi kurulsaydı
(üstte de uygulansaydı) havadaki cisim suya doğru emilirdi — yerçekimiyle
karışıp fark edilmesi zor bir hata; test bunu davranışsız bir ikizle
KARŞILAŞTIRARAK yakalıyor.

Ayrı bir sönümleme yok: su sürtünmesi zaten sönümlüyor. Ölçüldü — 900 karede
salınım genliği sönümlemeyle 1e-08, sönümlemesiz 1e-05 birim, ikisi de
görünmez. İkinci bir sönümleme yük taşımayan ama taşıyormuş gibi duran bir
satır olurdu.

### Added — `%` operatörü

`mod()` builtin'i vardı ama `%` yoktu: `7 % 3` sözcükleyici hatası veriyordu.
İkisi AYNI şeyi yapıyor — `%` yalnız her dilde beklenen, daha okunur yazım.
İşaret bölünenden geliyor (C ve çoğu dille aynı); farklı bir kural seçmek
`mod()` ile ayrışırdı. Önceliği `*` ve `/` ile aynı, ondalıkta `fmod`,
sıfıra bölmede bölmeyle aynı davranış.

### Added — `chr(c)` builtin'i

Dilde `ord` vardı ama tersi YOKTU. Bu bir asimetriydi ve pratik bir bedeli
oldu: metin girişi yazan kod (sahne editörünün metin alanı) Tulpar tarafında
bir ASCII tablosu taşımak zorunda kaldı. Artık builtin — tablo silindi.

Bayt düzeyinde, `ord` ile birebir eş (TulparLang dizeleri UTF-8 bayt dizisi
ve `length`/`substring` bayt tabanlı). 0-255 dışı boş dize döner; sessizce
çöp bayt üretmek metni bozardı.

### Added — prefab (şablon)

Bir varlığı adlı şablon olarak sakla, istediğin yere yerleştir. Şablon,
varlığın KENDİ SERİLEŞTİRMESİ olarak tutuluyor (`_sc_ent_js3`) ve yerleştirme
aynı yükleyiciden (`_sc_load_ent3`) geçiyor. Ayrı bir şablon biçimi yazmak,
varlık biçimi her büyüdüğünde ondan sapardı — davranışlar, can, katılık,
model hepsi bedava geliyor ve yeni alanlar kendiliğinden dahil oluyor.

`prefab_save3d` / `prefab_place3d` / `prefab_delete3d` (+ TR ikizleri),
sahne JSON'unda `"prefabs"`. Aynı ad ÜZERİNE yazılıyor; şablonun adı kopyaya
TAŞINMIYOR (iki varlığın aynı adı taşıması ad aramasını belirsiz yapardı).

Inspector'da: "seçiliyi ŞABLON yap" ve şablon listesi (tıkla → seçilinin
yanına yerleştir, x → sil).

### Added — çoklu seçim

CTRL+tık (görünümde ve hiyerarşide) kümeye ekliyor/çıkarıyor. Taşıma, silme
ve çoğaltma kümenin tamamına uygulanıyor.

`_ed_sel3e` BİRİNCİL seçim (inspector onu gösteriyor, tutamaklar onun
üstünde) ve küme birincili DE içeriyor — iki ayrı gerçek kaynağı tutmak
"silinen neydi?" sorusunu belirsiz yapardı. Birincil kümeden çıkarılırsa
yerine kümeden biri geçiyor.

Toplu taşımada birincilin DELTASI ötekilere uygulanıyor; hepsini aynı mutlak
konuma taşımak onları üst üste yığardı. Çoğaltmada kopyalar seçili kalıyor
(Unity ve Blender de böyle yapıyor). Ölen handle'lar her karede kümeden
düşürülüyor — tutulsalardı sonraki toplu işlem geçersiz handle'larla
çalışırdı.

### Added — tetikleyici bölgeler sahne biçiminde ve editörde

Bölgeler biçimde YOKTU: editörden eklenemiyor ve bir sahne dosyası "buraya
girince şu olsun" diyemiyordu. Artık `"zones"` alanı taşıyor — geometri,
etiket, tek-atım ve etkinlik. KANCALARI taşınmıyor (onlar kod), ama editörün
yeniden yüklemesinde indeksle korunuyorlar; korunmasaydı TAB'a basmak
"girince/çıkınca" bağlarını sessizce koparırdı.

Editörde: hiyerarşide varlıkların altında ayrı bir grup, kendi özellik paneli
(etiket, şekil, konum, boyut/yarıçap, tek-atım, etkin, sil) ve sağ tık
menüsünde "+ bölge". Bölgeler oyunda görünmez ama editörde görünür yapılıyor
— düzenlenecek bir şeyin görünmemesi onu düzenlenemez kılar.

Bölge silme bütün paralel dizileri birlikte kaydırıyor ve üyelik kayıtlarını
düzeltiyor; biri unutulsa bölge verileri birbirine kayardı.

### Fixed — DERLEYİCİ: kapsam değişken tablosu SESSİZCE taşıyordu

En üst kapsam programın BÜTÜN global değişkenlerini tutuyor ve tablo 256
kayıtlıktı; `scene3d` tek başına 353 tanesine sahip. Taşınca
`scope_decl_slot` -1 döndürüyor, değişken hiç kaydedilmiyor ve okuması çöp
veriyordu — **hiçbir hata mesajı yok, derleme başarılı görünüyor.** Yanlış
çalışan bir ikili üretiliyordu.

Editöre dokuz global eklemek sınırı aştırdı ve hata şu şekilde ortaya çıktı:
scene3d test süitinin özeti "Fail: 0" yerine **"Fail: nullptr"** yazdı —
`lib/test.tpr`'nin sayacı, geç kaydolduğu için düşen değişkenlerden biriydi.

Tablo 4096'ya çıkarıldı (kapsam `calloc` ile ayrılıyor, yığın riski yok) ve
taşma artık **sesli**: fonksiyon tablosundaki gibi ölümcül hata veriyor.
Sınır dizinin kendisinden türüyor — ayrı bir sabit tutmak ikisinin
ayrışmasına açıktı. Hata yolunun gerçekten ateşlendiği sınırı 64'e düşürerek
doğrulandı.

### Added — editörde sağ tık bağlam menüsü

Sağ tuş zaten kamera bakışı. Unity/Unreal/Godot'taki ayrım uygulandı: sağ
tuşla SÜRÜKLEMEK bakış, sürüklemeden BIRAKMAK bağlam menüsü (4 piksellik
eşik; eşiksiz menü her bakış hareketinin sonunda açılırdı).

Varlık üzerinde: Çerçevele / Çoğalt / Zemine otur / Sil. Boşlukta: kutu /
küre / boru / duvar ekle — eklenen şey İMLECİN altındaki zemine konuyor,
kameranın konumuna değil.

Menü açıkken görüntü penceresi tıklama almıyor (menüdeki seçim aynı anda
arkadaki sahnede de bir şey seçerdi) ve menü çakışma denetiminin dışında
tutuluyor (altındakileri kasıtlı örtüyor).

### Added — `args()`: komut satırı argümanları (dil) ve `./editor <dosya>`

Dilde argv erişimi HİÇ yoktu: Tulpar ile yazılmış bir CLI aracı hangi
dosyayla açıldığını öğrenemiyordu. Üretilen `main` artık `(argc, argv)`
alıyor — `int main()` de `int main(int, char**)` de geçerli C, yani imzayı
genişletmek çağrı yerlerini etkilemiyor — ve girişte runtime'a veriyor.

`args()[0]` programın kendi yolu; C'nin argv sözleşmesiyle aynı, çünkü başka
bir sözleşme uydurmak "neden kaydı?" sorusunu doğururdu.

Editör bunu kullanıyor: **`./editor benim_sahnem.json`**. Dosya YOKSA bu bir
hata değil — yeni sahne yaratmanın en doğal yolu (`./editor yeni.json` → boş
sahne, F5 orayı yazar). Editörün tek bir gömülü dosyayı açabilmesi onu kendi
iş için kullanılamaz yapıyordu.

### Fixed — hiyerarşi sayacı konsolun içine taşıyordu

Çakışma dedektörü ilk gerçek bulgusunu verdi: hiyerarşinin alt sayacı
("1-15 / 18") konsol panelinin içine yazıyor ve kırmızı çerçeveyle
işaretleniyordu. Sebep yine ölçeklenmemiş bir sabitti — sayaç için ayrılan
yükseklik 14 piksel, ama 2x'te metin 20 piksel. Konum artık satır
yüksekliğinden türüyor ve "metnin altı paneli aşmıyor" değişmezi her ölçekte
sınanıyor.

Konsol yüksekliği de satır SAYISINDAN türüyor: sabit tabanla 3x ölçekte tek
satır gösteriyordu ve tek satırlık konsol işe yaramıyor.

### Fixed — iç içe geçmiş yazılar: satır aralığı ölçekle büyümüyordu

Çakışma dedektörü hiçbir şey bildirmiyordu ama ekrandaki yazılar iç içe
görünüyordu. İki sebep vardı ve ikisi de aynı kör noktadan geliyordu.

**Satır aralığı sabitti.** Panel içindeki dikey ölçüler 1x tasarımının
değerleriydi (`y + 3`, `r * 12`) ve ölçeğe bağlanmamıştı. 2x'te font 20
piksele çıktı ama konsolun satır aralığı 12 kaldı — yani ARDIŞIK SATIRLAR 8
PİKSEL ÜST ÜSTE BİNDİ. Satır yüksekliği ve dikey merkez artık ölçekten
türüyor (`_g_lh3` / `_g_cy3` / `_g_ly3`), ve bir test her ölçekte satır
aralığının fonttan büyük olduğunu sınıyor.

**Dedektör metinleri görmüyordu.** Yalnız widget kutuları kaydediliyordu;
kutular çakışmadığı için denetim sessiz kalıyordu. Artık düz metinler de
kaydediliyor. Widget'ın KENDİ etiketi kutusunun içinde durduğu için "içeren"
durum çakışma sayılmıyor — saymak dedektörü her karede yüzlerce sahte
uyarıyla kullanılamaz yapardı.

Tespit ile raporlama ayrıldı (`_g_count_overlaps3(report)`): tespit saf, yani
penceresiz sınanabiliyor. Birleşik olduklarında dedektörün kendisi test
edilemiyordu — bu oturumda "sınanamayan yerdeki hata" defalarca tekrarlandı.

### Fixed — arayüz çakışması: artık arayüzün KENDİSİ denetliyor

Editörde bazı düğmeler hâlâ üst üste geliyordu. Tek tek yerleşim formülü
sınamak bunu garanti etmiyor: her yeni widget yeni bir formül, ve içerik
(metin genişliği, yazı tipi, ölçek, pencere boyutu) değiştikçe eski formüller
kayıyor. Gerçek font + 2x ölçek tam olarak bunu yaptı.

**Dedektör.** Her widget dikdörtgenini kaydediyor; kare sonunda ikili kesişim
aranıyor. Çakışma varsa suçlu dikdörtgenler KIRMIZI çerçeveyle gösteriliyor ve
konsola widget numaralarıyla yazılıyor. Kenar teması çakışma sayılmıyor — yan
yana dizilmiş düğmeler tam bitişik olabiliyor.

**Araç çubuğu yeniden yazıldı.** Orta grup (OYNAT/DUR) doğrudan `W/2`'ye
konuyordu ve solun nerede bittiğine BAKMIYORDU. Artık önce ölçülüyor sonra
yerleştiriliyor: orta grup ortalanıyor ama sol grubun bitişinin gerisine
düşemiyor. Izgara ve UI düğmeleri yalnız orta gruba çarpmadan sığıyorsa
çiziliyor — bir editörde ızgara düğmesinin kaybolması, üst üste binmiş iki
düğmeden iyidir. Durum metni de orta gruptan sonra yer kalırsa çiziliyor.

**Menü şeridi** aynı yaklaşımla: her düğme çizilmeden önce sığdığı
denetleniyor.

Ölçekle büyümeyen sabit boşluklar (`2`, `10`, `16`) da ölçeğe bağlandı.

### Changed — editör gerçek bir yazı tipi kullanıyor

raylib'in varsayılan fontu 10 piksel tabanlı bir BITMAP: ölçeklenince
pikselleşiyor ve uzun süre bakılan bir editör arayüzünde yorucu. Editör artık
sistemden bir TTF yüklüyor (Noto Sans → DejaVu Sans → Liberation Sans →
Adwaita Sans sırasıyla; `editor_yazitipi3d(yol)` ile kendi fontun).

Font depoya GÖMÜLMÜYOR: iyi bir UI fontu ~600 KB ve ikiliye gömmek onu editör
kullanmayan her oyuna da taşırdı. Bulunamazsa varsayılan fonta düşülüyor ve
konsolda söyleniyor — sessizce çirkinleşmek "editör neden böyle?" sorusunu
cevapsız bırakırdı.

Çizim ve ölçüm TEK kapıdan geçiyor (`_g_draw3` / `_g_w3`): ikisinin ayrı
yollardan gitmesi, ölçümün çizimden sapmasına ve yerleşimin sessizce kaymasına
yol açardı — editörün bütün sütun genişlikleri ölçülen metne dayanıyor. Yeni
`tm_font_width` bağlaması çizimle aynı harf aralığını kullanıyor.

Ölçek değişince font hedef boyda YENİDEN yükleniyor: TTF rasterize edildiği
boyda net, eskisini ölçeklemek bulanıklaştırırdı.

### Changed — editör UX'i sektör standardına çekildi

**Kısayollar.** WASD/QE artık yalnız SAĞ TUŞ BASILIYKEN uçuruyor — Unity,
Unreal ve Godot'ta ayrım tam budur. Sürekli uçuş harf tuşlarını rehin
alıyordu: W aynı anda "ileri uç" demek olduğu için standart araç kısayolları
imkânsızdı. Artık Q/W/E/R seç/taşı/döndür/ölçekle (1-4 de duruyor),
CTRL+S kaydet, CTRL+Z/Y geri/ileri, G ızgara, DEL/CTRL+D sil/çoğalt.

**F ile çerçeveleme.** Editörlerin en çok kullanılan tuşlarından biriydi ve
yoktu: uzaktaki bir varlığı seçtikten sonra oraya elle uçmak gerekiyordu.
Mesafe cismin BOYUTUNDAN türüyor (sabit mesafe küçüğü görünmez, büyüğü kadraj
dışı bırakırdı) ve kullanıcının kurduğu bakış AÇISI korunuyor.

**Üzerine gelince vurgulama.** İmlecin altındaki varlık soluk bir çerçeveyle
gösteriliyor; tıklamadan önce neyi seçeceğini bilmek her editörde var ve
yokluğu "tıkladım ama yanlış şey seçildi" hissi veriyordu.

**Kaydedilmemiş değişiklik.** Menü şeridinde dosya adının önünde yıldız.
Editörde en sinsi kayıp "kaydettim sanmak"tı. Ayrıca "Yeni" artık yıkıcı
olduğunu biliyor: kaydedilmemiş sahnede düğme kendisi "EMIN MISIN?"e
dönüşüyor (modal pencere makinesi kurmadan ikinci bir karar noktası).

**Yön göstergesi.** Sahne görünümünün köşesinde X/Y/Z okları. Serbest uçan
bir kamerada "hangi yöne bakıyorum?" sürekli sorulan soru. İzdüşüm
ortografik — gösterge yönü anlatıyor, konumu değil.

**Araç ipuçları.** Kısaltılmış etiketler ("kure", "toh", "arlk") tek başına
anlaşılmıyordu; üzerine gelince ne olduğunu ve kısayolunu söyleyen bir kutu
çıkıyor. Widget imzaları değişmedi — ipucu çağrıdan SONRA soruluyor, çünkü
`_g_hot3` zaten imlecin altındaki widget'ı tutuyor.

**Hiyerarşi araması.** Ada ya da etikete göre süzme; sahne büyüdükçe listede
gezinmek imkânsızlaşıyordu. Eşleşme büyük/küçük harf duyarsız.

**Durum çubuğu** seçili varlığın adını ve konumunu gösteriyor. Silindir
ekleme düğmesinin etiketi "sil." idi ve "sil" gibi okunuyordu — "boru" oldu.

### Changed — arayüz ölçeklenebilir oldu (varsayılan 2x)

Arayüz elemanları çok küçüktü. Yazı tipi yalnız 10/20/30 puntoda net olduğu
için ara boy sunulamıyor (aradaki her punto harfleri birbirine sokuyor); onun
yerine **bütün arayüz** aynı katsayıyla ölçekleniyor: 1x / 2x / 3x, varsayılan
**2x**. Araç çubuğundaki "UI 2x" düğmesi döndürüyor.

Yalnız puntoyu büyütmek yetmezdi: sütun genişlikleri panel genişliğinden
türüyor, yani metin panelden taşardı. Paneller de ölçekle büyüyor — ama DOĞRU
ORANTILI değil (3x'te ikisi birden editör penceresinin yarısını yerdi):
sabit taban + ölçek payı. Bir test her üç ölçekte de sütunların panele
sığdığını, sayı alanlarının okunabilir kaldığını ve şerit/panel toplamlarının
pencerenin yarısını geçmediğini sınıyor.

### Added — kural düzenleyici ve panel kaydırma

**Kurallar artık editörden kurulabiliyor** (DÜNYA panelinin altında): çarpışma
kuralı (etiket + etiket → eylem + miktar) ve "bitince" kuralı. Eylemler
topla/hasar/öldür/vur/kazan/kaybet arasında dönüyor. Kurallar sahneyi OYUN
yapan şey; şimdiye kadar yalnız JSON'a elle yazılabiliyordu, yani editörde
kurulan bir sahne oynanabilir bir oyun olamıyordu.

**Inspector kaydırılabiliyor** ve panele KIRPILIYOR. İçerik panelden uzun
olabiliyor (çok davranışlı varlık, dünya ayarları + kurallar); kırpma olmadan
taşan satırlar alttaki konsolun üstüne çiziliyordu. İnce bir kaydırma çubuğu
nerede olunduğunu gösteriyor.

Yeni tame bağlamaları (5 nokta, denetim temiz): `tm_scissor`,
`tm_scissor_end`. Alternatif her widget çağrısından önce elle sınır denetimi
yapmaktı — her yeni widget o borcu büyütür ve yarı görünür satırlar yine
kırpılamazdı.

### Changed — DÜNYA hiyerarşide bir nesne; oynarken fare oyuna geçiyor

**Dünya artık hiyerarşinin ilk satırı** ve bir nesne gibi seçiliyor (Godot'nun
WorldEnvironment'ı ile aynı fikir). Yerçekimi, gökyüzü, sis, su, arazi ve
gündüz-gece de sahnenin parçası; onları ayrı bir sekmede saklamak "sahnede ne
var?" sorusunun cevabını eksik bırakıyordu. Inspector'daki sekme düğmeleri
kaldırıldı — iki ayrı seçim yolu (liste + sekme) tutmak hangisinin geçerli
olduğu konusunda sürekli bir tutarsızlık kaynağı olurdu.

**İmleç:** düzenlerken serbest (panellere tıklanacak), OYNARKEN yakalı (oyun
normal davranmalı, fare bakışı çalışmalı). Editörde **ESC oyunu DURDURUR** ve
düzenlemeye döner — duraklat menüsü açmaz. Önce yalnız imleci serbest
bırakıyordu; fare geri geliyor ama simülasyon sürüyordu ve bu yarım bir
davranıştı. Durdurmak hem beklenen hem daha az durumlu: düzenlemeye dönünce
imleç zaten serbest kalıyor.

### Added — editörde dünya ayarları ve varlık adı

Inspector artık iki sekmeli: **VARLIK** ve **DÜNYA**. Dünya sekmesinde
gökyüzü (aç/kapa + üst/alt renk), sis, zemin, yerçekimi, ışık/gölge, su
(seviye/renk/alfa), arazi (tepe/tohum + yeniden üret + doğal boya),
gündüz-gece (saat + gün uzunluğu) ve eğim sınırı düzenlenebiliyor. Bunların
hepsi sahne biçiminde zaten taşınıyordu; eksik olan tek şey editörden
değiştirilebilmeleriydi — elle JSON yazmak gerekiyordu.

Varlık **adı** da düzenlenebiliyor (yeni metin alanı widget'ı). Ad önemli:
kamera hedefi ve kurallar varlıklara ADLA atıfta bulunuyor, yani adsız bir
varlık sahne dosyasında hedeflenemiyor. `rename3d`/`ad_ver3d` eski kaydı
düşürüyor — aynı handle için iki ad kalsaydı serileştirme eskisini yazardı.

### Fixed — gölge geçişi render texture'ı bozuyordu (sahne görünümü boş kalıyordu)

Editörün sahne görünümü paneli koyu mavi bir dikdörtgen olarak kalıyordu ve
sahne panellerin arkasında çiziliyordu. Sebep tek bir satırdı: gölge geçişi
kendi framebuffer'ına geçip işi bitince `rlDisableFramebuffer()` çağırıyor,
yani VARSAYILAN framebuffer'a — ekrana — dönüyordu. Editör sahneyi bir render
texture'a çizdiği için gölge geçişinden SONRAKİ her şey ekrana gidiyor,
dokuda yalnız temizleme rengi kalıyordu. "Koyu mavi" tam olarak o temizleme
rengiydi.

Gölge geçişi artık ÖNCEKİ hedefe dönüyor (`tame_restore_target`): render
texture etkinse ona, değilse ekrana. Aynı düzeltme framebuffer kurulumundaki
dönüşe de uygulandı.

Bununla birlikte `screen_width()`/`screen_height()` artık ETKİN HEDEFİN
ölçüsünü veriyor. Render texture'a çizerken pencere boyutunu döndürmek,
hedefe göre yerleşen her şeyi (sağa/alta yaslı HUD) dokunun dışına atıyordu.

### Fixed — sahne görünümü boş görünüyor, sahne editörün arkasına taşıyordu

Üç ayrı kusur aynı belirtiyi üretiyordu.

**Sessiz geri düşüş.** Render texture kurulamazsa sahne TAM EKRANA çiziliyordu
ve paneller onun üstüne biniyordu — yani hata tam olarak "görünüm paneli boş,
sahne editörün arkasında" gibi görünüyordu. Artık öyle bir durumda 3B çizim
atlanıyor ve görünüm panelinde sebebi yazıyor. Sessizce yanlış çizmektense
görünür şekilde başarısız olmak.

**Boş sahne sessizdi.** Sahne dosyası bulunamazsa editör boş bir dünya açıp
yalnız konsola yazıyordu; ekranda hiçbir işaret yoktu. Artık görünüm panelinde
"SAHNE BOŞ" ve ne yapılacağı yazıyor. Editör ayrıca sahne dosyasını birkaç
aday yolda arıyor, yani depo kökü dışından çalıştırılınca da buluyor.

**Oyunun kendi HUD'ı çağrılmıyordu.** Çizim bloğunu editör için yeniden
düzenlerken `call(_hud3_fn)` satırı düştü; oyunların kendi HUD'ı hiç
çizilmiyordu. Geri kondu.

Durum çubuğu artık görünüm dikdörtgenini ve render hedefi tanıtıcısını
gösteriyor — bu sınıftan bir sorun bir daha çıkarsa ekrandan teşhis edilebilir.

### Fixed — editörde fare imleci kayboluyordu

Editör sahneyi yörünge kamerasıyla açıyor, o da fare bakışını yani İMLEÇ
KİLİDİNİ istiyor, kilit de imleci gizliyor. Sonuç: editöre girince imleç
kayboluyor ve panellere tıklanamıyordu.

Kilit kararı döngünün içine gömülüydü, dolayısıyla penceresiz sınanamıyordu.
Saf bir fonksiyona (`_s3_want_lock3`) alındı ve editör kabuğu açıkken —
oynarken bile — hiç kilitlenmiyor. Oyun içi fare bakışı o hâlde sağ tuş
sürüklemesine düşüyor; alternatifi "DUR düğmesine ulaşamamak" olurdu.

### Changed — editör artık BAĞIMSIZ UYGULAMA (TAB kaplaması kaldırıldı)

Editör, çalışan oyunun üstüne TAB ile açılan bir kaplamaydı ve bu sektör
standardı değildi: Unity/Unreal/Godot'ta editör UYGULAMADIR, oyun onun
içindeki bir panelde koşar. Geçiş de garip duruyordu. TAB yolu kaldırıldı.

`examples/scene3d_editor.tpr` (derlenmiş hâli `./editor`) editörü açıyor:
menü şeridi (Yeni/Aç/Kaydet/Geri/İleri), araç çubuğu (araçlar solda,
**OYNAT–DUR ortada** — Unity'nin imzası), solda hiyerarşi, ortada SAHNE
GÖRÜNÜMÜ, sağda özellikler, altta konsol ve durum çubuğu.

Sahne görünümü artık bir **render texture**: 3B sahne ekrana değil dokuya
çiziliyor ve panele yerleştiriliyor. Doğrudan çizip kırpmak (scissor)
yetmezdi — kamera izdüşümü pencerenin en-boy oranını kullanır, görüntü ezik
ve merkezi kaymış çıkardı. Dokuya çizince raylib izdüşümü hedefin
boyutundan türetiyor. Seçim ışını da görünüm dikdörtgenine göre kuruluyor.

OYNAT ekrandaki düzenlenmiş sahneyi yazarlık durumu olarak sabitleyip
KOPYASINI koşturuyor; DUR kopyayı atıyor. Oyunun kendi HUD'ı yalnız
oynarken ve sahne görünümünün içinde çiziliyor.

Yeni tame bağlamaları (5 nokta, denetim temiz): `tm_rt_new`, `tm_rt_free`,
`tm_rt_w`, `tm_rt_h`, `tm_rt_begin`, `tm_rt_end`, `tm_rt_draw`.

### Fixed — editör arayüzü baştan yazıldı: yazı tipi ve taşan yerleşim

Kullanıcı ekranda "yazılar üst üste gelmiş gibi" gördü ve haklıydı. İki ayrı
sebep vardı, ikisi de ölçülerek doğrulandı.

**Yazı tipi.** raylib'in varsayılan fontu 10 piksel tabanlı bir bitmap ve
`DrawText` içinde harf aralığı `fontSize/10` ile TAMSAYI, glif ölçeği ise
`fontSize/10.0` ile KESİRLİ bölmeden geliyor. Yani 13 punto istediğinde
glifler 1.3 kat büyüyor ama aralık 1 pikselde kalıyor — harfler birbirine
giriyor. İlk sürüm 12/13/14/15 kullanıyordu. Arayüz artık baştan sona 10
punto; başlıklar puntoyla değil RENK ve zemin şeridiyle ayrılıyor. Bir test
puntonun 10/20/30 dışına çıkmasını engelliyor.

**Yerleşim.** Sütun genişlikleri elle yazılmış mutlak sayılardı ve üç yerde
taşıyordu (960 piksellik pencerede ölçüldü): alt yardım satırı 434 piksel,
araç çubuğu durum metni KAYDET düğmesinin üstüne, davranış satırı inspector
panelinin 6 piksel dışına. Artık her konum panel dikdörtgeninden türüyor,
metinler `text_width` ile ÖLÇÜLEREK yerleştiriliyor ve sığmayan metin
kesiliyor. Araç çubuğunun durum metni yalnız yer kalırsa çiziliyor.

Yerleşim aritmetiği parametreli fonksiyonlara alındı, böylece penceresiz
sınanabiliyor — metin genişliği ölçülemiyor (MeasureText pencere istiyor) ama
taşmanın kaynağı ölçüm değil aritmetikti.

### Fixed — editör denetimi: sahne yüklemesi geri koyamayacağı şeyi siliyordu

Bir alt-ajan denetimi editörde bir dizi hata buldu; en ciddisi sessiz durum
kaybıydı ve ölçülerek doğrulandı.

**Yükleme artık tam teardown yapmıyor.** `scene_load3d` `scene3d_reset()`
çağırıyordu, yani sahne JSON'unda KARŞILIĞI OLMAYAN her şeyi de siliyordu:
tetikleyici bölgeler, kalıcılık (rekor), nişan modu, kamera ayarları. Ölçüm:
editöre girip çıkmak bölge sayısını 1'den 0'a, kayıt durumunu 1'den 0'a,
nişan modunu AIM_LOOK'tan AIM_FLAT'e düşürüyordu — ve aynı yol her Ctrl+Z'de
işliyordu. Düzeltme tek tek "şunu da koru" listesi DEĞİL (o liste motora
eklenen her yeni durumla sessizce eskirdi); yükleme artık yalnız GERİ
YÜKLEYEBİLDİĞİNİ temizliyor (`_sc_clear_for_load3`).

**SHAPE_MODEL varlıklar ilk TAB'da gri kutuya dönüyordu:** yazıcı "model"
üretiyor, okuyucu o dalı tanımayıp SHAPE_CUBE'e düşürüyordu. Model ve
animasyon tanıtıcıları artık taşınıyor; `ed_duplicate3d` de onları kopyalıyor.

**Diğerleri:** editörde ESC/BACKSPACE artık duraklat menüsü açmıyor (sayı
alanında bir rakam silmek örtüyü açıyordu); `_g_focus3` kip geçişinde
temizleniyor (takılırsa editörün tüm klavyesi ölüyordu); sürüklemede KAVRAMA
OFSETİ saklanıyor (uzun bir duvarın ucuna tıklamak onu merkezi imlece
gelecek şekilde ışınlıyordu); geri-al işareti yalnız gerçek bir jest
başlarken konuyor (boş tıklamalar 40'lık geçmişi yiyordu); fare tekerleği
panel üstündeyken kamerayı sürmüyor (liste kaydırmak aynı anda kamerayı da
kaydırıyordu); widget kimlik aralıkları ayrıldı; davranış alanları artık
TÜRE GÖRE gösteriliyor (serileştirilmeyen alanlar düzenlenebiliyor ama ilk
kayıtta sessizce geri dönüyordu).

### Added — eksen tutamakları (gizmo)

Seçili varlıkta üç ok: X kırmızı, Y yeşil, Z mavi. Bir oka tutununca hareket
(ya da ölçek) O EKSENDE kalıyor. Buna kadar sürükleme yalnız YATAY düzlemdeydi:
yüksekliği fareyle değiştirmenin yolu yoktu ve hareket tek eksene
kilitlenemiyordu.

Matematik saf ve pencere istemiyor: ışın ile eksen DOĞRUSU arasındaki en yakın
yaklaşım kapalı formda. Ekran uzayında çizip piksel mesafesine bakmak kamera
açısına göre bozulurdu — tepeden bakınca dikey ok bir noktaya çöker ve
tıklanamaz olurdu.

Tutamak boyu EKRANDA sabit (kamera mesafesiyle ölçekleniyor, alt/üst sınırlı):
dünya birimi sabit olsaydı uzaktaki varlığın okları görünmez, yakındakinin
okları ekranı kaplardı.

Üç ok merkezde kesiştiği için "ilk eşleşeni al" kamera açısına göre YANLIŞ
ekseni verirdi; en yakın olan seçiliyor.

### Added — tam editör arayüzü (Unity/Unreal düzeni)

Araç çubuğu (Oynat, kip seçimi, ızgara, geri/ileri, kaydet), hiyerarşi paneli
(canlı varlık listesi, kaydırma, hızlı ekleme düğmeleri) ve inspector (etiket,
şekil, konum/boyut/yaw, renk, katılık, can, davranış listesi ve ekle/sil).

Arayüz "anlık kip" (immediate mode): kalıcı widget nesnesi yok, her kare
yeniden çiziliyor ve tıklama aynı çağrıda dönüyor. Saklanan tek şey odak ve
sürükleme gibi JESTLER — onlar zaten kareler arası. Widget kimliği çağıran
tarafından veriliyor; otomatik sayaç, liste uzunluğu değişince kimlikleri
kaydırır ve yanlış alan odakta kalırdı.

Sayı alanları Unity'deki gibi çift işlevli: SÜRÜKLE değeri tarar, TIKLA yazmaya
geçer. İkisi tek alanda olmazsa ya ince ayar ya hızlı tarama kaybolur.

Fare panellerin üstündeyken görüntü penceresi seçim ışınını atlıyor, ve bir
sayı alanına yazılırken klavye kısayolları kapanıyor ("3" yazmak kamerayı
uçurmamalı, "d" varlığı çoğaltmamalı).

Yeni tame bağlamaları: `tm_text_width` (MeasureText) ve `tm_char_pressed`
(GetCharPressed) — panel yerleşimi ve metin girişi için.

### Fixed — AOT fonksiyon tablosu 1024'te doluyordu

Editör katmanı eklenince tek bir program sınırı aştı ve derleme "function
table overflow" ile durdu. Tablo 4096'ya çıkarıldı. Sınır artık DİZİDEN
türetiliyor: ayrı bir `kMaxFunctions` sabiti tutmak, biri büyütülüp diğeri
unutulduğunda ya erken hata ya sessiz taşma demekti.

### Added — editör kipi: sahneyi gözle kurmak

`TAB` ile açılıyor ve oyunu DONDURUYOR (editör bir oyun kipi değil; fizik
çalışırsa düzenlediğin şey ayağının altından kayar). Serbest uçuş kamerası,
fareyle varlık seçme, ızgaraya oturan sürükleme, ölçek/döndürme,
sil/çoğalt/zemine-otur, ve `F5` ile sahneyi JSON dosyasına geri yazma —
yeniden derleme yok.

Katman ikiye ayrık: SAF matematik (ışın kurma, seçim, ızgara, düzlem
kesişimi) ve KOMUTLAR (taşı/ölçekle/döndür/sil/çoğalt). Girdi işleme yalnız
ince bir kabuk, çünkü tarayıcıdaki editör (Faz 5) aynı komutları çağıracak.

Işın testlerinin beklentisi kodun formülünden değil **fov'un tanımından**
geliyor: ekranın dikey kenarından geçen ışın ileri yönle tam fov/2 açı yapar.
Aynı formülü tekrarlayan bir test aynı yanlışı onaylardı.

Seçim, kamera-engel taramasının kullandığı `_seg_aabb3`'ü çağırıyor — ayrı
bir ışın-kutu testi yazmak aynı geometriyi iki yerde tutmak olurdu. Dönük
kutuların doğru seçilmesi bu yüzden bedava geldi.

### Fixed — katı-katı temas kancaları hiç atmıyordu

`_s3_collision` önce MTV ile cisimleri TAM ayırıyor, kanca taraması ise ondan
SONRA çakışma arıyordu — ayrılmış iki cisim tam teğet duruyor, yani çakışma
yok. Sonuç: `carpisinca3(TAG_ENEMY, TAG_PLAYER, ...)` kaydeden her oyun o
kancayı hiçbir zaman çağırmıyordu. Gönderilen `scene3d_arena` örneği dahil:
düşmanlar dokununca hasar vermiyordu, ve kodun kendi yorumu tersini söylüyordu.
Hata SESSİZDİ çünkü katı-KATISIZ çiftler (eşya, mermi) ayrılmadıkları için
çalışıyordu.

Düzeltme fizik motorlarının standardı: temas taraması cisimleri küçük bir payla
(6 cm) şişirerek bakıyor. Pay AABB/küre/silindir/SAT ayrımını bilmiyor —
boyutlar geçici olarak büyütülüp AYNI `_overlap3` çağrılıyor, çünkü şekil
matematiği ikinci bir yerde tekrarlansaydı er geç ondan sapardı.


### Added — 3B tetikleyici bölge: "buraya girince şu olsun"

`scene3d`'de kapı, kontrol noktası, tuzak, bonus pedi, boss arenası yoktu; hepsi
`solid3d(id, false)` + `carpisinca3` ile **taklit** ediliyordu. O taklidin iki
kusuru vardı ve bu katmanın var olma sebebi ikisi:

1. **Kanca çakışılan her karede çağrılıyordu** — saniyede 60 kez. İstenen ise
   "girince bir kez"; her oyun bu bayrağı kendi elinde tutmak zorundaydı.
2. **Bölge bir entity oluyordu**: çiziliyor, slot yiyor, geniş faza ve MTV
   çözümüne giriyor, `alive_count3d()` sayıyordu. Görünmez bir işaret için
   fazla bedel.

Bölgeler artık entity **değil**, ayrı düz dizilerde duruyor ve kare başına
giriş/çıkış **kenarı** hesaplanıyor.

- `trigger3d(x,y,z, sx,sy,sz, tag)` / `bolge3d` — kutu bölge (entity ile aynı
  "tam boy" sözleşmesi). `trigger_sphere3d(x,y,z, r, tag)` / `bolge_kure3d` —
  küre bölge; "şunun N birim yakınına gelince" için kutu kurmak tuhaftı.
- Üç kanca: `on_enter3d`/`girince3d` (bir kez), `on_exit3d`/`cikinca3d` (bir
  kez), `on_stay3d`/`icindeyken3d` (her kare — eski taklidin davranışı, artık
  *seçilerek*). Kanca içinde `ben3()` giren entity'yi, `trigger_id3d()` /
  `bolge_no3d()` hangi bölgenin tetiklediğini söyler; `oteki3()` **-1**, çünkü
  bölge entity değil ve önceki çarpışmadan kalan değeri bırakmak sessiz hataya
  davetiye olurdu.
- `trigger_once3d`/`bolge_bir_kere3d` — tek atım (kontrol noktası, bir kere
  anlatılan mesaj). **Tek gerçekten tek**: bölgede aynı anda iki gövde varsa
  bile giriş bir kez atar, çünkü tarama her entity için `_trg_on` bayrağını
  yeniden okuyor.
- `trigger_on3d`/`bolge_etkin3d`, `trigger_move3d`/`bolge_tasi3d` (asansör
  güvertesi, hareketli tuzak, aura), `inside3d`/`icinde3d`,
  `trigger_count3d`/`bolge_sayisi3d`, `trigger_show3d`/`bolge_goster3d`
  (tel kafes ayıklama çizimi — bölgeler normalde görünmez, bütün mesele o).
- **Kapatmak üyeliği de düşürür ve çıkış atmaz.** Kapalı bölge olay üretmez;
  aksi hâlde tek atımlık bir bölge arkasında sahte bir "çıkış" bırakırdı.
- **İçeride ölen entity için çıkış atılmaz**, üyeliği sessizce düşer: handle
  zaten geçersiz, kanca onunla bir şey yapamazdı. Ölümü izlemek `olunce3d`'nin
  işi.
- **Bölgeler bölümle birlikte gider.** Bölge *yerleştirilmiş geometridir* (kapı,
  tuzak); `carpisinca3`/`olunce3d` ise *kuraldır* ve bölümü aşar. Ayrım bilerek.
- Sıralama: bölgeler fizik ve çarpışmadan **sonra** (entity duvara itilmiş,
  nihai konumda), bölüm geçişinden **önce** (bir bölge kancası `bolum_gec3d()`
  çağırabilsin).
- `examples/scene3d_arena.tpr` üç kancayı da kullanıyor: bölüm başına tek atımlık
  **bonus pedi** (kutu, +50 skor + `iyilestir3d`), **zehir havuzu** (küre, her
  kare `hasar3d` — dokunulmazlık penceresi ısırığı kendisi sınırlıyor) ve
  havuzdan **çıkınca** kıvılcım + HUD göstergesinin sönmesi.
- 12 yeni regresyon testi (`tests/scene3d_engine.test.tpr`, 80 → 93). Hepsi
  bozma enjekte edilerek doğrulandı: kenar tespiti, etiket süzgeci, küre
  bölgenin *gövde* ölçmesi, ölüm-çıkış bastırması, bölüm temizliği, kapatma
  sessizliği ve iki gövdeli tek atım — her biri ilgili bozmada kırmızıya döndü.

### Added — gökyüzünde prosedürel yıldızlar

Gökyüzü düz bir degradeydi; gündüz-gece döngüsü geldikten sonra gecenin boş
olması iyice göze batıyordu.

Yıldızlar **gökyüzü shader'ında** üretiliyor: bakış yönü bir ızgaraya
yuvarlanıp hash'leniyor, eşiği geçen hücre bir yıldız oluyor. Parlaklık
hücreden hücreye değişiyor ve ufka yakın olanlar sönüyor.

Bu yerleşim bilinçli. Alternatif — yıldızları 2B nokta olarak çizmek — **dağların
önüne düşerdi**: 2B katman 3B sahneden sonra çizildiği için arazi onları
örtemezdi. Gökyüzü kubbesi zaten en arkada ve derinliğe yazmadan çiziliyor,
dolayısıyla örtüşme kendiliğinden doğru. Üstelik sıfır çizim çağrısı, sıfır
asset, ve web/Android'de de çalışıyor (iki shader varyantına da eklendi).

- `sky_stars3d(i)` / `yildiz3d(i)` — tame katmanı (0 kapalı, 1 tam)
- `stars3d(x)` / `yildiz3d_sabit(x)` — scene3d'de elle sabitleme
- `stars_auto3d()` / `yildiz_oto3d()` — **varsayılan**: gündüz-gece döngüsü
  açıkken gece çöktükçe beliriyorlar, gündüz kayboluyorlar

2 yeni test (142 → 144). Shader çıktısı penceresiz doğrulanamıyor; test
edilebilen şey **karar** — hangi saatte hangi yoğunluğun istendiği. İlk
sürümde bu karar `if` ile dağıtılmıştı ve "elle sabitleme yok sayılıyor"
bozması hiçbir testi kızartmıyordu; karar tek bir saf fonksiyona
(`_dn_star_out3`) alınınca yakalanır oldu.

### Added — su yüzeyi

Arazi geldiğinden beri vadiler kuruydu. Su, dünya çapında tek bir **yatay
düzlem** (göl/deniz seviyesi): arazinin o seviyenin altında kalan her yeri
sular altında.

- `water3d(y)` / `su3d`, `water_off3d`, `water_color3d(renk, alfa)` / `su_renk3d`
- `under_water3d(id)` / `su_altinda3d`, `under_water_at3d(x,y,z)`,
  `water_level3d()` — oyun mantığı için (nefes, hız, ses)
- **Yüzme fiziği** (`water_physics3d` ile kapatılabilir): suda yerçekimi
  azalıyor, üstel sürtünme başlıyor ve `jump3d` **zemin gerektirmiyor** —
  her basış bir yüzme vuruşu. Bu son madde olmadan suya düşen oyuncu dibe
  çakılıp bir daha çıkamazdı.

**Su en sonda çiziliyor.** Saydam ve dünyanın büyük kısmını kapladığı için,
opak cisimlerden önce çizilirse derinlik tamponuna yazıyor ve suyun *altında*
kalan her şey eleniyor — su, dibi göstermek yerine düz bir levhaya dönüyor.
(Entity'lerin iki geçişli çizilmesinin sebebi de aynı.) Saydam görünebilmesi
ışık shader'ındaki alfa düzeltmesine bağlıydı; ondan önce su opak bir levhaydı.

Sorgu, entity'nin **merkezini** ölçüyor (yarı bel hizası); ayak ucu ölçüsü su
kenarında yürürken sürekli açılıp kapanırdı.

Gerçek kaldırma kuvveti bilerek modellenmedi: cisim suda yavaşça batıyor.
Basit ve öngörülebilir; "kendiliğinden yüzen sandık" TODO'da.

5 yeni test (137 → 142). Çizim GPU'da ve penceresiz doğrulanamıyor ama fizik ve
sorgu saf matematik: yerçekimi azaltmasını, yüzme vuruşunu ve fizik kapatmasını
sökmek ilgili testleri kırmızıya döndürüyor. `examples/scene3d_terrain.tpr`
artık göllü.

### Added — builtin tablosu ↔ codegen ↔ LSP tutarlılık denetimi

Bir builtin üç yerde birden bilinmek zorunda (codegen, typeinfer tablosu, LSP)
ve bunlar **elle** senkron tutuluyordu. Ayrışmışlardı; ayrışmanın iki yönü de
sessizdi ve ikisi de ölçüldü:

- **Tabloda var, codegen'de yok (7):** `clock`, `toBool`, `toLower`, `toUpper`,
  `values`, `socket_recv`, `socket_select`. typecheck "sorun yok" diyor,
  kullanıcı çalışma anında "fonksiyon bulunamadı" alıyor.
- **Codegen'de var, tabloda yok (96):** o çağrılar **hiç denetlenmiyor**.
  Ölçülen fark: `str s = len("abc")` yakalanıyor ama `str s = pow("a","b")`
  sessizce geçiyordu — tabloda olmayan builtin'in dönüşü VOID sayıldığı için
  sonraki denetimler de atlanıyor.

`assert`'in yıllarca sessiz no-op kalması tam bu aileden bir hataydı: bir liste
gerçekliği yansıtmıyordu ve bunu söyleyen hiçbir şey yoktu.

- **`tests/builtin_audit.py`** üç listeyi kaynak dosyalarından çıkarıp
  karşılaştırıyor; `build.sh suites` ve CI'da koşuyor (derleme gerektirmez).
  Denetimin kendisi de bozma ile doğrulandı: tabloya hayalet bir isim eklemek
  ve tablodan gerçek bir builtin çıkarmak ayrı ayrı kırmızıya döndürüyor.
- **14 matematik builtin'i typeinfer tablosuna** eklendi (`pow`, `atan2`,
  `acos`, `hypot`, `round`, `trunc`, …). Dönüş tipleri `runtime_bindings.cpp`'den
  **okundu**, tahmin edilmedi — hepsi `VM_FLOAT`.
- **17 builtin LSP'ye** eklendi (tamamlama/hover yoktu): trigonometri ailesi,
  `startsWith`/`endsWith`/`indexOf`, `keys`, `clone`, `env`, `sha256`.
- Kalan **47 boşluk `KNOWN_GAPS` içinde takip ediliyor** — kapatılmadı ama
  artık görünür ve listeden silmek imza eklemek demek. Kullanıcı yüzü olmayan
  42 iç sembol (`wings_*`, `tls_*`, `arena_*`, …) `INTERNAL` listesinde,
  gerekçesiyle.

### Fixed — kalabalık oyuncuyu duvarın içine sokabiliyordu (duvardan geçme zinciri)

Çarpışma çözümü şu sırayla çalışıyordu: **(1)** hareketli gövdeleri duvarlardan
dışarı it, **(2)** hareketli gövdeleri birbirinden ayır. İkinci adım ayırmayı
yarı yarıya paylaştığı için, düşmanlar oyuncuyu duvara bastırdığında oyuncu
duvara doğru yarım adım geri gidiyordu — ve o kare bir daha düzeltilmiyordu.

Ölçüldü: kuzey duvarına bastıran kalabalıkta kalıcı batma **0 düşmanda 0.00,
4 düşmanda 0.15, 8 düşmanda 0.30 birim** (~0.33'te doyuyor). Köşede ve 45°
dönük duvarda da ~0.25.

Oyuncu duvarın içinde durunca kamera ışını **geometrinin içinden başlıyor** ve
kullanıcının bildirdiği zincir buradan geliyor: kameranın bozulması, duvarın
öbür tarafını görme, ulaşılmaması gereken yerlere çıkma. (Bu, oyunlarda tanıdık
bir sınıf: kamerayı geometriye sokup dünyanın dışına çıkmak.)

**Duvar çözümü artık kare sonunda TEKRAR çalışıyor** — dünya geometrisi
cisimlerden daha otoriter olmalı ve bunu söyleyen şey sıralamadır. Ölçüm
sonrası: 14 düşman ve 20 birim/sn baskıda bile batma 3.6e-07 (kayan nokta
gürültüsü). Köşeler ve dönük duvar da tam yüzeyde duruyor.

2 yeni test (120 → 122); ikinci geçiş kaldırıldığında ikisi de kırmızıya
dönüyor.

### Fixed — kamera dünyanın dışına çıkıyordu ("duvarların içinden geçiyorum")

Röntgen eklenirken duvar taraması kapatılmıştı ("saydamlık zaten görüş
sağlıyor"). Yanlıştı ve kullanıcı testinde çıktı: saydamlık kamerayla oyuncu
**arasındaki** cismi çözüyor, kameranın duvarın **arkasına** geçmesini değil.
Sırtını duvara dayayınca kamera arenanın dışına çıkıyor, dışarıdan bakınca
duvarlar arka yüz ayıklamasıyla kayboluyor — oyun "duvarların içinden
geçiyorum, yükseklik algım gitti" gibi görünüyor. Kamera dünyanın içinde kalmak
**zorunda**; saydamlık onun yerine geçemez, yanına gelir.

Duvar taraması geri geldi ve çözüm **iki aşamalı** oldu:

1. **Önce YÜKSELT** — engelin üstünden bakmayı dene (en fazla 74°, beş adım).
   Oyuncu ekranda aynı boyda kalır, yönelim korunur. Sırtını duvara dayamanın
   doğal karşılığı kameranın duvarın üstüne çıkmasıdır, oyuncunun ensesine
   yapışması değil — ilk şikâyet buydu.
2. **Yetmezse yakınlaştır** (dünya birimi tabanla). Son çare.

Katı gövde çarpışmasının sağlam olduğu ayrıca ölçüldü: arenanın dört duvarı,
silindir sütunu ve 45° dönük çapraz duvarı oyuncuyu tam yüzeylerinde durduruyor
(z=−14.9, x=14.9, …). Yani "içinden geçme" tamamen kamera kaynaklıydı.

### Added — engele yürümek tırmandırmıyor, zıplamak gerekiyor

Kullanıcı isteği üzerine davranış ölçüldü ve **regresyon testine bağlandı**:
alçak bir kutuya yürüyen oyuncu önünde duruyor (0.4, 1.0, 3.0 yüksekliklerinde
de), üstüne çıkmak için zıplamak gerekiyor. MTV en az batma eksenini seçtiği
için yanlış ayarlanmış bir çözüm burada "bedava tırmanma" üretebilirdi; iki test
(tırmanmama ve zıplayarak aşabilme) bunu artık kilitliyor.

### Changed — kamera artık engelden kaçmıyor, engeli SAYDAMLAŞTIRIYOR

Kamerayı duvara çarptıkça içeri çekmek yanlış çözümdü: duvara sırtını dayamak
kamerayı oyuncunun dibine sokuyor, yönelimi tamamen kaybettiriyordu. Aşağıdaki
mesafe düzeltmesi bunu yumuşattı ama kökten çözmedi — kullanıcı testinde hâlâ
"kamera engele takılıyor" olarak görüldü.

Üçüncü şahıs oyunlarının standart çözümü uygulandı: **kamera yerinde kalıyor,
kamerayla oyuncu arasına giren cisim saydam çiziliyor.** Oyuncu her zaman
görünür. Varsayılan AÇIK; eski davranış `kamera_seffaf3d(false)`.

- `camera_xray3d` / `kamera_seffaf3d`, `xray_alpha3d` / `seffaflik3d` (alfa 70).
- Röntgen açıkken duvar taraması kamerayı **çekmiyor**; arazi/zemin taraması ise
  her hâlükârda sürüyor — kameranın yerin altına girmesi saydamlıkla
  çözülebilecek bir şey değil, orada gerçekten dünyanın altını görürsün.
- Oyuncunun **kendisi** ve mermiler asla saydamlaşmaz (ilki amacın tersi olurdu,
  ikincisi yanından geçen her mermide duvarı yanıp söndürürdü).

**Çizim iki geçişe ayrıldı ve bu zorunlu:** saydam cisim opaklardan önce
çizilirse derinlik tamponuna yazar ve arkasındaki oyuncu elenir — yani duvar
saydam değil, delik gibi görünür. Saydamlar en sona kalıyor, böylece oyuncunun
pikselleri tamponda hazırken üstlerine karışıyorlar.

**Işık shader'ında alfa düzeltildi — saydamlığın çalışmasının ön koşulu bu.**
Stok raylib ışık shader'ı opak varsayımıyla yazılmış: `colDiffuse +
vec4(specular, 1.0)` terimi alfaya 1.0 **ekliyor**, ambient terimi de üstüne
pay koyuyor. 70/255 = 0.27'lik bir tint alfası böylece 1.54'e çıkıp 1.0'a
kırpılıyordu, yani saydam çizmek imkânsızdı — röntgen işaretlemesi doğru
çalıştığı hâlde ekranda hiçbir şey değişmiyordu (kullanıcı bildirimiyle
yakalandı). Alfa artık açıkça `texelColor.a * colDiffuse.a`: yüzeyin kendi
alfası çarpı tint alfası. Opak çizimlerde ikisi de 1 olduğu için mevcut
görüntü değişmiyor; 3B'de alfa kullanan tek yer bu özellik.

3 yeni test (115 → 118). **Dürüst sınır:** iki geçişin SIRASI ve alfa
karışımının kendisi penceresiz doğrulanamıyor (ikisi de GPU'da); işaretleme
mantığı test altında, çizim tarafı akıl yürütme + görsel testle doğrulandı.

### Fixed — arkada engel varken kamera oyuncunun dibine giriyordu

Kamera-engel çözümünde hem geri çekilme payı hem de "en yakın mesafe" **oran**
olarak ifade edilmişti: taban `0.12`, yani yörünge mesafesinin %12'si. Sonuç,
kameranın ne kadar uzakta kurulduğuna göre değişiyordu — `kamera_yorunge(p, 16,
...)` diyen bir sahnede arkadaki engel kamerayı oyuncunun **1.9 birim** yakınına
sokuyordu (pratikte kafasının içine), 40'lık bir kamerada ise 4.8 birime.
"En yakın mesafe" kavramının oranla ifade edilmesi baştan yanlıştı.

- `_cam_near3` (varsayılan 2.2) ve `_cam_pad3` (0.35) artık **dünya birimi** ve
  yörünge mesafesinden bağımsız. Ayarlanabilir: `camera_near3d`/`kamera_en_yakin3d`,
  `camera_pad3d`/`kamera_pay3d`, `camera_ease3d`/`kamera_yumusatma3d`.
- **Yumuşatma eklendi ve bilerek asimetrik:** içeri anında (yoksa kamera bir kare
  duvarın içinde kalır), dışarı kademeli — engelin arkasından çıkarken kameranın
  geri fırlaması, girmesinden daha rahatsız edici.
- Engel `near`'dan da yakınsa kamera duvara biraz girer. Bilinçli takas;
  alternatifi oyuncunun kafasının içinden bakmak.
- Kamera hedefi değişince (`camera_orbit`) yumuşatma durumu sıfırlanıyor, yeni
  bölüm önceki sahnede sıkışmış mesafeyi miras almıyor.

Karar mantığı saf fonksiyonlara ayrıldı (`_cam_allowed3`, `_cam_smooth3`) —
kamera konumu raylib'e yazıldığı için geri okunamıyor, dolayısıyla test
edilebilmesinin tek yolu buydu. 4 yeni test (111 → 115); eski oransal mantık
geri konduğunda ikisi, yumuşatma asimetrisi sökülünce biri kırmızıya dönüyor.

### Added — gündüz-gece döngüsü

Gökyüzü sabitti: sahne hangi saatte kurulduysa sonsuza kadar o saatteydi.

- `daynight3d(seconds_per_day)` / `gunduz_gece3d` — tam bir günün gerçek süresi.
- `set_time3d(hour)` / `saati_ayarla3d`, `freeze_time3d` / `saati_dondur3d` —
  "sahnem hep gün batımında olsun" isteyen oyun saati kurup dondurur.
- `time_of_day3d` / `gunun_saati3d`, `is_night3d` / `gece_mi3d`,
  `sun_height3d` / `gunes_yuksekligi3d` (+1 tepe, 0 ufuk, -1 ayaklar altında).

Gökyüzü gradyanı, güneşin **yönü ve rengi**, ortam ışığı ve sis rengi birlikte
değişiyor; şafak/gün batımında turuncu bir alacakaranlık bandı devreye giriyor.
**Gölgeler bedava dönüyor** — gölge haritası 0 numaralı ışığın yönünden türüyor,
o da güneşle birlikte dönüyor: sabah uzun gölgeler, öğlen kısa.

**Işık yönü gün boyu kesintisiz dönüyor ve ufkun altına inmiyor.** İlk sürümde
gece ışığı "güneşin tam karşısındaki ay"dı; bu, güneşin ufku geçtiği **karede**
ışık yönünü 180° atlatıyordu — gölgeler tek karede öbür tarafa sıçrıyor ve
ekran kare düşürmüş gibi görünüyordu (kullanıcı bildirimiyle yakalandı). Ayrıca
ufka paralel ışık gölge haritasını dejenere ediyor, gölgeler uzayıp titriyor;
şafak ve gün batımı tam o iki an. Işığın yüksekliği artık 0.22'nin altına
inmiyor, gece/gündüz farkını **renk ve ortam ışığı** taşıyor. Ölçüldü: adımın
CPU maliyeti kare başına ~0.5 µs, yani hissedilen şey işlem yükü değil bu
süreksizlikti.

Tamamen `lib/scene3d.tpr` içinde — tek satır C eklenmedi. Gerekli her şey
zaten vardı (`sun`, `ambient_light`, `sky`, `fog_sky` hepsi her kare
çağrılabilir ve ucuz: gökyüzü kubbesi bir kez kuruluyor, sonrası birkaç float).
Sis rengi her kare ufuktan yeniden alınıyor; alınmasaydı sis bütün gün
şafak rengiyle kalırdı. Saat duraklatmada donuyor — oyun durduysa zaman da
durmalı, yoksa menüden çıkınca gece olmuş buluyorsun.

6 yeni regresyon testi (105 → 111). Gökyüzü GPU'da çiziliyor ama saati süren
şey saf matematik ve hepsi penceresiz doğrulanıyor: saat sarmasını kaldırmak,
dondurmayı yok saymak ve renk karışımının kırpmasını sökmek ilgili testleri
kırmızıya döndürdü. Süreksizlik için ayrı bir test var — saati 18:00'in iki
yanından ince adımlarla geçirip ışık yönünün kare başına küçük değiştiğini
sınıyor; eski "karşı yöndeki ay" mantığı geri konduğunda kırmızıya dönüyor.

### Added — arazi katman boyama: çim / toprak / kar + eğimde kaya

Arazi tek renkti — yüz yirmi birimlik bir dünyanın tamamı aynı yeşil. Oysa
yükseklik de eğim de zaten elimizdeydi (fizik ikisini de kullanıyor), yani bu
iş yeni **veri** değil, o veriyi göstermeyi istiyordu.

- `terrain_paint3d(low, mid, high, rock, mid_y, high_y, rock_slope)` /
  `arazi_boya3d` — eşikler **dünya Y'si**, eğim derece. `terrain_natural3d(peak)`
  / `arazi_dogal3d` tek satırlık hazır palet.
- `terrain_layer3d(x, z)` / `arazi_katmani3d` — o noktada hangi katman
  (`LAYER_LOW/MID/HIGH/ROCK`). Oyun mantığı için **keskin** cevap verir (ayak
  sesi, hız, "karda mısın"); mesh boyaması ise sınırlarda yumuşak geçer. Göz
  gradyan ister, oyun kesin cevap.
- **Eğim yüksekliği ezer:** dik yüzeyde çim de kar da tutmaz, kaya çıkar.

Uygulama tarafında iki karar taşıyıcı:

1. **Renk mesh'in tepe noktalarına yazılıyor, shader'a değil.** Arazi sıradan
   bir model olarak kalıyor: doku, ışık, gölge ve sis yollarının hiçbiri
   değişmiyor, ayrı materyal yönetilmiyor. Mesh'i kendimiz üretmiyoruz da —
   `GenMeshHeightmap`'in üçgenlemesi tek doğruluk kaynağı olarak kalsın diye
   yalnız renk VBO'su ekleniyor (`tm3_terrain_height` o üçgenlemeyi taklit
   ediyor; ayrışırlarsa fizik görselden kayar).
2. **Işık shader'ı düzeltildi:** `texelColor` artık `fragColor` ile çarpılıyor.
   Stok raylib ışık shader'ı tepe rengini vertex'ten fragment'a taşıyıp
   **kullanmıyordu**, yani boyama görünmezdi. Renk tamponu olmayan mesh'lerde
   raylib öznitelik varsayılanını beyaz yaptığı için bu değişiklik mevcut
   hiçbir çizimi etkilemiyor.

**Sıra önemsiz:** katman ayarı mesh üretim anında uygulandığı için "önce
katman, sonra arazi" zorunluluğu doğardı — yanlış sırada hiçbir şey olmayan,
hata da vermeyen sessiz bir tuzak. Onun yerine `scene3d` arazi parametrelerini
saklıyor ve katman değişince araziyi yeniden kuruyor.

Varsayılan eşikler (tepenin %35'i / %62'si) **ölçülerek** seçildi: Perlin
gürültüsü pratikte `sy`'ye ulaşmıyor, tepe genelde %80 civarında kalıyor —
oranları doğrudan yüksek tutmak karı "en uç iki noktaya" düşürüyordu. Seçilen
değerler üç ayrı tohumda da dengeli dağılım veriyor.

3 yeni regresyon testi (102 → 105). Mesh boyaması GPU'da olup bitiyor ve
penceresiz doğrulanamaz, ama **sınıflandırma** saf matematik ve yükseklik
verisi pencere olmadan da saklanıyor — boyama ile oyun mantığı aynı eşikleri
okuduğu için asıl kural test altında. Eşikleri ters çevirmek ve (C tarafında)
eğim kuralını devre dışı bırakmak ilgili testleri kırmızıya döndürdü.

### Added — 3B'de gamepad: çubuk, tetik, menü

tame'de bağlamalar zaten vardı (`gamepad_down`, `gamepad_axis`, ...); `scene3d`
onları **hiç okumuyordu**, yani 3B oyunlar kolla oynanmıyordu. Artık kol takılıysa
kendiliğinden çalışır — oyunun tek satır girdi kodu yazması gerekmez.

- **Sol çubuk hareket, sağ çubuk bakış, A zıpla, START duraklat.** Klavye ve
  dokunmatik aynı anda açık kalır; biri diğerini kapatmıyor.
- **`move3d` artık analog büyüklüğü koruyor.** Eskiden girdi vektörü *koşulsuz*
  normalize ediliyordu, yani her girdi tam hızdı. Klavye/dokunmatik için farksız
  (ikisi de zaten 1 üretiyor), kol için analogluğun tamamen kaybı demekti —
  çubuğu yarım itmek koşmakla aynı şeydi. Büyüklük 1'i **aşarsa** hâlâ normalize
  ediliyor, yani çapraz gitmek düz gitmekten hızlı değil.
- **Ölü bölge yeniden ölçekliyor.** Eşiği ham değere uygulayıp bırakmak, çubuk
  eşiği geçtiği anda hızın 0'dan 0.22'ye sıçraması demekti; kalan aralık 0..1'e
  yayılıyor.
- **Menülerde imleç.** Duraklat menüsünde üç düğme var ve Enter hep ilkine
  gidiyordu — kolla "Yeniden" seçilemezdi. Yukarı/aşağı (D-pad, sol çubuk,
  klavye okları) + A/Enter onay, B/ESC geri. İmleç 0'da başladığı için eski
  "Enter = ilk düğme" davranışı birebir korunuyor; uçlarda **sarmıyor**.
- Ayarlar: `gamepad3d`/`kol3d` (kapat/aç), `gamepad_id3d`, `stick_deadzone3d`,
  `gamepad_look_speed3d`, `gamepad_active3d`/`kol_bagli_mi3d`.
- **Okuma tek yere hapsedildi** (`_read_gamepad3()`, `play3d` içinden kare başına
  bir kez) — dokunmatikle birebir aynı desen. Sebebi mimari: raylib'in girdi
  çağrıları pencere ister, motorun fizik/çarpışma katmanı ise pencere **açmadan**
  test ediliyor. Ölü bölge matematiği de cihaz okumasından ayrı bir saf
  fonksiyona (`_gp_curve3`) alındı, aynı gerekçeyle.
- 7 yeni regresyon testi (95 → 102), hepsi bozma enjekte edilerek doğrulandı:
  koşulsuz normalize, normalize'ın hiç olmaması, ölçeklenmeyen ölü bölge,
  sınırsız imleç ve seçimi yok sayan onay — beşi de ilgili testi kırmızıya
  döndürdü.

### Fixed — kapanan tetikleyici bölge sahte "çıkış" atıyordu

Tek atımlık bir bölge, **tarama ortasında** kapanabiliyor: bölgede zaten duran
bir gövde işlendikten sonra ikinci bir gövde girip bölgeyi kapatırsa, birincisi
o karenin üyelik kümesine çoktan yazılmış oluyordu. Sonraki kare onu "artık
kümede yok" diye görüp `cikinca3d` kancasını çağırıyordu — oyuncu bölgeden hiç
çıkmamışken. Çıkış geçidi artık bölgenin açık olduğunu da kontrol ediyor:
**kapalı bölge hiçbir olay üretmez.**

Kapatmanın üyeliği düşürmesi bunu tek başına engellemiyordu; iki ayrı iş
yapıyorlar (üyelik düşürme `inside3d()`'nin kapanır kapanmaz doğru cevap
vermesi için). Tek gövdeyle senaryo hiç görünmüyor — mevcut `tek atım` testi
bu yüzden yakalayamıyordu; iki gövdeli yeni bir regresyon testi eklendi ve
koruma sökülerek kırmızıya döndüğü doğrulandı.

### Added — `heal3d` / `iyilestir3d`: can vermenin doğru yolu

Şifa için tek yol `damage3d(id, -n)` ya da `health3d(id, n)` idi; ikisi de
yanlış araçtı:

- `damage3d(-n)` **dokunulmazlık penceresi açıyor**, yani şifa alan oyuncu bir
  de hasara bağışık kalıyordu. Şifa "vurulmadın" demek değil.
- `health3d(n)` can sistemini **yeniden kuruyor** (hp *ve* hpmax = n), yani
  120 canlı bir gövdeye 100 uygulamak canını düşürürdü.

`heal3d` hpmax'ı aşmadan ekler, dokunulmazlığa dokunmaz, can sistemi kurulmamış
entity'yi yok sayar. `examples/scene3d_arena.tpr` bonus pedi artık bunu
kullanıyor.

### Added — 3B kalıcılık: rekor ve bölüm ilerlemesi artık kaydediliyor

tame `save_data`/`load_data` veriyordu (masaüstünde CWD, Android'de
`internalDataPath`) ama `scene3d` kullanmıyordu: rekor da, açılan bölüm de oyunla
birlikte yok oluyordu. arcade'de ikisi de vardı.

**OPT-IN (`save_progress3d()` / `kayit_ac3d()`), bilerek:** kalıcılık *diske
yazmak* demek ve motor bunu istenmeden yapmamalı — aksi hâlde her örnek
çalıştırması çalışma dizinine dosya bırakırdı. Açan oyun tek satırla açar.

- **Rekor.** `best_score3d()` / `rekor3d()`, `new_record3d()` /
  `rekor_kirildi3d()`. Skor **doğal kontrol noktasında** işleniyor —
  `game_over3d()` içinde, çünkü hem kaybetme hem kazanma oradan geçiyor. Her
  `score3d_add` için diske yazmak saçma olurdu.
- **Bölüm ilerlemesi.** `level_done3d(k)` / `bolum_bitti_mi3d`,
  `unlocked_level3d()` / `acik_bolum3d`. `next_level3d()` çağrılması "bu bölüm
  TEMİZLENDİ" demektir, işaretleme orada; `goto_level3d(n)` ise serbest atlama
  (menü, hile, yeniden başlatma) ve **işaretlemiyor** — yoksa oyuncu hiç
  görmediği bölümü geçmiş sayılırdı.
- `unlocked_level3d()` **kesintisiz** sayıyor: 3. bölüm bitmiş ama 2. bitmemişse
  cevap 2. Aksi hâlde oyuncu atladığı bölümü hiç görmezdi.
- Anahtar sahne başlığından türüyor, yani iki farklı oyun birbirinin rekorunu
  ezmiyor. arcade ile aynı dosya deseni, farklı önek (`scene3d_best_*`,
  `scene3d_lvl_*`); `.gitignore`'a arcade'inkilerin yanına eklendi.
- Menü katmanı bağlandı: başlangıç ekranı rekoru, oyun-bitti ekranı ya rekoru ya
  da **"YENI REKOR!"** rozetini gösteriyor. `restart3d()` rozeti sıfırlıyor
  (rozet yeni ele ait), kalıcı rekorun kendisi duruyor.
- `scene3d_reset()` **belleği** sıfırlıyor, **diski** değil: "sahneyi baştan kur"
  ≠ "oyuncunun rekorunu sil".

**Android'de gerçek bir çökme çıktı ve emülatör kurulumu olmasa görülmezdi.**
`kayit_ac3d()` üst düzeyde, yani `oyna3d()`den — dolayısıyla `InitWindow`'dan —
önce çağrılıyor. Android'de raylib'in dosya yolu göreli adlar için
`AAssetManager`'a düşüyor (`LoadFileText` → `android_fopen` →
`AAssetManager_open`) ve asset manager ancak aktivite ayağa kalkınca kuruluyor;
öncesinde NULL mutex üzerinde SIGSEGV. Masaüstünde raylib düz `fopen` kullandığı
için 80 testin hepsi geçiyordu — hata yalnız cihazda vardı. İki yerden düzeltildi:

- `tame_impl_save_data`/`load_data` artık **yalnız Android'de** pencere hazır mı
  diye bakıyor ve hazır değilse çökmek yerine "veri yok" diyor. Guard'ı her
  platforma koymak motorun **headless test edilebilirliğini** kırardı (arazi
  fiziğinde bilerek korunan özellik) — nitekim ilk denemede kalıcılık testleri
  tam bu yüzden düştü, o yüzden `PLATFORM_ANDROID` ile sınırlandı.
- `play3d()` pencereden **sonra** kalıcılığı koşulsuz yeniden okuyor (idempotent).
  Doğru okuma anı orası; üst düzeydeki ilk okuma Android'de boş dönüyor.

7 test eklendi; üçü bilerek hata sokularak doğrulandı. **Bir test zayıf çıktı ve
bu ölçülerek bulundu:** `goto_level3d`'yi "bitti" saydıran bozma testi
geçiyordu — çünkü test `scene3d_reset()` sonrası doğrudan `goto_level3d(3)`
çağırıyordu ve `_cur_lvl3` hâlâ 0 olduğu için işaretleme zaten erken dönüyordu.
Test artık önce gerçekten 1. bölüme giriyor, sonra atlıyor.

### Added — düşman engelden kaçınıyor: `chase3d` artık duvara toslamıyor

`chase3d` düz çizgi kovalıyordu: düşman duvara dayanıp orada titriyordu. Bir 3B
oyunda en çok göze batan kusur buydu. **Varsayılan AÇIK** — bu bir özellik değil
kusur düzeltmesi; eski davranışı isteyen (uçan düşman, duvarsız arena)
`chase_direct3d()` / `chase_avoid3d(false)` diyebilir.

Navmesh/A* değil, bilerek: bu bir preset motor, sahne tamamen dinamik ve
duvarlar da entity. Yön taraması **durum tutmuyor**, her karede sıfırdan karar
veriyor — duvar hareket etse, yıkılsa ya da yeni duvar doğsa hiçbir veri yapısını
tazelemek gerekmiyor. Bedeli belgeli: tek başına U biçimli bir tuzaktan çıkamaz
(yerel minimum); gerçek labirent isteyen oyun kendi yolunu `set3vel` ile sürer.

İki şey **ölçülerek** bulundu, ikisi de load-bearing:

- **Tarafa bağlanma şart.** İlk sürüm her karede "hedefe en yakın açık yön"ü
  sıfırdan seçiyordu. Simetrik bir duvarın önünde bu taraf değiştirip duruyor,
  düşman ilerlemeden titriyordu — ölçüldü: `vx` her kare **+6.5 ↔ −6.9**
  arasında zıplıyordu. Artık seçilen taraf `Ent3.avoid`da saklanıyor ve yol
  açılana kadar korunuyor; seçili taraf tamamen kapanırsa taraf çevriliyor
  (köşede kalıcı kilit olmasın).
- **Engel, hareket edenin yarıçapı kadar ŞİŞİRİLMELİ.** Işın sıfır kalınlıkta,
  gövde değil: merkezden "açık" görünen yol gövdeyi duvarın köşesine takıyordu.
  Ölçüldü — düşman köşede **tam olarak** donuyordu (konum 7 basamağa kadar
  sabit: fizik ilerletiyor, çarpışma birebir geri itiyordu). Klasik
  "konfigürasyon uzayı" numarası: engeli yarıçap kadar büyüt, nokta için açık
  olan yol gövde için de açık olsun.

Y aralığı örtüşmeyen duvarlar taramada atlanıyor, yani üst geçit gibi yapılar
yolu kapatmıyor.

Mevcut bir testin **niyeti ile ölçtüğü şey ayrıştı** ve düzeltildi:
`t_enemy_blocked_by_wall` "240 kare sonra z < 5" diye bakıyordu, oysa niyeti
katı-gövde garantisiydi. Kaçınma gelince düşman duvarın **etrafından** dolaşıp
z > 5'e çıkıyor — hiçbir şeyin içinden geçmeden. Test artık gerçek değişmezi
ölçüyor (simülasyon boyunca duvarla hiç örtüşmeme) ve ikiye ayrıldı: kaçınma
kapalıyken geçemez, açıkken dolaşarak geçer. 6 yeni test; üçü bilerek hata
sokularak doğrulandı (bağlanmayı kaldır, şişirmeyi kaldır, kaçınmayı kapat).

### Added — arazi eğimi: yamaç sınırı, kayma, ve kamerayı tepeden çıkarma

Faz 10 dünyayı sürekli bir yüzey yaptı ama iki ödün bırakmıştı: oyuncu dik
yamaçları da tırmanabiliyordu ve kamera tepenin içine girip sahneyi topraktan
gösteriyordu. İkisi de kapandı.

**Yeni bir C binding'i gerekmedi.** STATUS.md `tm3_terrain_normal(x, z)`
öneriyordu; ama `terrain_height3d` zaten mesh'in üçgenlemesini birebir taklit
ettiği için yüzey normali komşu örneklerden **merkezi farkla** saf Tulpar'da
türetilebiliyor. Böylece 5 noktalık bağlama da, `wasm/dist` + `android/dist`
arşivlerini yeniden derlemek de tamamen atlandı ve özellik üç hedefte anında
çalıştı. Merkezi fark hücre sınırlarını ayrıca yumuşatıyor — üçgen bazlı kesin
normal keskin sıçrar ve kayma titrerdi.

- **`slope_limit3d(derece)` / `egim_siniri3d`** — bundan dik yamaçlarda entity
  tırmanamaz, yamaç aşağı kayar. **Varsayılan kapalı**, bilerek: eğim sınırı
  doğruluk değil **tasarım tercihi** (bazı oyunlar serbest tırmanma ister).
  Açmayan sahne bit-bit eskisi gibi çalışır.
- **`slide_accel3d(a)` / `kayma_ivmesi3d`** — kayma ivmesi.
- **`terrain_up3d(x, z)`** yüzeyin "düzlüğü": normalin Y bileşeni, yani eğim
  açısının **kosinüsü** (1 = düz, 0 = dik duvar). Derece değil, çünkü dilde
  `acos` yok ve karşılaştırma zaten kosinüs üzerinden yapılıyor — açıya çevirip
  geri dönmek hem gereksiz hem kayıplı olurdu. `terrain_steep3d` /
  `arazi_dik_mi3d` sınırla karşılaştırıp doğrudan cevap veriyor.
- Yamaç aşağı yön ayrıca hesaplanmıyor: normalin **yatay bileşeni** zaten
  tepeden dışarı, yani yamaç aşağı bakıyor.
- **Kamera ışını artık zemini görüyor.** Arazide kapalı biçimli bir kesişim yok,
  o yüzden ışın boyunca 14 örnek yürünüp zeminin altına düşülen ilk yer
  bulunuyor. `_floor_at3` kullanıldığı için **rampalar da bedavaya** kapsandı —
  onlar da `TAG_WALL` olmadıkları için eski kutu taramasının dışındaydı.

6 regresyon testi eklendi (61 → 67) ve üçü bilerek hata sokularak doğrulandı.
**Yön testinin ilk hâli yetersizdi ve bu ölçülerek bulundu:** "bitiş zemini
başlangıçtan alçak" diye ölçüyordu ve kayma yönü tersine çevrilince bile
geçiyordu — entity yamaç yukarı kayıp arazinin **ayak izinden çıkıyor**, dışarıda
yükseklik taban 0'a dönüyor ve "alçaldı" sanılıyordu. Test artık yönü doğrudan
ölçüyor (hareket vektörünün yamaç-aşağı yönüyle iç çarpımı; ters kurulumda
−9.5) ve entity'nin ayak izinde kaldığını ayrıca sabitliyor.

Yan düzeltme: `scene3d_reset()` eğim durumunu da temizliyor. Temizlemediği için
testler sıraya bağımlıydı — "varsayılan kapalı" testi bir öncekinin bıraktığı
sınırı ölçüyordu.

### Added — 3B menü/UI katmanı: başlangıç ekranı, duraklat, yeniden başla (Faz 11)

Motor Faz 7-10'da "üstüne oyun yazılabilir" hale gelmişti ama **yayınlanabilir**
değildi. Oyun-bitti ekranı yalnız yazı basıyordu: hiçbir tuş iş yapmıyor,
yeniden başlamanın yolu yok, tek çıkış pencereyi kapatmak. `arcade`'de bunların
hepsi vardı; 3B'de yoktu.

**Ön koşul bir tame binding'i çıktı.** raylib `InitWindow`'da çıkış tuşunu ESC'ye
kuruyor — ESC'ye basıldığında `WindowShouldClose()` true dönüyor ve oyun döngüsü
bitiyor, yani ESC Tulpar tarafında **hiç yakalanamıyordu**. Duraklat menüsünün
kurulamamasının tek sebebi buydu. `tm_exit_key(k)` eklendi (sarmalayıcılar
`exit_key` / `cikis_tusu`); `scene3d` açılışta `exit_key(K_NONE)` çağırarak
kestirmeyi kapatıyor ve ESC sıradan bir tuşa dönüşüyor.

- **`menu3d(baslik, altbaslik)` / `baslangic3d`** — başlangıç ekranı, **opt-in**.
  Çağırmayan sahne bit-bit eskisi gibi doğrudan oyuna girer.
- **Duraklat** (ESC / P / GERİ): Devam · Yeniden · Çıkış. Duraklatınca oyun
  gerçekten **donar** — bakış, `update`, fizik, çarpışma ve parçacık adımlarının
  hepsi atlanır; sahne yine çizilir, yalnız ilerlemez.
- **Oyun-bitti ekranı** artık gerçek bir ekran: skor + "Tekrar Oyna" · "Çıkış".
- **`restart3d()` / `yeniden3d()`** sırayla: bölüm kaydedilmişse en küçüğüne
  döner; yoksa `on_restart3d(fn)` kancasını çağırır; o da yoksa yalnız
  skoru/durumu sıfırlar. Sonuncusu **belgelenmiş bir sınır**: sahneyi kuran şey
  `setup` ve setup kancaları da kaydediyor — motorun onu tek başına tekrarlaması
  kancaları çoğaltırdı.
- Her düğme **klavye + fare + dokunmatik** ile çalışıyor (Android hedefi), ölçüler
  pencereye oransal. Menü açıkken imleç kilidi otomatik bırakılıyor; FPS modunda
  imleç kilitli geldiği için aksi hâlde düğmeye tıklanamazdı.
- Yan kazanç: **`is_over3d()`/`bitti_mi3d()`** ve **`alive_count3d()`** —
  `arcade`'de karşılıkları vardı, 3B'de yoktu. İkincisi ölü slot'ları saymıyor
  (`length(_e3)` slot geri kullanımı yüzünden yanıltır).

9 regresyon testi eklendi (`tests/scene3d_engine.test.tpr` 52 → 61). Biri
yazılırken gerçek bir hata yakaladı: yeniden-başlatma kancası `int` global'de
saklanıyordu ve fonksiyon referansı natif i64 global'e yazılırken kırpılıyordu,
`call()` hiçbir şey çağırmıyordu — dosyadaki diğer kancalar (`_setup3_fn`,
`_update3_fn`, `_hud3_fn`) zaten `var` kullanıyordu.

Üç hedefte de doğrulandı: Linux, Android (arm64 + x86_64, emülatöre kuruldu),
web (wasm). Yeni binding yüzünden `android/dist` ve `wasm/dist` arşivleri
yeniden derlendi.

### Fixed — fonksiyon yereli aynı adlı global'i eziyordu (AOT codegen)

```tulpar
func inner() { int p = 0; while (p < 5) { p = p + 1; } }
int p = 99;  inner();  print(p);   // 99 değil, 5
```

Sessizce yanlış sonuç veren bir derleyici hatası; her Tulpar programını
etkiliyordu, 3B'ye özgü değildi. Faz 9'da bir çarpışma hatası ararken beni bir
kez yanlış ize sürüklemişti (probe üst-düzey `int p` kullanıyordu).

**Sebep:** `AST_VARIABLE_DECL`'in iki global kısayolu da — natif `int` global ve
boxed VMValue global — yalnızca *"bu adda bir modül-düzeyi global var mı?"* diye
soruyor, bildirimin **fonksiyon gövdesi içinde** olup olmadığına bakmıyordu.
Fonksiyonun kendi `int p = 0;`i doğrudan global'e yazıyor ve yerel hiç
ayrılmıyordu; sonraki `p = p + 1` de doğal olarak global'i sürüyordu.

**Neden bu kadar keyfî görünüyordu:** parametreler etkilenmiyordu (fonksiyon
girişinde gerçek alloca alıyorlar), salt-okuyan fonksiyonlar da. Ölçülen matris:
yerel `int`/`float`/`str` bildirimi ve `for`-init bozuk; parametre ve salt-okuma
sağlam.

**Düzeltme:** her iki kısayol da yalnız kök scope'ta geçerli. Main'in scope'u
kök (parent'ı yok) ve her fonksiyon/lambda gövdesi onun üstüne biniyor — bir
fonksiyonun global'i *okuyabilmesinin* sebebi zaten bu, yani doğru sinyal
kodda hazırdı. Üst düzey `if`/`while` blokları bilerek "üst düzey" sayılıyor:
bloklar scope açmıyor ve blok kapsamı olmayan bir dilde oradaki yeniden bildirim
gerçekten aynı değişkendir.

`tests/global_shadow.test.tpr` (10 test) yalnız ezilmemeyi değil **ters
sözleşmeyi** de sabitliyor: fonksiyon global'i hâlâ okuyabilmeli ve
ATAYABİLMELİ — `scene3d`'nin `_lvl_pending3 = n` deseni, `arcade`/`wings`'in
tamamı buna dayanıyor; fazla hevesli bir düzeltme her şeyi bozardı. Düzeltme
geri alınarak doğrulandı: 6 test kırmızıya döndü, 4 ters-sözleşme testi yeşil
kaldı.

### Performance — çarpışmaya geniş faz: 200 entity'de 15.4 ms → 1.12 ms

`_s3_collision` her kare tüm çiftleri gezip her biri için `_overlap3` / `_mtv3`
çağırıyordu. Önce **ölçüldü**, ve ölçüm beklentiyi yanlışladı.

Maliyet (headless, 60fps bütçesi 16.6 ms): 50 entity 0.87 ms, 100 → 3.88,
**200 → 15.4** (bütçenin %92'si), 400 → 57, 800 → 258.

Şüpheli `Ent3` struct kopyalarıydı — her çift testi diziden iki struct çekiyor.
Mikro-ölçüm (200 entity = 40.000 çift) onu çürüttü: boş döngü ~0 ms, çift başına
iki struct kopyası 1.8 ms, gerçek `_overlap3` **23.7 ms**. Yük kopyada değil,
zincirin kendisindeydi: şekil seçimi, `Ent3`i **değerle** alan yardımcılar
(`_is_round3`, `_radius3`), tekrar tekrar dizi okuması. Yani doğru hamle zinciri
hızlandırmak değil, **hiç çağırmamak**.

- **Geniş faz.** Kareye bir kez düz float dizilerine konum + kapsayan küre
  yarıçapı yazılıyor; kapsayan küreleri ayrık olan çiftler zincire hiç girmiyor.
  Eleme struct kopyasından **önce** duruyor, böylece o 1.8 ms de kazanılıyor.
- **Yarıçap = kutunun köşegen yarısı.** Üç şeklin de üstünü örtüyor, o yüzden
  eleme korumalı: kutuyu tanımı gereği kapsar ve **yaw'dan bağımsızdır**
  (döndürme kürenin yarıçapını değiştirmez → OBB/SAT güvende); küreyi kapsar
  (yarıçapı `sx*0.5`, köşegen yarısı ondan büyük); silindiri Minkowski
  eşitsizliği gereği kapsar.
- **Diziler yalnızca eleme için.** `alive`/`tag`/`solid` kararları hâlâ `_e3`ten
  okunuyor — bir kanca ortasında `kill3d`/`spawn3` çağırırsa otorite tek yerde
  kalsın. Konum ise elemeyi doğrudan etkilediğinden üç mutasyon noktasında
  senkron tutuluyor: `_apply_mtv3` (ayırma taramanın ORTASINDA entity oynatır),
  `set3pos` (kancadan ışınlanma), `_alloc_slot3` (geri kullanılan slot'ta ölen
  entity'nin konumu duruyordu).

**Sonuç:** 200 entity 15.4 → **1.12 ms** (bütçenin %92'si → %6.7), 800 entity
258 → 16.8 ms. Her ölçekte ~14×; pratik tavan ~200'den ~800 entity'e çıktı.
Algoritma **hâlâ O(n²)** — uniform grid asimptotik çözüm olmayı sürdürüyor, ama
artık çok daha ucuz, çünkü düz konum/yarıçap dizileri grid'in zaten ihtiyaç
duyacağı zemin.

Üç regresyon testi eklendi ve üçü de **bilerek hata sokularak** doğrulandı:
elemeyi fazla hevesli yapmak `_bp_far3 ⟹ ¬_overlap3` değişmezini 26 ihlalle
düşürdü (ve gerçek bir oynanış testini de kırdı), elemeyi sessizce devre dışı
bırakmak `_prunes`i, senkronu kaldırmak `_sync_after_push`ı kırmızıya döndürdü.

### Fixed — typecheck artık `import`'u takip ediyor (`assert` hatasının kök nedeni)

`assert`'in bool koşullarda hiç başarısız olmaması tek bir yazım hatası değildi;
**yapısal bir körlüğün belirtisiydi.** Denetim onu kovaladı ve zincirin tamamını
buldu.

- **`assert_eq_bool` de no-op'muş.** İmzası `(int actual, int expected)`,
  normalleştirmesi `actual != 0`. Bool için `!= 0` **her zaman true** — iki taraf
  da 1'e normalleşiyor, iddia asla başarısız olamıyor. **15 dosyada 77 çağrı**
  hiçbir şey doğrulamıyordu. `assert` ile aynı düzeltme: parametreler tipsiz,
  kontrol doğruluk-değeri üzerinden. Düzeltmeden sonra 54 paket hâlâ geçti —
  iddialar doğruymuş, yalnızca denetlenmiyorlarmış.

- **Asıl bulgu: typeinfer hiçbir modülün kaynağını açmıyordu.** Argüman denetimi
  zaten vardı ve `assert(x < y, msg)`'yi (`bool` → `int cond`) ilk görüşte
  reddederdi — ama `assert`'ün imzasını hiç görmedi. Yani `test`, `wings`,
  `router`, `orm`, `scene3d`, `arcade`: **tüm stdlib çağrıları denetimsizdi.**
  `typeinfer_program` artık AOT ile aynı çözüm sırasını (gömülü stdlib → düz yol
  → `<ad>.tpr` → `tulpar_modules/`) kullanarak modülleri parse ediyor ve yalnızca
  **imzalarını** kaydediyor. Gövdeler denetlenmiyor: stdlib içini her kullanıcı
  derlemesinde denetlemek hem yavaş hem gürültülü olurdu, üstelik modülün kendi
  tanıları onu düzenleyene aittir. Ön-geçiş maliyeti ölçüldü: fark yok (~6ms).

- **Yeni tanı — sabit karşılaştırma.** `==`/`!=` operandları farklı skaler
  tiplerse uyarı verilir. Çalışma zamanı önce tip etiketine baktığından `b == 1`
  hep false, `b != 0` hep **true**'dur ve hiçbiri çağrı yerinde görünmez.
  `int`/`float` bilerek dışarıda: sayısal terfi `1 == 1.0`'ı gerçekten doğru
  yapar.

- **Üç yanlış pozitif ayıklandı.** İmzalar görünür olunca kod tabanında 461 uyarı
  belirdi; **418'i tek bir sebeptendi** — `json` katı bir tip gibi ele alınıyordu,
  oysa Tulpar'ın dinamik tipi. Artık iki yönde de uyumlu. Kalan ikisi: `if
  (<tipsiz parametre>)` ve `if (<json>)` — scene3d/arcade/wings'in on/off ev
  stili, tip bilgisi taşımadıkları için uyarmak yapısal olarak yanlış pozitif;
  ve `int x = <bool>` **store**'da serbest, çünkü AOT gerçekten dönüştürüyor
  (`tests/bool_to_int_coerce.test.tpr`). 461 → 6, ve kalan 6'nın hepsi bu
  çalışmadan önce de vardı (ölçülerek doğrulandı). Net etki: `lib/scene3d.tpr`
  eskiden 5 yanlış pozitif üretiyordu, artık üretmiyor.

**Dilde kalan asimetri:** `int x = true` ve `x = true` dönüşüyor, `f(true)` →
`int` parametresi **dönüşmüyor**. `assert` hatası tam olarak bu boşlukta doğdu.
Typecheck artık yakalıyor; asıl çözüm parametre bağlamayı store ile aynı hale
getirmek olur (STATUS.md backlog'unda 🟡).

Regresyon: `tests/typeinfer/{pass,fail}/` altına 6 fixture — biri doğrudan
`assert`'in ölüm senaryosu (`assert_eq_int(true, false)` reddedilmeli), biri de
`int`/`float` ile `json` jokerinin serbest kaldığını sabitliyor. Üç `fail`
fixture'ının **doğru mesajla** reddedildiği tek tek doğrulandı.

### Added — gerçek arazi: yükseklik haritalı dünya (Faz 10)

Faz 9'un rampası dünyayı düz düzlemden kurtardı ama sınırlıydı: kama biçimli
entity'ler, kademeli çizim, elle yerleştirme. Arazi gerçek çözüm — bir yükseklik
haritasından üretilmiş **tek mesh** ve her `(x, z)` için **sürekli yükseklik**.

- **`tm3_terrain_gen(res, sx, sy, sz, base, scale, seed)`** — Perlin
  gürültüsünden prosedürel arazi; **asset dosyası gerekmez**.
  **`tm3_terrain_load(path, ...)`** — gri tonlamalı yükseklik haritası
  dosyasından. İkisi de **normal bir MODEL handle'ı** dönüyor: çizim, gölge,
  ışık ve gölge geçişinin display-list kaydı zaten model handle'ı üzerinden
  çalıştığı için arazi üçünü de bedavaya alıyor. Ayrı bir `Model` tutulsaydı
  hepsini elden bağlamak gerekirdi.
- **`tm3_terrain_height(x, z)`** — yüzeyin dünya Y'si. Örnekleme
  `GenMeshHeightmap`'in **üçgenlemesini birebir taklit ediyor**, düz bilineer
  değil: mesh her hücreyi köşegenden iki üçgene bölüyor ve bilineer o köşegende
  mesh'ten sapıyor — oyuncu görünürde zeminin altına gömülür ya da üstünde
  yüzerdi. Fizik ile görselin uyuşması buna bağlı.
- **`scene3d`:** `terrain3d` / `arazi3d`, `terrain_file3d` / `arazi_dosya3d`,
  `terrain_color3d`, `zemin_yukseklik3d`. Zemin yüksekliği artık tek bir yerden
  (`_floor_at3`) geliyor: arazi varsa onun yüzeyi, yoksa düz zemin — **rampalar
  her iki durumda da üstüne binebiliyor**. Yerçekimi, zıplama ve kamera
  otomatik uyum sağlıyor; arazi kullanmayan sahneler bit-bit aynı kalıyor.

**Yükseklik verisi ile mesh kasten ayrı tutuluyor.** Gri değerleri çıkarmak saf
CPU işi, mesh üretmek ise GPU'ya yükleme yapıyor (`GenMeshHeightmap` sonunda
`UploadMesh` çağırıyor). Pencere yokken yükseklik verisi yine saklanıyor ve
model handle'ı `-1` dönüyor — böylece **arazi fiziği pencere açmadan headless
test edilebiliyor**, yalnız çizim devre dışı kalıyor. Motorun arazi testleri
(5 adet) bu ayrım sayesinde var.

Örnek: `examples/scene3d_terrain.tpr` — 120×120 birimlik prosedürel açık dünya;
altınlar `arazi_yukseklik3d` ile tam yüzeye oturtuluyor. Motor **49 headless
birim test**.

### Removed — natif Windows desteği bırakıldı; Windows artık WSL üzerinden

**3.13.0'dan itibaren TulparLang natif Windows'u desteklemiyor.** Windows
kullanıcıları **WSL** (Windows Subsystem for Linux) içinde Linux sürümünü
kullanıyor: `wsl --install`, ardından WSL içinde normal Linux tek-satır
kurulumu. Derleyici, Wings, `tame`/`arcade`/`scene3d` oyun katmanları ve **web
ile Android hedefleri** orada olduğu gibi çalışıyor — Android tarafı zaten
WSL'den Windows SDK araçlarını interop ile sürebiliyor.

Kaldırılanlar:
- CI'daki `build-windows` işi (MSYS2/MinGW LLVM ile derleme, DLL paketleme,
  Inno Setup adımı) ve release'lerdeki tüm Windows varlıkları
  (`tulpar-windows-x64.exe`, `tulpar-setup-windows-x64.exe`,
  `libtulpar_runtime-windows-x64.a`, beş MinGW/OpenSSL DLL'i).
- `build.bat`, `build.ps1`, `run_tests.bat`, `run_tests.ps1` ve
  `installer/` (Inno Setup betiği + sihirbaz görselleri).
- `COMPILE_ONLY_TESTS` artık **tek yerde**: `build.sh`. Eskiden aynı listenin
  `run_tests.ps1`'deki `$compileOnly` ile elle senkron tutulması gerekiyordu.

**Kaynak içindeki `PLATFORM_WINDOWS` / `_WIN32` dalları bilerek bırakıldı.**
`src/common/platform*.h` shim'lerinden sökmek soket/thread/dl/yol katmanlarına
yayılan büyük ve riskli bir refactor olurdu ve desteklenen platformlarda hiçbir
kazanç sağlamazdı. Bunlar artık **bakımsız ve test edilmemiş** sayılıyor:
hiçbir şey onları derlemiyor ya da çalıştırmıyor.

> **Migrasyon:** Windows'ta `tulpar.exe` kullanan varsa WSL'e geçmeli. Mevcut
> yayınlanmış sürümlerin Windows varlıkları GitHub Releases'te duruyor; yalnız
> yeni sürümler Windows varlığı içermeyecek.

> **Depo ayarı (elle yapılmalı):** `main`'in branch protection kurallarında
> `build-windows` hâlâ **zorunlu status check** olarak listeli. Kaldırılmazsa
> PR'lar hiç raporlanmayacak bir check'i bekleyip kilitlenir.

### Added — 3D oyun motoru: oynanış, his ve dünya geometrisi (Faz 7-9)

Faz 4-6'nın üçü de render'dı (ışık, gölge, doku) + kamera. Sahne iyi görünüyor
ve gezilebiliyordu ama `scene3d`'nin **oynanış tarafı toplayıcı-demo
seviyesindeydi**: bölüm yok, düşman yok, mermi yok, can yok. Bu üç faz motoru
"gezilebilir sahne"den "üstüne oyun yazılabilir"e taşıyor.

**Ön koşul — entity slot geri kazanımı.** `spawn3` ham dizi index'i dönüyor,
`kill3d` yalnız `alive=0` yapıyordu: boş slot asla geri kullanılmıyordu, yani
mermi eklendiği an entity store sonsuza kadar büyürdü. arcade'in
**generation'lı handle** deseni (`gen * 2^20 + slot`) `scene3d`'ye taşındı;
bayat handle sessizce yok sayılıyor, böylece ölmüş bir entity'nin id'sini tutan
kod, o slot yeniden kullanıldıktan sonra yanlışlıkla YENİ entity'yi oynatmıyor.

**Faz 7 — oynanış (saf Tulpar).**
- **Bölüm:** `level3d(n, fn)` / `bolum3d`, `next_level3d()` / `bolum_gec3d()`,
  `goto_level3d`, `won3d()` / `kazandin_mi3d()`. Geçiş **kare sonunda**
  uygulanır (`_lvl_pending3`) — çarpışma döngüsünün ortasında entity store'u
  silmek, o döngünün elindeki slot'ları ayağının altından çekmek olurdu.
  Geçişte entity'ler silinir ama **kancalar korunur**.
- **Düşman AI:** `chase3d` / `takip_et3d`, `patrol3d` / `devriye3d`,
  `in_range3d` / `menzilde3d`.
- **Mermi:** `bullet3d` / `mermi3d` — sahibin baktığı yöne, gövdesinin önünde
  doğar; yerçekimi ve zemin teması işlemez. Ömrü dolan, dünya sınırını aşan ya
  da duvara giren mermi ölür. **Mermi sahibine çarpmaz** (sahip handle olarak
  saklı, karşılaştırma slot üzerinden — bayat handle yanlış eşleşmez).
- **Can/hasar:** `health3d` / `can3d`, `damage3d` / `hasar3d`, `on_death3d` /
  `olunce3d`, `invuln3d`. **Dokunulmazlık penceresi varsayılan 0.6 sn** —
  olmasaydı temas eden düşman 60 FPS'te saniyede 60 hasar vururdu. HUD'da can
  barı (yalnız `health3d` çağrılmışsa görünür).

**Faz 8 — "his" katmanı.**
- **`tm3_billboard` builtin'i** (raylib `DrawBillboard`): her zaman kameraya
  dönük dörtgen. Parçacık küre/kutuyla çizilseydi yandan bakınca incelirdi.
  Dokusuz billboard destekleniyor (içeride 1×1 beyaz doku) — parçacık için
  kullanıcıyı "önce doku yükle" adımına zorlamak anlamsızdı. Işık uygulanmaz:
  parçacık ışık kaynağıdır.
- **`tm3_screen_x` / `tm3_screen_y`**: dünya→ekran izdüşümü. Billboard 3B can
  barı / isim etiketi / hasar sayısı veremez (metin dörtgene sığmaz); izdüşüm
  verir. Yanında `on_hud3d(fn)` / `hud_ciz3` kancası — 2B çizim sahneden sonra.
- **Parçacık havuzu** (saf Tulpar): `particles3d` / `parcacik3d`, `burst3d` /
  `patlat3d`, `particle_gravity3d`. Parçacıklar bilinçli olarak entity DEĞİL —
  oyun onlara tek tek başvurmaz, o yüzden handle makinesi gereksiz. Ölü slot'lar
  geri kullanılır, 400'lük tavan (dolunca yeni parçacık düşer; kare hızı görsel
  şıklıktan önemli).
- **Karakter animasyonu:** `anim3d(id, bosta, kosu, fps)`. `anim_play` builtin'i
  baştan beri vardı ama `scene3d` kullanmıyordu — modelli entity donuk duruyordu.
  Motor kare sayacını sürüyor, "yatay hız > 0.35 ise koşu" seçimi otomatik.
- **Konumsal ses:** `sound3d` / `ses3d`, `sound_range3d`.

**Faz 9 — dünya geometrisi.**
- **Kamera-duvar çarpışması** (varsayılan AÇIK): hedeften kameraya ışın, duvara
  çarparsa kamera içeri çekilir. Kameranın duvarın içinden bakması sanat yönü
  tercihi değil kusurdur (gölge kararıyla aynı gerekçe). `camera_collide3d(false)`
  ile kapanır; açık alanda maliyeti yok.
- **Şekil farkında çarpışma:** küre-küre, küre-kutu (en yakın nokta), silindir
  (dikey kapsül). **İkisi de eksen-hizalı kutuysa eski ucuz AABB yolunda kalır**,
  yani mevcut sahnelerin davranışı değişmez.
- **Dönen kutu (OBB):** XZ düzleminde 4 eksenli SAT. Eksen-hizalı test dönük bir
  duvarda hem yanlış pozitif hem yanlış negatif veriyordu.
- **Rampa / eğimli zemin:** `ramp3d` / `rampa3d`. Dünya artık tek düz düzlem
  değil. Çizim kademeli (raylib'de kama primitifi yok) ama **fizik analitik
  eğimi kullanır**, yani yürüyüş pürüzsüz. Gerçek arazi (heightmap) ayrı bir iş.
- **Katı gövde sistemi:** `Ent3.solid` + `solid3d` / `kati3d`. Duvar itmesi
  eskiden **yalnız `TAG_PLAYER`** için çalışıyordu — düşmanlar duvarların
  içinden geçiyordu. Artık her hareketli katı gövde statik geometriye karşı
  itiliyor, iki hareketli gövde çarpışınca ayrılma **yarı yarıya** paylaşılıyor
  (biri diğerini tek başına sürseydi hafif olan duvara sıkışırdı). Toplanabilir,
  mermi ve rampa katı değil.

Örnek: `examples/scene3d_arena.tpr` — üç fazı birden kullanan 3B arena
(3 bölüm, kovalayan/devriye gezen düşmanlar, parçacık, 3B'ye bağlı can barları,
rampa, dönük duvar, kapsül sütun). Motor **44 headless birim test**.

### Fixed — `assert` sessizce hiçbir zaman başarısız olmuyordu

`lib/test.tpr`'deki `func assert(int cond, str msg)` gövdesinde `cond == 0` ile
bakıyordu. Doğal her kullanım (`assert(x < y, ...)`) bir **bool** geçiriyor ve
bool VMValue'su `int 0`'a eşit çıkmıyor — sonuç: **`assert` hiçbir koşulda
başarısız olmuyordu**, yalnız düz `0` tamsayısı çalışıyordu. Projedeki tüm
`assert(...)` çağrıları boşa çalışıyor, test paketi yersiz yere yeşil yanıyordu.
`func assert(cond, str msg)` + `if (!cond)` ile düzeltildi; ardından ortaya çıkan
gerçek test hataları da giderildi. (Aynı aile: `assert_eq_str`'deki #40 notu.)

### Fixed — SAT ayırma vektörünün işareti tersti

Faz 9'un dönük-kutu çarpışmasında minimum ayırma vektörü **ters yöne**
hesaplanıyordu: `d = merkez_a - merkez_b` olduğu için `dist > 0` iken cisim
zaten +u tarafındadır ve ayırmak için +u'ya itilmelidir; kod -u'ya itiyordu.
Sonuç: oyuncu duvardan dışarı değil **içine** sürülüyor, karşı taraftan
çıkıyordu. `move3d` her karede oyuncunun yaw'ını hareket yönüne çevirdiği için
bu yol pratikte neredeyse her duvar çarpışmasında devreye giriyordu.
Regresyon testi artık itmenin YÖNÜNÜ doğruluyor — "artık çakışmıyor" demek
yetmez, ters işaret de o testi geçer.

### Fixed — Arch/CachyOS gibi dağıtımlarda derleme

`llvm_map_components_to_libnames` statik LLVM bileşen adları üretiyor; Arch'ın
LLVM 22 paketi statik bileşenleri **göndermiyor** (kalan birkaç `.a` da LTO
bitcode olduğu için GNU `ld` okuyamıyor). CMake artık `find_library` ile
**statik bileşenlerin diskte gerçekten var olup olmadığına** bakıyor: varsa
eskisi gibi bileşen bileşen, yoksa paylaşımlı `libLLVM`.

Ölçüt olarak LLVM'in `LLVM_LINK_LLVM_DYLIB` bayrağı **yetmiyor** — MSYS2/MinGW
LLVM 22 paketinde bayrak açık ama statik bileşenler de var; bayrağa uyulunca
`tulpar.exe` `libllvm-22.dll`'e bağlanıyor ve CI'ın DLL paketleme koruması
düşüyor. Windows dağıtımı kendi kendine yeten tek dosya olmalı.

### Added — 3D kamera: yörünge ve birinci şahıs (Faz 6)

3B sahnenin kamerası bugüne kadar **hiç dönmüyordu**: oyuncunun sabit +Z
arkasında durup onu izliyordu, dolayısıyla W tuşu her zaman dünya ekseninde
-Z demekti. Oynanabilir tek tür, tek açıdan izlenen sahnelerdi — oyuncu
etrafına bakamıyor, arkasını dönemiyordu. Faz 6 kamerayı serbest bırakıyor.

- **Üç kamera modu** (`scene3d`): `camera_follow(hedef, mesafe, yukseklik)`
  eskisi gibi SABİT; `camera_orbit(...)` / `kamera_yorunge(...)` üçüncü şahıs
  yörünge; `camera_fps(hedef, goz_yuksekligi)` / `kamera_fpv(...)` birinci
  şahıs. Yörünge, `camera_follow` ile **birebir aynı açıdan başlar** (aynı
  mesafe/yükseklik küresel koordinata çevrilir) — fark, artık döndürebilmen.
  Birinci şahısta hedef entity **çizilmez** (kamera gövdenin içindedir) ve
  gövde bakış yönüne döner.
- **Hareket artık kameraya göre:** `move3d` yön girdisini kamera yaw'ıyla
  döndürüyor — "ileri" kameranın baktığı yön. Sabit kamerada yaw hep 0 olduğu
  için dönüşüm birim matris kalır, yani **mevcut sahnelerin kontrolü ve
  görüntüsü değişmez**.
- **Bakış girdisi üç yoldan:** fare (birinci şahısta imleç kilitli, sürekli;
  yörüngede SAĞ tuş basılıyken sürükleme), ekranın **sağ yarısına parmakla
  sürükleme** (mobil — dokunup çekmek hâlâ zıplatır, çünkü zıplama parmak
  KALKINCA ve parmak kaymadıysa tetiklenir), ve **Q/E** klavye yedeği
  (tarayıcı Pointer Lock vermediğinde de kamera çevrilebilsin). Tekerlek
  yörüngede yakınlaştırır. Eğim ±85°'de kırpılır (kamera takla atmasın),
  yörüngede üst sınır 25° ve kamera zeminin altına düşürülmez.
- **Yeni API:** `camera_sens3d` / `camera_touch_sens3d` (hassasiyet),
  `camera_look3d(yaw, pitch)` (sahneyi çerçevele), `camera_yaw3d()` /
  `camera_pitch3d()` / `camera_dist3d()` (okuma), `camera_zoom3d(min, max)`,
  `camera_lock3d(on)` (imleç kilidi) — hepsinin Türkçe alias'ı var.
  `scene3d_reset()` kamerayı da sıfırlar.
- **4 yeni tame builtin:** `tm_mouse_dx` · `tm_mouse_dy` (kare içi fare
  deltası — imleç kilitliyken `mouse_x()` sabit kaldığı için bakışın tek girdi
  kaynağı) · `tm_cursor_lock` · `tm_cursor_locked`. Sarmalayıcılar:
  `mouse_dx`/`fare_dx`, `mouse_dy`/`fare_dy`, `cursor_lock`/`imlec_kilitle`,
  `cursor_locked`/`imlec_kilitli`. Web'de imleç kilidi tarayıcının Pointer
  Lock'ıdır; pencere yokken (headless) sessiz no-op.
- Örnek: `examples/scene3d_camera.tpr` — 1/2/3 ile üç mod canlı değiştirilir.

### Fixed — tuş sorgusu sayısal kodu yok sayıyordu

`key_down(87)` gibi **ham raylib tuş kodu** verilen çağrılar her zaman `false`
dönüyordu: binding argümanı yalnız string ad ("W", "SPACE") olarak açıyor,
sayı gelince `NULL`'a düşüp tuşu "basılı değil" sayıyordu. Kendi tuş
sabitlerini tutan `scene3d`'nin (`K_W = 87`, `K_LEFT = 263` …) **tüm klavye
girdisi bu yüzden ölüydü** — 3B sahneler yalnız dokunmatikle sürülebiliyordu.
`tm_key_down` / `tm_key_pressed` / `tm_key_released` artık ad VEYA kod kabul
ediyor (tip runtime'da ayrılıyor).

### Added — 3D doku, gökyüzü ve materyal (Faz 5)

3B sahneler artık **düz renk olmak zorunda değil**: yüzeylere doku döşenebiliyor,
arkalarında gradyan bir gökyüzü var ve parlaklıkları ayarlanabiliyor.

- **Doku bir DURUM'dur:** `texture3d(t, tile_u, tile_v)` / `doku3d(...)` bir kez
  ayarlanır, sonraki her primitif (kutu/küre/silindir/düzlem) onu kullanır;
  `no_texture3d()` / `doku3d_kapat()` düz renge döner. `tile`, dokunun yüzey
  boyunca kaç kez tekrarlanacağı — 60×60 birimlik bir zemine tek bir 128×128
  karo dokusunu 24×24 döşemek böyle olur. Renk (çizim çağrısının son
  parametresi) dokuyla **çarpılır**: beyaz ver, doku olduğu gibi çıksın; renkli
  ver, dokuyu boya. Modeller kendi dokularını korur (`model_texture`) — GLB
  materyalini ezmek yıkıcı olurdu.
- **Prosedürel damalı doku:** `checker(w, h, cells, c1, c2)` / `damali(...)`
  dosyasız doku üretir ve normal bir doku handle'ı döner (2B'de de kullanılır).
  Motorun geri kalanı (gömülü shader'lar, `gen_*` primitifleri) gibi asset
  gerektirmeme çizgisinde: kimse basit bir zemin karosu için PNG taşımasın.
- **Materyal:** `material3d(shine, spec)` / `materyal3d(...)` — `shine`
  specular üssü (büyük = küçük ve keskin parlama, "cilalı"; küçük = geniş ve
  yayvan, "mat"), `spec` parlamanın gücü (0 = tamamen mat). Hazır iki uç:
  `mat3d()` / `matte3d()` ve `parlak3d()` / `glossy3d()`. Varsayılan 16 / 1.0,
  yani **Faz 5 hiçbir mevcut sahnenin görüntüsünü değiştirmez.**
- **Gökyüzü:** `sky(tepe, ufuk)` / `gokyuzu(...)`, `sky_off()` /
  `gokyuzu_kapat()`. Cubemap veya 6 resim yok — kameranın konumunda duran büyük
  bir kürenin rengi **bakış yönüne** göre hesaplanıyor, yani yukarı bakınca
  zenit, aşağı bakınca ufuk rengi geliyor (2B gradyan arka planın yapamadığı
  şey bu). Küre derinliğe yazmaz, sahnenin geri kalanından önce çizilir.
- **`scene3d` entegrasyonu:** `sky3d(tepe, ufuk)` / `gokyuzu3d(...)` ve
  `ground_texture3d(t, tile)` / `zemin_doku3d(...)`. Işık ve gölgenin aksine
  gökyüzü **varsayılan kapalı** — o bir sanat yönü kararı, algı düzeltmesi
  değil; karanlık/stilize bir sahneye zorla açık mavi gökyüzü koymak yanlış
  olurdu. Zemine doku verilince ızgara çizgileri çizilmez (doku zaten ölçeği
  gösteriyor).
- **Mesafe sisi:** `fog(renk, yogunluk)` / `sis(...)`, `fog_off()` /
  `sis_kapat()`. Üssel-kare eğri — yakında hiç yok, uzakta hızlı doyuyor; düz
  doğrusal sisin "her şey biraz soluk" görüntüsünü vermez. Sisin doğru rengi
  gökyüzünün **ufuk** rengidir (uzaktaki cisim arkasındaki gökyüzüne
  karışmalı, başka bir renge değil — yoksa "kirli cam" gibi görünür), o yüzden
  `fog_sky(yogunluk)` / `sis_gokyuzu(...)` rengi `sky()`'den kendisi alır. Sis
  ışık shader'ında hesaplandığı için `lights_off()` ile ışık kapatılırsa sis de
  çizilmez (gölgeyle aynı bağımlılık). `scene3d`'de `fog3d(yogunluk)` /
  `sis3d(...)`, varsayılan kapalı.
- Örnek: `examples/tame3d_texture.tpr` (BOŞLUK / dokunuş ile dokuyu aç-kapat;
  aynı küre solda parlak, sağda mat).

**7 yeni builtin:** `tm3_texture` · `tm3_material` · `tm3_sky` · `tm3_sky_off` ·
`tm3_fog` · `tm_checker` (+ Faz 4b'den `tm3_shadows_active`). Masaüstü GL 3.3,
web GLES2 ve Android GLES2'nin üçünde de derleniyor.

### Added — 3D gölgeler (shadow mapping)

Yönlü ışık (güneş) için **gölge haritalama**: nesneler zemine ve birbirlerine
gölge düşürüyor — "havada yüzüyor" hissi gidip "yerde duruyor" hissi geliyor.
Yumuşak kenar için 3×3 PCF, yüzeyin kendini gölgelemesine (shadow acne) karşı
eğime göre bias + gölge geçişinde ön-yüz ayıklama.

- **3 yeni builtin:** `tm3_shadows` (aç/kapat) · `tm3_shadow_area` (kapsanan
  alanın yarı-genişliği) · `tm3_shadows_active` (gölge GERÇEKTEN çalışıyor mu).
  Sarmalayıcılar: `shadows_on`/`golge_ac`, `shadows_off`/`golge_kapat`,
  `shadow_area`/`golge_alani`, `shadows_active`/`golge_aktif`.
- **Gölge haritası bir RENK dokusudur, derinlik dokusu değil:** derinlik
  shader'da RGBA8'e paketleniyor. Sebep aşağıda (GLES2) — bu yöntem masaüstü,
  web ve Android'de aynı kod yolunu kullanır.
- **Dürüst durum bildirimi:** `shadows_on()` çağırmış olmak gölgenin çalıştığı
  anlamına gelmez. FBO kurulamazsa gölgeler kapatılıp açık bir mesaj basılıyor,
  **ışıklandırma çalışmaya devam ediyor**, ve `shadows_active()` oyun koduna
  gerçeği söylüyor — demo bunu ekranda gösteriyor.
- Örnek: `examples/tame3d_shadows.tpr` (BOŞLUK / dokunuş ile aç-kapat; zıplayan
  kürenin gölgesi büyüyüp küçülür).
- Teşhis: `-DTAME_SHADOW_DEBUG` ile derlenirse gölge haritası ekranın sol
  üstünde gösterilir (varsayılan kapalı).
- **`scene3d`'de gölgeler VARSAYILAN AÇIK** (ışık gibi) — motoru kullanan oyun
  sıfır konfigürasyonla gölgeli görünür. Işık kapalıysa gölge de çizilmez.
  `golge3d(false)` / `shadows3d(false)` ile kapanır,
  `golge_alani3d(a)` / `shadow_area3d(a)` ile alanı daralır (varsayılan 24.0;
  küçültmek gölgeyi keskinleştirir).

**Mobil/GLES2'de dört ayrı hata çıktı, hepsi düzeltildi** — masaüstünde
şans eseri gizlenmişlerdi:

1. **Alfa harmanlama paketlenmiş derinliği bozuyordu.** raylib alpha blending'i
   varsayılan açık tutar; derinliği RGBA'ya paketlerken alfa kanalı da veri
   taşıdığı için rastgele bir opaklık gibi yorumlanıp her fragment arka planla
   harmanlanıyordu → benekli gölge haritası, noktalı/kayıp gölge. Derinlik
   geçişinde `rlDisableColorBlend()`.
2. **`mediump` yetmiyordu.** Derinlik karşılaştırması ~10 bit mantiste bozuluyor;
   gölge cismin üstünde tutuyor ama geniş zemin düzleminde hiç oluşmuyordu.
   Fragment shader `GL_FRAGMENT_PRECISION_HIGH` varsa `highp`. (Masaüstü GPU'lar
   `mediump`'ı zaten fp32 işlediği için web'de sorun görünmüyordu.)
3. **GLES2 yalnız-derinlik FBO'yu kabul etmiyordu.** Android emülatörü
   `OES_depth_texture`'ı DESTEKLEDİĞİ HALDE framebuffer "incomplete attachment"
   veriyordu (raylib boyutlu iç format kullanıyor; katı GLES2'de
   internalformat == format olmalı). Çözüm: renk dokusu + derinlik
   renderbuffer'ı.
4. **Paketlenmiş derinlik interpolasyona gelmez.** Bilinear örnekleme iki komşu
   paketin ortalamasını alıp anlamsız derinlik üretiyordu → `NEAREST` + `CLAMP`.

Mimari not: gölge sahneyi **iki kez** çizmeyi gerektirir (ışığın gözünden
derinlik + kameradan normal geçiş), ama tame'de çizimler `space_begin`/
`space_end` arasında anında yapılıyordu. Artık **gölge açıkken** çizimler bir
listeye kaydedilip `space_end` iki geçiş halinde oynatıyor; **gölge kapalıyken
eski anında-çizim yolu birebir korunuyor** (sıfır maliyet).

### Added — 3D ışıklandırma (Faz 4)

3D sahneler artık **ışık alıyor**: düz renkli geometri yerine gölgeli yüzeyler,
specular parlama ve yüze göre parlaklık. Blinn-Phong shader **kaynak içine
gömülü** (`LoadShaderFromMemory`) — `.vs/.fs` asset'i taşınmıyor, dolayısıyla
web/Android paketlerine ek dosya girmiyor ve "oyunu kopyaladım, ışık gitti"
sınıfı hata mümkün değil. Masaüstü (GLSL 330) ve GLES2 (GLSL 100, web+Android)
varyantları ayrı gömülü.

- **4 yeni builtin:** `tm3_lights` (aç/kapat) · `tm3_light_set` (slot 0-3,
  yönlü veya nokta) · `tm3_light_off` · `tm3_ambient`. Tulpar sarmalayıcıları
  çift dilli: `lights_on`/`isik_ac`, `sun`/`gunes`, `point_light`/`nokta_isik`,
  `dir_light`/`yonlu_isik`, `light_off`/`isik_sil`, `ambient_light`/`ortam_isik`.
- **En çok 4 ışık**; nokta ışıklar **mesafeye göre zayıflıyor** (yakındaki
  ışığın sahneyi patlatmasını önler). Işık açıldığında hiç ışık tanımlı değilse
  makul bir güneş otomatik verilir — "ışığı açtım, ekran simsiyah" tuzağı yok.
- **`scene3d` motorunda ışık VARSAYILAN AÇIK** (güneş + ortam ışığı): motoru
  kullanan oyun sıfır konfigürasyonla gölgeli görünür. `lights3d(false)` /
  `isik3d(false)` ile kapatılıp eski düz-renk görünüme dönülebilir.
- Örnek: `examples/tame3d_lights.tpr` (BOŞLUK ile ışık aç/kapat — aynı sahnenin
  iki hali).

Uygulama notu (iki raylib gerçeği bu tasarımı zorunlu kıldı): `BeginShaderMode`
rlgl'in anlık shader'ını değiştirir ama `DrawMesh` `material.shader` kullanır →
**modeller BeginShaderMode'dan etkilenmez**, materyallerine ayrıca atanır. Ve
`DrawSphereEx`/`DrawCylinder` **normal üretmez** → ışık altında yanlış
gölgelenirdi; bu yüzden ışık açıkken küre/silindir/düzlem/kutu, normal'i doğru
olan **cached birim mesh'ler** üzerinden `DrawModelEx` ile çizilir (ışık
kapalıyken eski immediate-mode yolu birebir korunur).

### Fixed

- **Web (wasm) derlemesi `tame_impl.c`'yi platform bayrakları olmadan
  derliyordu** (`android/build_tame_android.sh` veriyordu, `wasm/build_tame_web.sh`
  vermiyordu). Sonuç: ışık shader'ı web'de sessizce MASAÜSTÜ varyantını seçiyor
  ve `'in' : storage qualifier supported in GLSL ES 3.00 and above only` ile
  derlenmiyordu — sahne ışıksız çiziliyordu. Script artık Android ile aynı
  bayrakları geçiyor; ayrıca shader seçimi `__EMSCRIPTEN__`/`__ANDROID__` gibi
  derleyicinin kendi tanımlarına da bakıyor, tek bir build bayrağına güvenmiyor.

### Added — 3D oyun katmanı (tame3d + scene3d)

TulparLang artık **3D oyun** yapabiliyor. Vendored raylib'in zaten derlenen 3D
modülü (`rmodels.c`, kamera/mesh/model) `tm3_*` binding ailesiyle açığa çıkarıldı
ve üzerine saf-Tulpar bir preset motoru (`scene3d`) kondu. Web (WASM/WebGL) +
masaüstü + Android link doğrulandı; sahneler headless Chrome/CDP ile görsel teyit
edildi.

- **~28 yeni `tm3_*` builtin** (5-nokta bağlı, çift dilli TR/EN sarmalayıcı,
  `tm3_` codegen prefiks kapısı):
  - **Faz 0 — temel:** `camera3d` · `space_begin/end` · `cube` · `cube_wires` · `grid`.
  - **Faz 1 — primitifler + raycast:** `sphere` · `sphere_wires` · `cylinder` ·
    `plane` · `line3d` · **`pick_box`/`pick_sphere`** (ekran ışını ile tıklama-seçim)
    · `vec3_len`/`vec3_dist`.
  - **Faz 2 — modeller + animasyon:** `load_model` (OBJ/GLTF/GLB/IQM) · **7 `gen_*`
    prosedürel şekil** (cube/sphere/plane/cylinder/torus/cone/knot) · `draw_model` ·
    `draw_model_rot` · `model_texture` · **iskelet animasyonu** (`anim_count`/
    `anim_frames`/`anim_play`) · `unload_model`. Model registry doku/font registry
    deseniyle; `close()`'ta otomatik boşaltma.
- **`lib/scene3d.tpr` — 3D preset motoru (arcade'in kardeşi, saf Tulpar).**
  Modern `struct Ent3` **dizisi** (paralel-dizi mirası yok — struct-in-array +
  alan-yazma artık çalışıyor). Motor her kare: girdi → hareket → yerçekimi/zemin →
  **AABB çarpışma + duvar itme (MTV)** → takip kamerası → shape'e göre çizim → HUD.
  Kullanıcı yalnız `setup`/`update`/çarpışma kancası yazar (`on_hit3d`,
  `me3d()`/`other3d()` bağlamı). "~40 satırda 3D toplayıcı".
- **Örnekler:** `examples/tame3d_cube.tpr` (Faz 0), `tame3d_primitives.tpr` (Faz 1),
  `tame3d_models.tpr` (Faz 2), `scene3d_collector.tpr` (Faz 3 — yürü/zıpla/topla).
- **Doğrulama:** scene3d motoru **8/8 headless birim test** (yerçekimi, zemin,
  zıplama, toplama+skor, duvar itme); 4 sahne WebGL'de görsel; Android `.so`
  (arm64-v8a + x86_64) `-Wl,--no-undefined` ile linklendi.
- Prebuilt arşivler (`wasm/dist`, `android/dist/<abi>`) yeni `tm3_*` sembolleriyle
  yeniden derlendi; `kMaxFunctions` 1024 (launcher + 3D için).

Third mobile wave — full app shell, per-game juice, three new games, and a
full-stack global leaderboard. All verified live on the Android emulator.

### Added (third wave)
- **Per-game effects + centralized audio across all games.** Every arcade game
  now sprays a `patlama()` particle burst at its scoring/collision/death points
  (10 games). Sound + haptics are centralized in the engine (DRY): `skor_ekle`
  blips, `game_over` plays a lose tone + death rumble, level-up / win / new-record
  each get their own tone — so all games gained audio with one edit, not ten.
- **App shell.** In-app **Settings** screen (Sound / Haptics / Language TR↔EN
  toggles, persisted via `save_data`, live-retranslates the whole shell UI),
  a **Pause** overlay (Resume / Restart / Menu, opened with the Android **Back**
  button / ESC), and a gear button on the menu. `sound_on()/ses_ac()`,
  `haptics_on()/titresim_ac()` gate every engine beep/vibrate.
- **Juice.** Floating `+N` score popups (anchored at the collision point),
  a purely-visual **combo/streak** indicator (`SERI x3` — never changes the
  score, so best-scores stay fair), and a short **menu↔game fade** transition.
- **Three new games** (13 total in the launcher): **2048** (swipe/arrow merge,
  colored tiles), **Pong** (vs a beatable AI, endless rally = score), and
  **Vur** (whack-a-mole reaction, pure tap, speeds up).
- **Global leaderboard (full-stack Tulpar).** `examples/arcade_app/skor_sunucu.tpr`
  is a Tulpar Wings server (`POST /skor`, `POST /tablo` → top-10). The engine's
  `skor_tablosu_url(url)` + `oyuncu_adi(name)` make every game submit its score
  on game-over and fetch + render the global top-5 on the game-over screen — a
  Tulpar game talking to a Tulpar backend, verified end-to-end (emulator →
  server). Off by default (`_lb_url==""` → no network); the launcher enables it
  from the `TULPAR_LB_URL` env var at generation time.

### Added (third wave, follow-ups)
- **Per-game music themes + larger touch targets + 3 more browser games.**
  `music_theme(n)` / `muzik_tema(n)` picks one of three in-game tracks (default,
  tense, bouncy); shooters/space use the tense theme, the platformer the bouncy
  one. Touch **hit-areas** are enlarged without changing the visuals (direction
  buttons and the action button are more forgiving to tap). The three endless
  games (2048, Pong, Whack/Vur) gained **English twins** and joined the bilingual
  browser gallery — 14 games now, each TR + EN. Diagonal top-down movement is
  normalized (was ~41% faster diagonally) and the joystick dead-zone is tighter.
- **Progression: per-level stars + level-select + badges (all games unlocked).**
  Tapping a leveled game now opens a **level-select screen** first — a grid of its
  levels, each showing a gold star if completed — and you pick which level to play
  (or Back). **Each level earns its own star** the moment you clear it, persisted
  per level (`arcade_lvl_<game>_<k>.txt`); the launcher card shows **completed/total**
  (e.g. ★ 4/6). Endless games (2048/Pong/Vur) have no levels, so a tap plays
  immediately and their card keeps a 0–3 star rating from score thresholds
  (`yildiz_hedef(a,b,c)` / `star_goals`). A **trophy button** on the menu opens a
  **Badges** screen with 5 local achievements — First Win, Record Breaker, Three
  Stars (3 total), Collector (15), Master (30) — each persisted, with a toast +
  jingle when newly earned. No locks: any game is always playable, and you can
  replay any earlier level. Level counts + completion are discovered at launcher
  start by probing each game's (side-effect-free) setup. Verified headless
  (`tests/arcade_progression.test.tpr`, 4/4: per-level marking, start-at-level,
  badge unlock+persist, endless thresholds). The launcher's function table also
  outgrew the old 512-symbol AOT cap (13 namespaced games + arcade + tame); raised
  to 1024 in `llvm_backend.{hpp,cpp}`.
- **Procedural background music (fileless chiptune).** A new volume-controlled
  synth builtin `tm_tone(freq, ms, vol)` (the `tm_beep` sibling; `beep` = full
  volume) drives an arcade music sequencer that plays a soft looping melody —
  a calm A-minor arpeggio on the menu/settings/pause screens and an upbeat
  pentatonic loop during active play — timed against `elapsed()` so tempo is
  frame-rate-independent. Notes play *under* the SFX (≈0.26 volume) so effects
  stay audible. No audio files → **zero APK-size cost**. Toggle via the new
  **Music / Muzik** Settings row (persisted in `arcade_mus.txt`), independent of
  the SFX (Sound) toggle; public `music_on()/muzik_ac()`. Settings now has 6 rows
  (Sound / Music / Haptics / Language / FPS / Records), re-spaced to fit.
- **Config-driven build target — `tulpar build` reads `tulpar.toml`.** A new
  `[build]` section (`target = "desktop"|"web"|"android"|"apk"|"aab"`, plus
  optional `entry` and `output`) lets a bare `tulpar build` (no flags, no file)
  pick the target, source, and output name from the manifest. CLI flags still win
  (`--target=…`, positional src/out override the manifest). So updating the mobile
  app is now just `cd examples/arcade_app && tulpar build` — which produces the
  signed `tulpar-arcade.apk` with the correct `dev.tulparlang.arcade` / "Tulpar
  Arcade" identity, instead of remembering the long `--target=android … out_apk`
  invocation (which silently produced a generic app).
- **More levels — every leveled game is now 6 levels (was 3).** +3 levels each
  for topla, zipla, nisan, tugla, uzay, labirent, karsiya, ucus, goktasi, yilan
  (2048/Pong/Vur stay endless by design), with escalating difficulty. Parameter
  games ramp tempo/speed/target; layout games get new hand-built boards. The
  labirent boards (M4–M6) are generated by a recursive-backtracker maze generator
  and **verified solvable by flood-fill** (every key reachable, every patrol on a
  horizontal corridor) — no unbeatable levels. Platformer (zipla) boards respect
  the documented jump-arc limits (≤80px vertical / ≤140px horizontal).
- **Fixed joystick.** The touch joystick base is now anchored at a fixed
  bottom-left position instead of spawning under (and drifting with) the finger —
  on some phones it could creep toward the screen center and the player's hand
  would block the view. Direction is read as the finger's offset from the fixed
  center; the base no longer moves. Its activation area is now a small circle
  around the fixed base (was the whole bottom-left quadrant), so touching the
  screen center — or anywhere off the joystick — no longer spawns a stick.
- **Professional game-over / win screen.** Ending a game now shows a full-screen
  overlay with the big score, best/record line, optional global top-4, and two
  buttons — **Tekrar Oyna / Play Again** and **Ana Menu / Menu** (in standalone
  `play()` the second is **Cikis / Quit**). Tapping the screen at random no longer
  restarts the game (only the buttons act; keyboard R/Enter = restart, Esc/Back =
  menu). A finished game can no longer be paused. The three go-to-menu paths
  (home button, pause→Menu, game-over→Menu) share one `_lx_goto_menu()` helper.
- **FPS counter (Settings-toggled).** A 4th Settings row (**FPS**, off by
  default, persisted via `arcade_fps.txt`) shows the live frame rate top-center
  during play (`tm_fps()` = raylib `GetFPS`; no C++ change needed). Public
  `show_fps()/fps_goster()`.
- **Reset records (Settings).** A 5th Settings row (**Rekorlar / Records**) wipes
  every game's persisted best score, with a two-tap confirm (`Sil`→`Emin?`→wiped)
  so it can't fire by accident; menu best-badges clear immediately.

### Fixed (third wave)
- **Launcher HOME↔gear tap cascade.** The in-game HOME button and the menu's
  gear (Settings) button share the top-right corner; tapping HOME used to
  cascade into opening Settings the same frame. Going home now consumes the
  touch (`_ar_touch_prevn`). Caught by on-emulator testing.

### Changed (third wave)
- **Pong now uses the vertical (▲▼) touch scheme** instead of the 4-button dpad —
  a paddle only moves up/down, so the horizontal buttons were dead. `kontrol_semasi("dikey")`.
- **2048 is swipe-only on mobile** (`kontrol_semasi("yok")`): no on-screen
  buttons, the swipe gesture drives it (arrow keys still work on desktop).

---

Second mobile wave — "juice", real device sensors, and store-ready packaging.
Every item below was verified live on the Android emulator (screencap /
logcat / dumpsys / bundletool validate).

### Added
- **Particle + screen-shake + flash effects in `arcade` (pure Tulpar).**
  `patlama(x,y,renk)` / `explode` (+ `patlama_n` count variant) spray
  gravity-affected sparks from a slot-recycling parallel-array pool; `sars()`
  / `shake()` (damped random offset — the whole scene shudders) and `parla()`
  / `flash()` (white full-screen). `game_over()` now auto-triggers shake+flash,
  so all ten games gained impact feedback with zero code changes. Effects keep
  animating on the game-over screen. `sars_x()/sars_y()` expose the offset for
  games that draw their own scene (snake).
- **Persistent per-game high scores in `arcade`.** The engine saves each game's
  best score (`save_data`, keyed by scene/launcher title) and shows a gold
  "NEW RECORD!" / "En iyi: N" badge on the game-over screen **and** on the
  launcher menu cards (loaded once at launch, no per-frame IO). Survives cold
  boot + reinstall (verified).
- **F1 — accelerometer / tilt controls.** `tm_accel_x/y/z`,
  `tm_accel_available` (NDK `ASensorManager`, **no JNI**, callback-driven event
  queue drained through raylib's existing looper poll) + `accel_x()/egim_x()`
  … wrappers; arcade `kontrol_semasi("egim")` (scheme 6) reads tilt as a
  **deviation from a captured neutral baseline** (auto-calibrates to however
  the phone is held, so gravity doesn't false-trigger). Desktop/web return 0 →
  keyboard still drives.
- **F2 — `tm_beep(freq, ms)` / `beep()` / `bip()`.** Fileless SFX: synthesises a
  sine wave (with attack/decay envelope) on the fly and plays it through a
  round-robin `Sound` pool — short game sounds with no asset shipping. Same on
  desktop/web/Android (pure raylib audio); AAudio stream open verified on device.
- **G1 — screen stays awake.** `AWINDOW_FLAG_KEEP_SCREEN_ON` set alongside
  fullscreen in the vendored `rcore_android.c` (TULPAR PATCH); the device no
  longer dims/sleeps mid-game (`dumpsys` shows `fl=KEEP_SCREEN_ON`).
- **G2 — background lifecycle.** On `APP_CMD_LOST_FOCUS`/`GAINED_FOCUS` the
  Android backend now `SetMasterVolume(0/1)` so audio mutes when the app is
  backgrounded and resumes on return (the loop already freezes = auto-pause).
  `tm_active()` / `aktif_mi()` exposes foreground state to game code.
- **G3 — splash background + adaptive/round icon.** The driver writes a
  `TulparSplash` theme (window background = splash colour, killing the black
  cold-start flash) plus an `res/mipmap-anydpi-v26/ic_launcher.xml`
  adaptive-icon (foreground = your PNG, background = splash colour) and
  `android:roundIcon`. Colour via `tulpar.toml [android] splash_color="#RRGGBB"`.
- **G4 — `tulpar build --aab`.** Emits a Play-Store-ready **Android App
  Bundle** via `android/package_aab.sh` (aapt2 `--proto-format` link →
  bundletool `build-bundle` → jarsigner). bundletool is invoked from the
  gradle-cached jar with an assembled classpath when no fat jar is present;
  `bundletool validate` passes (base module carries both ABIs + adaptive icon).
- **H1 — synchronous HTTP on Android.** `INTERNET` permission is now always
  written to the manifest; the existing blocking `http_request`/`http_get`
  works on-device over bionic sockets (verified: live `GET` returns status 200).
- **`tm_view_left/right/top/bottom` + `view_left()`/`ekran_sol()` … wrappers.**
  Report the visible screen's edges in world coordinates.

### Changed
- **Touch controls now anchor to the real screen edges (Android camera model).**
  Previously raylib letterboxed the scene and the ortho projection was the game
  world's size, so the on-screen joystick/buttons were cramped into the world's
  small inner area instead of reaching the phone's real corners. Now on Android
  `window(w,h)` opens **fullscreen** and centres the `w×h` world via a
  `Camera2D` (zoom+offset); `frame_begin/end` wrap `BeginMode2D/EndMode2D` and
  touch/mouse read back through the inverse transform — **no game code
  changes**. arcade anchors the joystick, ◀▶/dpad/vertical buttons, action
  button, home button and full-screen flash to `tm_view_*` (the real
  bottom-left / bottom-right / top-right corners). Desktop/web keep the camera
  off → byte-identical behaviour.
- `examples/arcade_uzay.tpr` (+ EN `invaders`) burst an orange explosion when an
  invader is shot; `examples/arcade_yilan.tpr` sparks gold when food is eaten.

## [v3.12.0]

Tulpar goes properly mobile: the arcade launcher becomes the official
ten-game **Tulpar Arcade** app, Android packaging collapses to a single
`tulpar build --apk` command with full app identity from `tulpar.toml`,
games gain assets/sound, persistent saves, haptics, swipe gestures,
per-game touch control schemes and hardware-back handling — every item
verified live on the Android emulator.

### Changed
- **Modern mobile touch controls in `arcade` — dynamic analog joystick +
  context-aware action button.** The fixed 4-arrow D-pad is replaced by a
  **floating analog joystick** (circle-within-a-circle): pressing anywhere in
  the movement zone spawns the base under the finger, the knob follows the
  finger, and the base *trails* the finger past the ring so a long drag never
  loses control — direction persists as long as the finger stays down (modern
  virtual-stick feel). The analog vector is thresholded into the same 8-way
  booleans the movement presets already consume, so every game and the desktop
  keyboard path are unchanged. The **action/fire button now only appears in
  games that actually read it** (`action_pressed`/`action_held`/`fire_pressed`
  or the platformer jump preset lazily flag intent) — collector/dodge/maze
  games (Topla, Tugla, Labirent, Karsiya, Goktasi) no longer show a dead
  button. **The joystick is context-aware too**: it only appears in games that
  read directional input, so a tap-only game (Ucus/flight) shows just the
  action button. And the joystick is **corralled to a bottom-left region** —
  the base spawns under the finger clamped into that region and stays fixed
  until release, so it can never wander across the screen. New public
  direction readers `left()/sol()`, `right()/sag()`, `up()/yukari()`,
  `down()/asagi()` (keyboard OR joystick) let games that drive movement
  manually join in: Tugla/Uzay/Goktasi (+ EN twins breakout/invaders/dodge)
  switched from raw `key_down("LEFT")` to them — making those games
  touch-playable for the first time — and Ucus/flight + shooter/invaders
  fire now goes through `fire_pressed()`. The launcher resets both flags per
  game so one game's controls don't linger on the next. Verified on the
  Android emulator: Nisan shows joystick+fire, Topla joystick-only, Ucus
  button-only (tap flaps), and Tugla's paddle moves by touch with the
  joystick pinned to the bottom-left corral.
- **Per-game control schemes (`kontrol_semasi` / `control_scheme`).** A
  joystick doesn't fit every game, so the touch layout is now a preset picked
  per game: `"joystick"`, `"dpad"` (4 arrow buttons in a cross), `"yatay"`/
  `"horizontal"` (◀ ▶ only), `"dikey"`/`"vertical"` (▲ ▼ only), `"yok"`/
  `"none"`, or the default `"auto"` — which infers the layout from the
  directions the game actually reads: left/right only → ◀ ▶ buttons, up/down
  only → ▲ ▼, both axes → joystick, none → no movement control. Buttons are
  round, semi-transparent, bottom-left, with pressed-state highlight, and
  hold-to-move semantics. Labirent + Karsiya (and EN maze/crossing) opt into
  `"dpad"`; everything else relies on auto (Tugla/Uzay/Goktasi/Zipla get
  ◀ ▶, Topla/Nisan the joystick, Ucus nothing but the flap button). The
  launcher resets the scheme per game; `tools/make_arcade_launcher.py` folds
  `kontrol_semasi`/`control_scheme` into generated setups. Verified on the
  Android emulator: Tugla shows ◀ ▶ (auto) and the held ▶ drives the paddle,
  Karsiya shows the 4-button dpad with ▲ climbing lanes, Topla still gets the
  joystick. A companion `dokunuldu()`/`tapped()` reader (any-finger tap edge)
  lets tap-only games use the whole screen as the control: Ucus/flight flaps
  on a tap anywhere — no button, no joystick, nothing drawn. Nisan/shooter and
  Goktasi/dodge pin `"yatay"`/`"horizontal"` explicitly (◀ ▶ buttons; Nisan
  keeps the fire button next to them).

### Added
- **One-command Android packaging + app identity (`tulpar build --apk`,
  `tulpar.toml [android]`).** `tulpar build --apk game.tpr out` now goes all
  the way to an installable **signed `out.apk`** in a single command (it
  implies `--target=android`, then drives `android/package_apk.sh` — aapt2 →
  zipalign → apksigner — automatically; `TULPAR_ANDROID_TOOLS` points at the
  script dir for non-repo layouts). App identity comes from a new
  `[android]` section in `tulpar.toml`: `package` (applicationId — finally
  lets two Tulpar games install side by side; default stays
  `dev.tulparlang.game`), `name` (launcher label), `icon` (a PNG the driver
  copies into `res/mipmap/ic_launcher.png` and package_apk.sh compiles via
  `aapt2 compile`), `orientation` (`landscape`/`portrait`/`sensor`), and
  `version_code`/`version_name`. Release distribution: pointing
  `TULPAR_ANDROID_KEYSTORE` (+ `_KS_PASS`, `_KEY_ALIAS`, optional
  `_KEY_PASS`) at your own keystore signs with it instead of the debug key —
  a Play-Store-uploadable APK. Verified on the emulator: a
  `dev.tulparlang.arcade` / "Tulpar Arcade" build with a custom icon
  installed **alongside** the default-identity APK, shows its own icon+label
  in the app drawer, and `apksigner verify` prints the release certificate.
- **Game assets inside the APK — Android games get sound and sprites.**
  `[android] assets = "<dir>"` in `tulpar.toml` (or `TULPAR_ANDROID_ASSETS`,
  mirroring the web target's `TULPAR_WEB_ASSETS`) stages a directory into the
  APK's `assets/` **preserving the path as written** — the same relative
  paths the game uses on desktop (`load_texture("assets/top.png")`) resolve
  on-device through raylib's AAssetManager integration, so no code changes
  per platform. Files are added to the zip by the packer itself rather than
  `aapt2 -A` because Windows aapt2 writes backslashed entry names that
  AAssetManager can never match. Verified on the emulator: a PNG loaded from
  the APK renders at its true 64×64 size (rotating, scaled, alpha intact)
  and a WAV `load_sound`/`play_sound` opens an AAudio output stream with
  zero raylib file warnings.
- **Mobile quality-of-life batch: persistent saves, haptics, hardware back,
  swipe gestures, portrait games.** Five additions that make Tulpar games
  feel native on a phone, all verified on the emulator:
  - `save_data`/`kayit_yaz(name, text)` + `load_data`/`kayit_oku(name)`
    (tame): persistent key-file storage through raylib's file layer — on
    Android it lands in the app's internal storage, on desktop in the CWD,
    so a high score written with the same two lines survives force-stop and
    relaunch on-device (proved: counter 3 → kill → relaunch → still 3).
  - `vibrate`/`titret(ms)` (tame): real device vibration via JNI from the
    NativeActivity (`getSystemService("vibrator")`); the driver always adds
    the `VIBRATE` permission to the manifest; desktop/web are silent no-ops.
    The vibrator service recorded our exact 150 ms step. Gotcha fixed on the
    way: `android/build_tame_android.sh` compiled `tame_impl.c` without
    `-DPLATFORM_ANDROID`, silently no-op'ing every Android-only branch — it
    now uses the same flags as raylib.
  - **Hardware BACK button** (`"BACK"`/`"GERI"` key name in tame; raylib
    keeps it away from the OS): the arcade launcher now treats BACK like
    ESC in-game (returns to the menu) and exits the app from the menu
    (focus verifiably returns to the previous task).
  - `kaydirildi`/`swiped("sol|sag|yukari|asagi|left|right|up|down")`
    (arcade): touch swipe gesture, classified on finger release with a
    dominant-axis threshold — snake-style games play with no on-screen
    controls at all (a right-swipe moved the test box exactly +80 world px).
  - **Portrait games**: `[android] orientation = "portrait"` + a portrait
    `window(480, 800)` render a proper vertical game (verified full-screen
    portrait on-device). `dokunuldu()`-style tap games pair naturally.
- **Yilan (Snake) rewritten on the arcade engine + official "Tulpar Arcade"
  app + APK download on the website.** `examples/arcade_yilan.tpr` is a full
  arcade-engine snake (grid state in plain arrays, zero entities, drawing via
  `ciz_ustune`, 3 levels with speed-up + obstacle walls, haptic feedback on
  eat/crash) steered by **swipe gestures, dpad buttons or arrow keys** — the
  launcher now bundles **ten** games (the menu grid auto-flows to 4 columns).
  `examples/arcade_app/` holds the official app identity (tulpar.toml:
  `dev.tulparlang.arcade` / "Tulpar Arcade" + icon) so
  `tulpar build --apk ../arcade_launcher.tpr tulpar-arcade` reproduces the
  shipping APK. The games page generator (`web_demo/gen_index.py`) gained a
  bilingual "Android — play on your phone" card linking `tulpar-arcade.apk`,
  and the site repo carries the APK + regenerated index (Astro build passes;
  publish pending). Emulator-verified: 10-card menu, snake turns via swipe
  and via the held ▲ dpad button, wall crash → engine game-over, tap
  restarts.
- **Arcade launcher — multiple games in one window / one APK (`launcher()`).**
  `import "arcade"` gains a game-registry + menu layer so a single native
  binary (desktop, web, or Android APK) can hold several games behind a
  touch/keyboard menu. `oyun_ekle(name, setup)` / `add_game(name, setup)`
  registers a game as a 0-arg **setup** function — it does the usual
  `baslangicta`/`her_kare`/`carpisinca`/`bilgi` registrations but *not*
  `sahne`/`oyna`, because the window and loop belong to the launcher.
  `launcher()` (alias `oyun_menusu()`) draws a responsive card grid and runs a
  menu↔game state machine: picking a card (tap, or arrows + ENTER) **fully
  resets the engine's registration state** — sahne callbacks, level registry,
  collision rules, hint/HUD, physics — before calling the chosen game's setup,
  so games never bleed into one another. An on-screen home button (top-right)
  or ESC returns to the menu; `menu_basligi(s)` / `launcher_title(s)` sets the
  title. Example `examples/arcade_launcher.tpr` bundles **all nine shipped
  arcade games** (Topla / Zipla / Nisan / Tugla / Uzay / Labirent / Karsiya /
  Ucus / Goktasi) in one APK — generated by `tools/make_arcade_launcher.py`,
  which namespaces each `examples/arcade_*.tpr` game's top-level functions +
  globals with a per-game prefix and folds its registration block into a
  `<prefix>_setup()` (the source games still run standalone). Verified on the
  Android emulator: the 3×3 menu renders, games launch with clean per-game state
  (e.g. Uzay's custom-tag fleet, Goktasi's countdown), and the home button /
  ESC return to the menu.

## [v3.11.0]

The Tame 2D game library and its two new compile targets — WebAssembly
(`tulpar build --target=web`) and **native Android**
(`tulpar build --target=android`) — land together, turning pure-TulparLang
games into browser and mobile apps. The `import "arcade"` preset engine gains a
level system, entity slot recycling, generation-tagged handles, and on-screen
touch controls, so all 10 bundled games are touch-playable on-device. Alongside
the game work, a batch of language/runtime correctness fixes — struct↔array
round-trips, `call()` N-argument dispatch, method-style calls on any receiver,
closures on the native fast path, and `try`/`catch` handler-stack hygiene. All
backwards-compatible.

### Added
- **Native Android target — `tulpar build --target=android game.tpr out`.**
  Tame/arcade games now compile to real Android apps (NativeActivity +
  raylib GLES2), verified rendering + animating + reading touch on the
  Android Studio emulator. One compiled module is emitted for both ABIs
  (arm64-v8a for devices, x86_64 for the emulator) and linked with the NDK
  into `lib/<abi>/libtulpargame.so`, then an APK is staged with a
  NativeActivity manifest. Mirrors the web target's architecture:
  `android/build_tame_android.sh` cross-compiles the runtime + raylib
  `PLATFORM_ANDROID` + `native_app_glue` + tame bindings into per-ABI
  static archives; `android/package_apk.sh` runs aapt2 + zipalign (16KB
  pages) + apksigner (driving the Windows SDK tools over WSL interop when
  there's no Linux SDK); `android/install_run.sh` does adb install + launch
  + screencap. The whole arcade preset engine (`import "arcade"` — entity
  store, MTV collision, level system, `call()`-based hooks) runs on-device
  unchanged. `async` is unsupported on Android (bionic has no
  makecontext/swapcontext, same as web).
- **Tame touch input — `touch_count()` / `touch_x(i)` / `touch_y(i)` /
  `touched()`** (bilingual: `dokunma_sayisi`/`dokunma_x`/`dokunma_y`/
  `dokunuldu`). Multi-touch finger positions for mobile games; on desktop a
  single touch still maps to the mouse so existing `mouse_*` code keeps
  working. 52 `aot_tm_*` bindings now.
- **Arcade touch controls — on-screen D-pad + action button.** The `import
  "arcade"` preset engine now draws and reads a translucent control overlay
  (bottom-left 4-way D-pad, bottom-right action/jump/fire button), auto-
  enabled on the first touch, so all 10 shipped arcade games are playable
  on touch-only devices — verified on-device. Movement presets read combined
  keyboard-OR-touch input (desktop keyboard play unchanged); game-over
  restart accepts a screen tap; `action_pressed()`/`action_held()` and
  `fire_pressed()`/`ates_basildi()` (+ TR aliases) let games read the action
  button (the two shooter examples adopt it). `touch_controls(true/false)`
  forces the overlay.
- **Tame — 2D game library (`import "tame"`).** Open a window, draw shapes
  and text, and read keyboard/mouse input from pure TulparLang — Pong-class
  games in ~40 lines (`examples/tame_hello.tpr`):
  - **Native layer:** vendored raylib 5.5 (`lib/raylib/`, zlib license, same
    model as SQLite) + 26 `aot_tm_*` builtins in a **separate**
    `libtulpar_tame.a` (`runtime/tame_impl.c` + `tame_bindings.cpp`, two-TU
    split so raylib.h never meets the runtime/windows headers). The AOT
    pipeline links it **only when the program imports "tame"** (or calls a
    `tm_*` builtin), so ordinary binaries gain no GL/window dependency —
    and even a tame binary dlopens X11/GL at runtime rather than linking
    them (GLFW module loader + glad), so it stays portable.
  - **Tulpar layer:** embedded `lib/tame.tpr` wraps the `tm_*` builtins with
    game-friendly names — `window/running/frame_begin/frame_end/clear/rect/
    circle/line/pixel/text/key_down/key_pressed/mouse_x/...` — plus
    `rgb()/rgba()`, the full named raylib palette (`GOLD`, `SKYBLUE`, ...,
    packed `0xRRGGBBAA` ints), and helpers (`rect_overlap`, `point_in_rect`,
    `clamp`). Keys are addressed by name (`"W"`, `"SPACE"`, `"LEFT"`,
    `"F1"`...), no key-constant table to learn.
  - Codegen binds the family table-driven (`k_tame_builtins` in
    `llvm_backend.cpp` — one row per builtin instead of 26 hand-rolled
    dispatch blocks); type inference accepts int **or** float for
    coordinates; all 26 symbols documented in the LSP hover/completion table.
  - Headless/no-DISPLAY runs fail gracefully (bilingual error, clean exit)
    instead of crashing — includes an upstream-matching patch to vendored
    raylib's `InitWindow` (5.5 ignored `InitPlatform()` failure and
    segfaulted in `rlglInit`).
  - **Sprites, fonts, audio (Phases 3-4):** `load_texture/draw_texture/
    draw_texture_ex(scale, rotation)/texture_width/height/unload_texture`,
    `load_font` (TTF) + `text_font`, `measure_text` (centering);
    `load_sound/play_sound/stop_sound/sound_volume` and
    `load_music/play_music/stop_music/music_volume`. Resources are int
    handles in slot registries (the DB-handle pattern); the audio device
    opens automatically on first load, playing music streams are pumped
    automatically inside `frame_end()`, and `close_window()` tears
    everything down in the right order (GL resources before the context,
    sounds before the device).
  - **Managed game loop (Phase 5):** `run(update, draw)` — the Wings
    `listen()` model for games. Takes two function refs, owns the loop,
    frame pacing, **and frame memory**: one `arena_save` before the loop,
    `arena_restore` every frame, `arena_drop` + window close on exit, so
    per-frame strings/objects never accumulate. Same rule as Wings:
    persistent game state lives in globals. Plus `triangle()` (vertex
    winding auto-corrected — raylib's silent CCW-only trap is closed) and
    `screenshot(path)` (PNG, written to the working directory).
  - **Named gamepad input:** `gamepad_available(id)`, `gamepad_name(id)`,
    `gamepad_down(id, btn)`, `gamepad_pressed(id, btn)`,
    `gamepad_axis(id, axis)` — the keyboard's name-based pattern extended
    to controllers: buttons `"A"/"B"/"X"/"Y"` (PS synonyms
    `"CROSS"/"CIRCLE"/...`), dpad `"UP"/"DOWN"/...`, shoulders/triggers
    `"LB"/"RB"/"LT"/"RT"` (`"L1"/"L2"...`), `"START"/"SELECT"/"GUIDE"`,
    stick clicks `"L3"/"R3"`; axes `"LX"/"LY"/"RX"/"RY"` (-1..1) plus
    `"LT"/"RT"` triggers. Without a controller everything degrades
    gracefully (false / `""` / 0.0).
  - Verified live under WSLg: every draw primitive, sprite scaling and
    rotation, and centered text confirmed pixel-level via `tm_screenshot`
    output; 60 FPS pacing (`frame_time` = 0.0167 s); reversed-winding
    triangle rendered; audio device opened; `run()` ran 480 frames stable.
  - `tame_hello.tpr`, `tame_sprite_demo.tpr`, and `tame_run_demo.tpr` are
    compile-only in the test suites (a window would block on machines with
    a display); compiling them end-to-end exercises the whole
    import → codegen → link chain. Test assets live in
    `examples/tame_assets/`.
- **Web target: `tulpar build --target=web` (or `--web`).** Compiles the
  program to a `wasm32-unknown-emscripten` object with the same LLVM
  backend (WebAssembly components now linked on every arch) and links it
  with `em++` against the `wasm/dist` archives produced by
  `wasm/build_tame_web.sh` (async-free runtime + raylib `PLATFORM_WEB` +
  the tame bindings) → `game.html + .js + .wasm`, runnable in a browser.
  `-sASYNCIFY` turns Tulpar's blocking `while (running())` game loop into
  browser-friendly cooperative yielding (raylib's web backend calls
  `emscripten_sleep` in `EndDrawing`). Game assets are embedded with
  `TULPAR_WEB_ASSETS=<dir>` (`--preload-file`). Two ABI notes for
  maintainers: on wasm32 the VMValue C ABI is sret+byval like Win64 — the
  codegen picks it at runtime via `vmvalue_abi_uses_sret()`
  (`llvm_values.cpp`) — and `tulpar_async` is excluded from the web
  runtime (stackful ucontext coroutines don't exist under Emscripten).
- **Arcade: entity slot recycling + generation-tagged handles.** A killed
  entity's slot now returns to a free-list and is reused, so a game that spawns
  bullets forever no longer grows the parallel arrays without bound (a shooter
  creating ~133 entities over 600 frames now plateaus at 22 slots). Reusing a
  raw index would silently alias: code holding a dead entity's id would start
  driving whoever took the slot. So ids handed out are now **handles** —
  `_egen[slot] * 2^20 + slot` — and killing bumps the slot's generation, making
  every existing handle to it stale at once. The whole public id API resolves
  through `_slot_of()`; a stale handle is ignored (setters no-op, getters return
  a neutral value, `alive()/yasiyor()` honestly returns false) instead of
  corrupting the new occupant. `entity_count()/entity_sayisi()` counts *allocated
  slots*, not live entities — new `live_count()/canli_sayisi()` gives the live
  count. Verified on native and web.
- **Arcade: levels (`bolum()/level()`).** Multi-level games are now an engine
  feature instead of something each game re-implements — register a 0-arg setup
  function per level and call `next_level()/bolum_gec()` when its win condition
  is met. New (bilingual, as everything in arcade): `level(n, fn)/bolum`,
  `next_level()/bolum_gec`, `level_no()/bolum_no`, `level_count()/bolum_sayisi`,
  `is_won()/kazandin_mi`, plus `tag_count(tag)/tag_sayisi` (live entities with a
  tag — for "all items collected?" conditions, since `live_count()` also counts
  the player and walls) and `get_vx()/get_vy()` (`vx_of`/`vy_of`).
  - **Semantics:** loading a level runs `clear_entities()` → the global
    `on_start`/`baslangicta` function → that level's function. **Score is kept
    across levels** (it's the player's running total); `R` restarts from level 1
    with score 0. `_ar_state` gained `2 = won`, drawn as a KAZANDIN screen with
    the total score; the HUD gains a `Bolum n/N` indicator (top-right) and a
    ~1.2 s "Bölüm N" banner on each transition. `is_over()/bitti_mi()` now means
    `state != 0` (winning is also an ending).
  - **Backwards compatible:** with no level registered, state 2 never occurs and
    behaviour is exactly as before (`on_start` + OYUN BITTI + R). Level
    registrations survive `clear_entities()/temizle()` — like collision rules,
    they are part of the game's definition, not its entities.
  - **`next_level()` defers the switch** (a `_lvl_pending` flag applied at end of
    frame in `_ar_update`/`step`) rather than swapping levels in place: it is
    typically called from a collision callback, i.e. while `_ar_collisions()` is
    iterating the parallel arrays — calling `clear_entities()` right there would
    mutate the array being walked.
  - Regression suite: `tests/arcade_levels.test.tpr` (headless via `step()/adim()`).
- **Levels in the four bundled games (3 each).** `arcade_zipla` (easy → zigzag
  climb → narrow platforms + a moving lethal obstacle), `arcade_topla` (3 items/1
  enemy → 5/2 → 6/3 fast), `arcade_nisan` (cumulative target score 30 → 60 → 100
  as spawn rate and speed rise), and `tame_snake` (built on raw tame, not arcade,
  so its level flow is hand-rolled: 5 food per level, `MOVE_FRAMES` 8→6→4, plus
  obstacle walls and a win screen in level 3). All four are also built for the
  web in `web_demo/` with a refreshed index page.
- **Four more arcade games, 3 levels each** — each one exercises a different part
  of the engine, so together they double as a feature sweep:
  - `arcade_tugla.tpr` (Breakout): bricks are `item()`s, the ball is a
    `TAG_BULLET` + `MV_VELOCITY` entity, the paddle is `MV_NONE` driven
    horizontally from `on_frame` (`MV_TOPDOWN` would move in 4 directions).
    Levels: flat wall → pyramid → gapped wall + faster ball.
  - `arcade_uzay.tpr` (Space Invaders): a fleet that moves as one body (reverses
    and drops a step at the edge), so the invaders are `MV_NONE` and driven from
    `on_frame`. Levels: 3×5 slow → 4×6 fast → 4×7 + the fleet shoots back (a
    custom tag `6` for enemy bullets, alongside the built-in `TAG_*`).
  - `arcade_labirent.tpr` (maze): layouts are written as ASCII rows and read with
    `ord()` (`#` wall, `P` player, `A` key, `E` patrol), so adding a level means
    writing 15 strings. The player is `MV_TOPDOWN` and rides the engine's MTV wall
    resolution; patrols are `MV_VELOCITY` (which ignores walls), so their corridor
    bounds are computed at setup by scanning the map. Levels: open → symmetric /
    2 patrols → dense / 3 fast patrols.
  - `arcade_karsiya.tpr` (Frogger): lanes of `MV_VELOCITY` traffic wrapped around
    at ±660 — the wrap has to happen *before* the engine's own "64px outside the
    world" auto-kill. Levels: 3 slow lanes → 4 → 5 fast.
- **Arcade: engine-drawn HUD is bilingual (`language()/dil()`).** The strings the
  engine renders itself — the score prefix, the `Level n/N` indicator, the
  `Level N` banner, GAME OVER / YOU WIN and the restart hint — are now drawn in
  Turkish or English by a language code. It defaults to the system UI language
  (`sys_lang()`), and `language("en")` / `dil("tr")` overrides it. Turkish stays
  the default and byte-for-byte the old output, so existing games are unchanged.
  The `controls()/bilgi()` strip is game-supplied, so its language is the calling
  game's choice.
- **English twins of all eight games (`examples/en/`).** Each Turkish game has a
  full English counterpart — English API aliases (`player()`, `level()`,
  `next_level()`, …; every arcade function already has a TR+EN name), English
  comments, English `scene()` title and `controls()` text, and a `language("en")`
  call. `jump`, `collect`, `shooter`, `snake`, `breakout`, `invaders`, `maze`,
  `crossing`. They live in `examples/en/` so the `examples/*.tpr` test runner
  doesn't double-count them; verified by direct compile + browser screenshot.
- **`web_demo` is bilingual.** `web_demo/index.html` (generated by a small script
  from the game sources, so it stays in sync) has a page-level TR/EN switch, a
  Play (TR) / Play (EN) button per game — both language builds are published
  (`zipla.html` + `jump.html`, …) — and a "See code / Kodu gör" panel with TR/EN
  tabs showing the actual embedded source, so a visitor sees each game written in
  both languages.

### Fixed
- **The regex builtin family is now registered in the type checker and LSP.**
  `regex_match` / `regex_search` / `regex_capture` / `regex_replace` worked at
  runtime but were invisible to tooling (no completion/hover, unknown to
  inference) and had zero test coverage. Registered with real signatures and
  documented semantics: `regex_match` is full-string match, `regex_search` is
  substring, an uncompilable pattern is a safe no-op.
- **StringBuilder was dead on arrival — the first `sb_append` segfaulted.**
  Its VMValue argument was declared as the raw by-value aggregate — the SysV
  lowering trap documented at `llvm_make_vmvalue_func_type` — so the callee
  read a corrupted value and dereferenced garbage. The argument now travels
  through the `aot_*_ptr` pointer ABI; all `sb_*` entry points gained
  null-handle guards (`sb_append(0, …)` is a no-op, not a crash); and the
  undocumented creator `StringBuilder(capacity)` is now in the LSP table.
- **`write_file`/`append_file` now actually return the documented bool.** Both
  are typed `-> bool` (type checker + LSP) but returned VOID unconditionally —
  a failed write (bad directory, no permission) was indistinguishable from
  success. True only when the file opened and every byte landed.
- **Wings: request-header lookup is now case-insensitive (RFC 7230).** Headers
  are stored with the client's exact casing and every internal read was a
  hand-rolled 2-case probe (`"Cookie"` then `"cookie"`) — any other casing
  (`COOKIE`, `AUTHORIZATION`, `CONTENT-TYPE`, …) silently missed: cookies
  dropped, `jwt_guard` 401'd valid bearer tokens, gzip never engaged, ETag 304s
  never matched. New `req_header(req, name)` public accessor (+ internal
  `_hdr_ci`) resolves case-insensitively; all five internal sites (cookies,
  jwt, `form_data`, If-None-Match, Accept-Encoding) use it. Stored casing is
  untouched, so existing exact-case reads keep working.
- **Closures created in a native-fast-path function computed garbage.** The
  all-int-signature fast path has no closure support, but its gate accepted
  lambda-initialized decls (`var f = () => i;` fits the i64 decl shape), so
  `func f(): int` bodies with a lambda silently produced wrong numbers. A new
  `expr_has_lambda` check bails return/assignment/decl statements with a
  value-position lambda to the boxed codegen.
- **Top-level lambdas couldn't capture top-level scope-locals.** Capture slots
  were only computed for function/lambda nodes, never the program root, so a
  lambda capturing a top-level for-init var (`for (int k …) { var g = () => k; }`)
  resolved nothing and returned nullptr (globals were always fine). The program
  root now gets the same capture analysis and `main` allocates an env array
  like any capturing function. Loop-created closures share the frame's one
  slot per variable (JS-`var`/Python semantics: they see the final value).
- **`try`/`catch`: leaving a `try` via `return`/`break`/`continue` leaked the
  handler frame.** The setjmp frame was only popped on normal fall-through, so
  a later `throw` could longjmp into a stack frame that no longer existed
  ("longjmp causes uninitialized stack frame" abort). Codegen now tracks open
  try scopes: `return` pops them all (after evaluating its expression),
  `break`/`continue` pop down to the loop's entry depth, and lambdas
  save/reset both try and loop tracking so a nested body can't pop the outer
  function's frames. A bare `throw` followed by more statements also corrupted
  its basic block ("Terminator found in the middle of a basic block") — throw
  now spawns a dead continuation block like `break` does.
- **`parse_iso8601` accepted impossible calendar days.** The day was only
  range-checked 1..31, so `"2026-02-30…"` silently normalised to March 2
  instead of failing — a data-corruption trap for a parser that returns -1 on
  every other malformed input. Days are now validated against the month's real
  length with the full Gregorian leap rule (2024-02-29 and 2000-02-29 parse;
  2025-02-29, 1900-02-29, Apr 31, Feb 30 → -1).
- **`contains()` / `indexOf()` silently failed on arrays.** Both were
  string-only: `contains([1,2,3], 2)` returned false and `indexOf` returned -1
  (with a spurious `expected str` typecheck warning), so the only way to test
  array membership was a manual loop. They now also search arrays by value
  (int/float/bool/string) — `contains([1,2,3], 2)` → true,
  `indexOf(["a","b"], "b")` → 1 — while the string-substring behaviour is
  unchanged. Signatures are now polymorphic in the type checker.
- **Float-to-string lost precision and printed scientific notation
  (`toString(1000000.5)` → `"1e+06"`).** Float formatting used `"%g"` (6
  significant figures), which dropped the fraction of larger values and
  switched to scientific notation. Tulpar floats carry 32-bit precision, so a
  naive bump to higher fixed precision instead exposed float32 rounding noise
  (`3.14` → `"3.14000010490417"`). Float display now prints the **shortest
  decimal that round-trips to the same float32** (new `aot_format_float`, used
  by toString / print / string-concat / JSON alike): `3.14` → `"3.14"`,
  `1000000.5` → `"1000000.5"`, `0.1 + 0.2` → `"0.3"` — matching how Python/Go/Rust
  render floats.
- **String ordering comparisons (`<`, `>`, `<=`, `>=`) were always false.** In
  `vm_binary_op` the string/string type pair fell through to a
  `default: VM_BOOL(0)` for the four ordering operators, so any `"a" < "b"`
  returned false regardless of the operands — and sorting an array of strings
  silently did nothing. (`==`/`!=` already used `strcmp`.) The four operators
  now compare lexicographically via `strcmp`, so `"apple" < "banana"` is true,
  `"abc" <= "abc"` is true, and bubble/insertion sorts over string arrays work.
- **Returning a non-trivially-unboxable struct from a function returned zeros —
  and this also unblocked nested structs.** A struct with float / string /
  nested-struct fields is a boxed `VM_OBJECT`, not a native aggregate, but both
  the call site and the `return` codegen assumed the native res-pointer ABI, so
  `P e = mk(1.5, 2.5)`, `push(a, mk())`, and `arr[i] = mk()` all yielded a zero
  placeholder, and a struct containing a struct (`Body { V pos; }`) didn't work
  at all. The native res-ptr write ABI (and its zero-placeholder fallback) is
  now gated on `struct_is_trivially_unboxable`; every other struct flows as a
  normal boxed `VMValue` return — the function stores its boxed object into the
  result slot and the caller loads it. Nested structs now round-trip
  (`bs[0].pos.x`, `bs[0].mass = 99`), as do float/string/mixed-field structs
  returned from constructors. Trivially-unboxable (int/bool) struct returns keep
  the native fast path unchanged.
- **`call(fn, a, b, …)` forwards N arguments (was a segfault).** Dynamic
  dispatch only knew the 0- and 1-argument shapes; calling a by-name function
  with 2+ args silently dropped the extras, so the boxed callee read an
  unpassed pointer parameter as garbage and crashed. Added `aot_call_dynamic_n`
  / `aot_invoke_boxed_n` (`runtime_bindings.cpp`), which invoke the target
  through its *registered arity* (0–8), padding missing params with VOID and
  ignoring extras so the pointer count always matches the callee (wasm's typed
  `call_indirect` included); a codegen branch (`argument_count >= 3`) stashes
  the args in a stack VMValue array, and `call` is now treated as variadic in
  the type checker. Callbacks no longer have to be 0-arg + global context — a
  `func on_hit(a, b)` handler can be `call()`-ed directly. The existing
  1-argument path (Wings `call(handler, req)`) is unchanged.
- **A native (int/bool) `struct` stored in an array lost field access.**
  `push(arr, s)` then `arr[i].x` reported "Invalid index or target". Trivially
  unboxable structs (all int/bool fields) lower to a native LLVM aggregate;
  once the static type is gone inside a dynamically-typed array, `arr[i].x` is
  a runtime string-key lookup, which the compact int-indexed `ObjStruct` can't
  serve. Float-carrying structs already lived as key-value objects and worked.
  The push/array-literal boxing now converts a native struct to the same
  string-keyed `VM_OBJECT` (`box_native_struct_as_object` in `llvm_backend.cpp`),
  so `struct Ent { int x; int y; }` works in an array with `ents[i].x = …` —
  no more parallel-array workaround. Static `s.x` on a typed local is
  unchanged; no regression across the 76-example suite.
- **Pulling a struct back out of a dynamic array (`Ent e = arr[i]`) read
  zeros.** The extraction path unpacked an int-indexed `ObjStruct`, but after
  the fix above a struct in an array is a string-keyed `VM_OBJECT`, so the
  fields didn't line up. `aot_struct_unpack_named` now resolves fields BY NAME
  from an object (and still copies an `ObjStruct` by position, so a `match`
  subject keeps working); codegen emits the declaration-ordered field-name
  table. `Ent e = ents[i]` now copies the stored fields with value semantics
  (mutating `e` leaves `ents[i]` untouched).
- **Assigning a struct value into an array element (`arr[i] = mk(…)`) stored a
  zero placeholder.** A struct-returning call / struct local on the RHS of an
  element assignment now boxes through the same string-keyed object path as
  `push`, via the shared `codegen_struct_expr_as_object` helper (which also
  backs `push` and array literals — deduplicated). As a side benefit, an array
  literal of struct-returning calls (`[mk(3,4), mk(5,6)]`) now works too (the
  old push/array-literal code only recognised struct *identifiers*).
- **Method-style calls now work on any receiver, not just a bare identifier.**
  `r.area()` worked, but `ents[i].area()` (array element) and `mk(3,5).area()`
  (call result) fell through to a field-closure call and crashed with "Null
  closure". The parser now rewrites `<expr>.<name>(args)` for any head, and
  `resolve_qualified_call` sends a non-identifier receiver straight to method
  dispatch (`name(recv, args)`) — it can never be a module alias. A boxed
  struct receiver (e.g. an array element) is unpacked into the struct
  parameter (`emit_unpack_boxed_struct_into`), so `ents[i].area()`,
  `rs[0].scaled(4)`, and `js[0].wide()` all resolve. Existing identifier /
  json / module-alias call sites are unchanged.
- **Arcade: `arcade_topla` read enemy velocity with a handle.** The bounce logic
  indexed the engine's parallel arrays directly (`_evx[enemy_id]`), but ids are
  handles (`gen * 2^20 + slot`), so it read past the array rather than the
  enemy's velocity. Use the new `vx_of()/vy_of()` getters, which resolve the
  handle to a slot.
- **Arcade: `overlaps()/degiyor()` had the same handle bug.** It took entity ids
  but indexed the parallel arrays with them directly, so the manual overlap test
  read out of bounds for every id (every id is a handle since slot recycling
  landed). It now resolves through `_slot_of()` and returns false for a stale or
  dead handle.
- **`tulpar build --target=web -o game.html` produced `game.html.html`.** The web
  output name is a *base*: the `.html` shell, `.js` and `.wasm` are all appended
  to it, so spelling the extension (the natural thing to write) doubled it and
  also produced `game.html.js`. A trailing `.html` is now stripped once, so
  `-o game` and `-o game.html` both emit `game.html` + `game.js` + `game.wasm`.
- **A local redeclared in a sibling block lost its initializer.** `if`/`else`
  blocks don't open a scope (only functions, lambdas, `for` and main do), so two
  declarations of the same name in sibling branches landed in the *same* scope.
  `add_local` appended a second entry while every lookup scans front-to-back and
  returns the first hit, so the later declaration's initializer store went to an
  alloca nothing ever read: the variable read back as the first branch's value,
  or — when that branch never ran — as uninitialised stack garbage. A
  declaration now reuses the existing slot, so the most recent one wins.
  Surfaced as the arcade platformer reading a garbage speed
  (`float s = _espeed[i]`) and slamming the player into the screen edge.
  Regression suite: `tests/scope_redecl.test.tpr`.
- **Silently wrong code from the native (all-int) fast path.** Functions typed
  `func f(int p): int` are emitted by `codegen_native_func_def`, a hand-rolled
  i64 statement subset that **silently drops** any statement it has no explicit
  case for. Its eligibility gate (`native_codegen_supports_stmt`) was far more
  permissive than the emitter, so ordinary typed code was miscompiled with no
  diagnostic:
  - `if (c) { return 1; } else { return 2; }` never emitted the else branch —
    every input taking the else returned **0**.
  - `if (c) { int a = 1; return a; }` dropped the declaration, leaving `a`
    unregistered and **crashing the compiler** (segfault).
  - a nested `if` inside a `while`/`for` body was dropped (loop bodies only
    emitted assignments and declarations), so accumulator loops computed the
    wrong result.

  The gate now mirrors the emitter exactly; anything richer falls back to the
  boxed VMValue codegen, which handles the full language. Guarded recursion
  (`if (n < 2) { return n; }` in `fib`) still takes the native path.
  Regression suite: `tests/native_fastpath.test.tpr`.
- **A builtin call in a native loop body was silently dropped** (open since
  2026-07-03). Native locals are i64, but `codegen_typed_expr` returns a *boxed*
  VMValue whenever an expression isn't statically int — typically when it
  contains a call. The while/for body emitters stored only the
  `INFERRED_INT`/`BOOL` case and discarded anything else without a diagnostic,
  so the store never happened:
  - `while (i < 3) { toplam = toplam + mod(n, 10); i = i + 1; }` returned **0** —
    the accumulator kept its old value.
  - `while (b != 0) { b = mod(a, b); ... }` (iterative Euclid GCD) never updated
    `b` → **segfault**.
  - `while (n > 0) { n = toInt(n / 10); ... }` never updated `n` → **infinite
    hang**.

  Loop bodies now unbox the payload (field 2), the same treatment the loop
  *condition* already applied to a boxed compare. `for` bodies had the identical
  defect and got the identical fix. This is the long-standing "while loop +
  builtin call miscompiles, use `for`/recursion instead" workaround — it is no
  longer needed.

## [v3.10.0]

A terminal-UI builtin suite for building flicker-free, app-like TUIs in pure
TulparLang, a locale probe, string-escape parsing, and an AOT codegen
correctness fix for comparison-heavy programs on LLVM 22. All
backwards-compatible.

### Added
- **Full-screen TUI builtins.** The flicker-free details (alternate screen,
  synchronized output, cursor home, line-wrap off) are hidden behind clean
  builtins so apps read like Python and never write raw ANSI themselves:
  - **`screen_open(): void`** — enter the alternate screen, hide the cursor,
    disable line-wrap, clear. **`screen_close(): void`** — the inverse (restore
    the normal screen, cursor, and wrap).
  - **`screen_render(frame: str): void`** — draw one frame atomically via
    synchronized output with the cursor homed, so a full repaint never tears or
    scrolls. The app builds the frame as a normal string; unlike `print()` it
    adds no trailing newline.
  - **`style(s: str, spec: str): str`** — wrap `s` in ANSI styles from a
    space-separated spec (`bold dim italic underline invert`; color names
    `red green yellow blue magenta cyan white gray`; `bright-<color>`;
    `on-<color>` backgrounds) instead of hand-written escapes.
  - **`display_width(s: str): int`** — visible terminal column width of `s`,
    ANSI- and UTF-8-aware (color codes count 0, wide/emoji 2, combining marks 0).
    Correct for alignment where byte-based `length()` is not.
  - **`fit_width(s: str, width: int): str`** — fit `s` to exactly `width`
    columns: truncate at a code-point boundary with `…`, or right-pad with
    spaces. For laying out TUI columns.
  - **`term_width(): int` / `term_height(): int`** — controlling-terminal size
    (columns / rows), falling back to 80 / 24 when it can't be queried. For
    responsive layout.
  - **`read_key_timeout(ms: int): str`** — like `read_key()` but waits at most
    `ms` milliseconds, returning `""` on timeout. Turns a blocking key read into
    the event loop a live/animated TUI (spinners, progress, auto-refresh) needs.
- **`sys_lang(): str`** — the OS UI language as a lowercase ISO-639 code
  (`"tr"`, `"en"`, …), or `""` when undeterminable. For app localization.
- **Octal and hex string escapes.** String literals now accept `\NNN` (octal,
  e.g. `\033`) and `\xNN` (hex, e.g. `\x1b`) alongside the existing
  `\n \t \r \e \\ \"`, so ANSI/control sequences can be written directly.
- **`ord(s: str, i: int): int`** — the unsigned byte value (0–255) at byte index
  `i` of `s`, or `-1` if out of range. Strings are UTF-8 byte sequences and
  `length()` / `substring()` are byte-based, so `ord` is the missing primitive
  for hand-rolled UTF-8 handling — e.g. deleting a whole multi-byte code point by
  walking back over continuation bytes (`0x80`–`0xBF`), which a naive
  drop-one-byte would corrupt.

### Fixed
- **Invalid O3 IR for comparison-heavy programs on LLVM 22.** The AOT backend's
  boxed-comparison fast-path merge (int / float / runtime-fallback) built its
  boolean result three different ways, so when a later truthiness check let
  LLVM 22's InstCombine `foldOpIntoPhi` sink the compare through the merge phi,
  it could leave a transient PHI with mismatched operand types
  (`phi i1 [ i1, i1, i64 ]`). It is self-correcting at InstCombine fixpoint, but
  the O1/O2/O3 pipelines run InstCombine with a bounded iteration count, so the
  invalid state could reach the verifier and force the whole module down to
  unoptimized. Two changes fix it: (1) the runtime-fallback path now rebuilds a
  comparison's boolean as the same `zext(i1)` shape as the fast paths, so all
  three phi operands are uniform and the fold is clean; and (2) if the
  in-process verifier still rejects the transient state, `llvm_backend_optimize`
  recovers by round-tripping the module through the IR printer/parser and
  re-verifying, keeping the full optimization level (it only ever emits a module
  that passes the verifier). LLVM 18 was unaffected; the toolchain-specific
  `TULPAR_AOT_DEBUG_O3=1` hook remains for diagnosis.

## [v3.9.0]

New backwards-compatible builtin for interactive terminal UIs.

### Added
- **`read_key(): str`** — blocks for a single keypress with no Enter and no echo,
  and returns its name. Arrow keys resolve to `"up"` / `"down"` / `"left"` /
  `"right"`; other special keys to `"enter"` / `"esc"` / `"space"` / `"tab"` /
  `"backspace"`; printable keys return the character itself. Backed by `_getch`
  on Windows and a `termios` raw-mode read on POSIX (with a graceful plain-read
  fallback when stdin is not a TTY). This is the missing primitive for building
  real app-like TUIs — arrow-key navigation, live selection, in-place repaint —
  instead of line-based `input()` prompts.

## [v3.8.1]

### Fixed
- **Windows console UTF-8 for AOT programs.** AOT-compiled binaries run their
  own entry point and call `aot_runtime_init()`, which set the UTF-8 locale and
  started Winsock but never switched the console code page. On Windows this made
  `print(...)` output with box-drawing, emoji, or Turkish characters render as
  code-page mojibake. `aot_runtime_init()` now calls `SetConsoleOutputCP(CP_UTF8)`
  / `SetConsoleCP(CP_UTF8)` (the compiler driver already did this for itself), so
  every compiled program renders UTF-8 correctly with no per-program workaround.

## [v3.8.0]

New backwards-compatible builtin plus a native-codegen correctness fix.

### Added
- **`sys_run(cmd: str): int`** — runs a shell command with inherited stdio (its
  output streams live) and returns the process exit code (`0` = success). Lets
  Tulpar programs drive external tools such as `winget`, `git`, or any CLI.
  Wired through the standard builtin path: runtime binding (`aot_sys_run`),
  typeinfer signature, LSP doc, and LLVM codegen dispatch.
- `TULPAR_AOT_EMIT_LL_PRE=1` — dump the pre-optimization LLVM IR (debug aid,
  companion to `TULPAR_AOT_EMIT_LL`).

### Fixed
- Native (all-int) fast-path codegen mis-compiled two cases exposed by
  value-returning functions that read globals:
  - `while` / `for` conditions that come back boxed (e.g. `i < length(arr)`)
    were emitted as `br i1 true` — an infinite loop. The boxed payload is now
    tested `!= 0`, matching the `if`-condition path.
  - reading an array/object subscript (`arr[i]`) inside a native function
    returned garbage; such statements now fall back to the boxed VMValue
    codegen, mirroring the existing subscript-assignment bail.

## [v3.7.0]

Backwards-compatible **developer-experience round** (design doc:
[WINGS_DX.md](WINGS_DX.md), cheatsheet: [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md)).
No breaking changes — every rename keeps the old name as a delegating wrapper.

### Added — ORM v2 (lib/orm.tpr, pure Tulpar)
- **Model handles + UFCS:** `database(path)` + `model(table, schema)` return a
  plain-json handle used method-style — `Note.create({...})`, `Note.find(id)`,
  `Note.all()`, `Note.where("done = ?", [0])`, `Note.first(cond, params)`,
  `Note.count()`, `Note.update(id, {...})`, `Note.save(obj)` (upsert),
  `Note.remove(id)`, `Note.raw(where_sql)` (escape hatch).
- **Schema shorthands** shared with `body_schema` vocabulary: `"pk"`, `"str"`,
  `"int"`, `"float"`, `"bool"` (+ `!` = NOT NULL); anything unrecognized passes
  through as raw SQL (`"TEXT UNIQUE"`). `"bool"` columns are cast to
  `true`/`false` on read — no more hand-written `row_to_note()` converters.
- **Parameterized SQL everywhere:** every generated statement binds values via
  the 3-arg `db_query`/`db_execute` (`?` placeholders); nothing is ever
  string-interpolated into SQL. Multiple databases work — each handle carries
  its own connection.
- New regression suite `tests/orm.test.tpr` (11 cases).

### Fixed — ORM
- **Zero/empty values are no longer silently dropped.** v1 selected columns by
  value truthiness, so `orm_update(id, {"done": 0})` (or `""`/`false`) wrote
  nothing. Column selection now iterates `keys(attrs)` — explicit `0`/`""`/
  `false` persist. Applies to the v1 wrappers too (intentional behavior fix).

### Added — wings DX layer (lib/wings.tpr, pure Tulpar)
- **`resource(path, Model[, opts])`** — automatic REST CRUD from an ORM model
  handle: `GET/POST path`, `GET/PUT/DELETE path/:id`, request schema derived
  from the model (`"str!"` → required, others optional → auto-422), rows
  bool-cast, `/docs` + OpenAPI fed automatically.
  `opts: {"only": [...]}` / `{"except": [...]}`
  (actions: index/show/create/update/destroy). A persistent CRUD API is now
  7 lines (`examples/wings_orm_resource.tpr`).
- **`serve(port, workers)`** — one front door for the server:
  `serve()` → 8484, `serve(8080)` → explicit port, `serve(8080, 4)` →
  `listen_pool`. The `listen*` family remains as advanced modes.
- **Short names (old names still work):** `cookies(req)` (`wings_cookies`),
  `ws_upgrade/ws_send/ws_close/ws_pong` (`wings_ws_*`), `sse_headers/sse_event`
  (`wings_sse_*`), `metrics_prom()` (`wings_metrics_prom`), `gzip(min?)`
  (`enable_gzip`), `delete(path, h)` (`del`), `accepts(schema)` (`body_schema`),
  `returns(schema)` (`response_model`).
- New regression suite `tests/wings_dx.test.tpr` (10 cases: route wiring,
  only/except filters, schema derivation, CRUD envelope statuses, aliases).

### Changed — tooling & docs
- LSP builtin table (`src/lsp/builtins.cpp`): added the wings DX layer, the
  previously unregistered `patch/head/options`/`body_schema`/`response_model`
  and request readers (`param`/`query`/`form`), and the **entire ORM v2
  surface** (ORM had zero LSP presence before) — everything now shows up in
  completion/hover.
- `examples/wings_notes_db.tpr` modernized: bound-parameter SQL (its comments
  wrongly claimed parameterized queries don't exist — stale since v3.3.0),
  function-ref handlers, `accepts()`/`delete()`; repositioned as the
  "hand-rolled SQL" teaching counterpart to `wings_orm_resource.tpr`.
- `examples/api_wings_crud.tpr` modernized: `req` parameter + `req.json`
  instead of the `_request` global, function-ref handlers, `accepts()` schema,
  `serve(3000)`.
- README: modern 8-line hello (function refs + `serve`), new 7-line
  persistent-CRUD showcase, `serve()` documented as the front door.
- New docs: `WINGS_DX.md` (design study), `WINGS_CHEATSHEET.md` (one-page
  UFCS-first API map).

## [v3.6.0]

Backwards-compatible feature round on top of v3.5.0: **wings completeness**
(the framework-parity follow-up from HANDOFF §2). No breaking changes.

### Added — wings (lib/wings.tpr, pure Tulpar)
- **Cookie SET side:** `set_cookie(res, name, val, opts)` /
  `delete_cookie(res, name)` build the `Set-Cookie` response header
  (opts: `path`, `domain`, `max_age`, `same_site`, `secure`, `http_only` —
  HttpOnly + SameSite=Lax + Path=/ by default). One cookie per response
  (response headers are a dict; last call wins).
- **Signed cookies:** `set_signed_cookie(res, name, val, secret, opts)` /
  `get_signed_cookie(req, name, secret)` — value carries an
  `hmac_sha256(secret, name + "." + value)` MAC (name-bound, so cookies
  can't be swapped); tampered/absent → `""`. MAC comparison is
  double-HMAC'd so string-compare timing reveals nothing.
- **Server-side sessions:** `session_start(req, secret)` (existing valid
  signed `tsid` cookie or fresh `secure_token(32)` id),
  `session_attach(res, sid, secret)`, `session_set/get(sid, key[, val])`,
  `session_destroy(sid)`. In-memory (`_wings_sessions` global, auto-persist);
  process-lifetime only.
- **Configurable CORS:** `cors(origin, {credentials, methods, headers,
  expose, max_age})` replaces the static wildcard defaults; sets
  `Vary: Origin` for specific origins and supports
  `Access-Control-Allow-Credentials: true` (the wildcard can't — the exact
  gap the registry frontend hit). Startup-only, covers the automatic
  OPTIONS/preflight 204.
- **Rate limiting:** `rate_limit(max, window_s)` — fixed-window,
  `use()`-based middleware keyed on `X-Forwarded-For`/`X-Real-IP` (first
  hop) with a global-bucket fallback; over-limit → 429 + `Retry-After`.
  Table resets each window, so memory stays bounded.
- **JWT guard:** `jwt_guard(secret)` — bearer-token middleware
  wire-compatible with the `wings_jwt` package (HS256, base64url segments,
  hex-MAC signature). Missing/bad/expired → 401; valid → claims injected as
  `req["jwt"]`. `jwt_public(path)` exempts paths (exact or trailing-`*`
  prefix); `/healthz`, `/metrics`, `/docs`, `/openapi.json` exempt by
  default. Also flags bearer auth in the OpenAPI doc.
- **HTML + templates:** `html(body)` (text/html response) and
  `render(tpl, vars)` — `{{key}}` substitution over a vars dict.
- **Typed path params:** `param(req, name, fb)` / `param_int` /
  `param_bool` — mirrors of the query helpers for `:name` route params.
- **ETag / conditional requests:** `cached_get` routes now serve a strong
  body-derived `ETag` and answer a matching `If-None-Match` with an empty
  `304` instead of the cached body.
- **Response compression:** `enable_gzip(min_bytes)` — transparent gzip for
  responses ≥ threshold when the client sends `Accept-Encoding: gzip`
  (adds `Content-Encoding: gzip` + `Vary: Accept-Encoding`; skips
  `cached_get` routes, whose pinned bytes are shared by all clients; skips
  when compression doesn't shrink the body).
- **OpenAPI completeness:** `response_model` schemas now document the 200
  response body in `/openapi.json`; `jwt_guard()` (or
  `docs_security("bearer")`) advertises a `bearerAuth` security scheme so
  Swagger UI shows Authorize.

### Added — runtime / language
- **`gzip_compress(s: str) -> str`** — gzip (RFC 1952) stream of the input
  bytes via a new **in-tree DEFLATE** (`runtime/tulpar_gzip.cpp`: fixed
  Huffman + greedy LZ77 over a 32K window + CRC-32). No zlib dependency —
  AOT user binaries stay self-contained. Binary-safe both directions
  (length-tracked strings). Wired through runtime, AOT codegen, typeinfer,
  LSP. Verified byte-exact against Python `gzip.decompress` and
  `curl --compressed` (CRC checked) on text, full-byte-range binary and
  random payloads; 16.4 KB HTML compresses to 274 B (1.7%).

### Fixed
- **`response_model` no longer drops `_headers`** — the output filter now
  preserves the `_headers` envelope key, so `set_cookie(...)` /
  `redirect(...)` headers survive response-model filtering.

### Tests / examples
- `tests/wings_features.test.tpr` — 13/13 (cookie builder, signed-cookie
  round-trip + tamper, sessions, CORS, rate-limit buckets, path params,
  JWT verify + middleware, html/render, `_headers` preservation, OpenAPI
  extensions, gzip).
- `examples/wings_features_api.tpr` — live showcase of the whole round
  (compile-only in CI; every endpoint verified with curl: sessions
  visits 1→2, tamper → 401, 100×200 → 429 + per-IP isolation, ETag → 304,
  gzip byte-exact).

## [v3.5.0]

Backwards-compatible feature on top of v3.4.0. No breaking changes.

### Added
- **`hmac_sha256(key: str, msg: str) -> str`** — keyed message
  authentication (HMAC-SHA256, RFC 2104) as a lowercase 64-char hex digest,
  built on the in-tree SHA-256 (no OpenSSL). The signing/verification
  building block for signed cookies, webhook signatures and JWT-style
  tokens — verify by recomputing the MAC and comparing. Wired through
  runtime, AOT codegen, typeinfer and the LSP builtin table. Validated
  against the RFC 4231 test vectors.
- **First registry package: `wings_jwt`** (`packages/wings_jwt/`) — HS256
  signed session tokens for wings apps (`sign` / `sign_ttl` / `verify` /
  `decode` / `from_header`), zero dependencies, 8/8 tests. Built on
  `hmac_sha256` + `base64_encode`. The first real, installable content for
  the `api.pkg.tulparlang.dev` registry beyond smoke packages.

### Fixed
- **`db_execute` typecheck return type** restored to `bool` (was wrongly
  changed to `int` in v3.3.0 when the parameterized-SQL overload was added).
  The runtime always returned `VM_BOOL(rc == SQLITE_OK)`, so the catalog lied
  — under `strict = true` this rejected the idiomatic `bool ok = db_execute(…)`
  with "expected bool, got int", which silently broke strict-mode builds of
  the `tulpar-be` registry. `int ok = db_execute(…)` still works (AOT bool→int
  decl coercion is unaffected).

### Docs
- New **Crypto & security** section in the built-ins reference (EN + TR)
  documenting `sha256` / `hmac_sha256` / `password_hash` / `password_verify`
  / `secure_token` / base64 with guidance on which to use where.

## [v3.4.0]

Backwards-compatible feature on top of v3.3.0. No breaking changes.

### Added
- **`secure_token(n: int) -> str`** — cryptographically secure random base62
  string of length `n`, backed by `std::random_device` (OS CSPRNG / `/dev/urandom`),
  unbiased via rejection sampling. Use this — **not** `randint`/`random` (the
  non-crypto `rand()` seeded with `time()`) — for session tokens, salts and any
  other security-sensitive randomness. Wired through runtime, AOT codegen,
  typeinfer and the LSP builtin table.

## [Unreleased] — v3.3.0 (candidate)

Backwards-compatible features on top of v3.2.1. No breaking changes.

### Added (v3.3.0)
- **Parameterized SQL queries.** `db_query(db, sql, params)` and
  `db_execute(db, sql, params)` now accept an optional array of bound values for
  `?` placeholders (`sqlite3_bind_*`), so user input never touches the SQL text —
  injection-safe without manual quote-escaping. The 2-arg forms are unchanged;
  the cached prepared-statement path is reused (constant SQL = one cache entry).
  `db_execute` returns a success bool.
- **Password hashing KDF.** New `password_hash(pw)` and
  `password_verify(pw, stored)` builtins implementing PBKDF2-HMAC-SHA256
  (100k iterations, random 16-byte salt, self-describing
  `pbkdf2_sha256$iters$salt$dk` string, constant-time verify). Built on the
  in-tree SHA-256 — no OpenSSL dependency. Use these for auth instead of bare
  `sha256`.
- **Wings `patch` / `head` / `options` route helpers.** First-class verbs
  alongside `get`/`post`/`put`/`del` (the router matches the method string
  generically, so PATCH/HEAD/OPTIONS requests dispatch correctly).

### Added (earlier, v3.1.0–v3.2.1)
- **SQLite parallel reads under WAL.** A DB handle is now a `DbConn` descriptor
  index (user API unchanged); file-backed databases open a per-thread `sqlite3`
  connection lazily so `listen_pool` workers read in parallel instead of
  serializing on one connection's mutex. Measured read-by-PK throughput
  23.8k → 35.1k RPS (~+47%); RSS stays flat (~9.9 MB). `:memory:`/temp DBs keep
  a single shared connection.
- **`db_open` server-friendly defaults.** `busy_timeout=5000` + WAL +
  `synchronous=NORMAL` on file-backed DBs (write throughput ~2.3× in the stress
  harness). Opt out with `TULPAR_DB_NO_WAL=1`.
- **Wings ergonomics (FastAPI-level).** Function-reference handlers
  (`get("/users", list_users)`), `req` parameter (`req.params.id`, `req.json`),
  response helpers (`ok`/`created`/`not_found`/…), automatic JSON body parse,
  invisible auto-persist (writes to globals survive the per-request arena),
  schema validation (`body_schema({...})` → automatic 422), automatic `/docs`
  (Swagger UI + `/openapi.json`), and a branded default port 8484.
- **Language: `async` `gather(...)`** (concurrent awaits) and **`match`
  destructuring** — arrays (`[head, ..tail]`), json/object fields
  (`{role: "admin", name}`), typed-struct variants (`Circle{r}`), and nested
  patterns.
- Benchmark + stress harness: Wings vs FastAPI comparison and a multi-threaded
  HTTP + SQLite load generator (`benchmarks/`).

### Fixed
- **Thread-safety audit of the AOT runtime** (affects `listen_pool` /
  `listen_async`):
  - `toString()` used a shared, non-thread-local scratch buffer; concurrent
    callers could clobber it, yielding an empty result → malformed SQL → ~1.1%
    spurious 404s. Now `thread_local`.
  - The exception-handler context (`eh_main`/`eh_cur`) was global; a pooled
    handler using `try`/`throw` could `longjmp` across worker threads
    (crash/UB). Now `thread_local`.
  - The dynamic-call cache published its slot key with a plain store — correct
    on x86 TSO but not on ARM/aarch64 (an Apple Silicon / aarch64 target could
    read a stale function pointer). Now an `std::atomic` release/acquire publish.
- **Per-request memory leak on the Wings hot path** — per-request malloc region
  + runtime write-barrier keep RSS flat (ASan clean).
- `db_last_insert_id` / `db_error` codegen signatures (LLVM module-verification
  warning on every DB program).
- Default arguments (missing trailing args pad to boxed `0`), `\e`/`\0` string
  escapes, boxed unary-minus, and a clean Ctrl+C exit (no misleading
  "compile/link failed").

### Repo hygiene
- Stop tracking accidentally-committed local dirs (`github/` dot-less duplicate
  of `.github/`, `claude/` Claude Code lock, `.opencode/` tool config).

## [3.0.0] — 2026-06-15

### Changed (breaking)
- **AOT-only architecture.** The bytecode VM interpreter, the AST→bytecode
  compiler, and the REPL were removed — Tulpar now follows the C/Rust/Go model
  with a single AOT/LLVM execution path. `--vm`/`--run` are ignored with a
  warning; `--repl`/`-i` print a removal notice. An AOT failure is now a hard
  error (no VM fallback).

### Added
- **`async`/`await` v1** — stackful coroutines + event loop (POSIX `ucontext` /
  Windows fibers), non-blocking `sleep_async`, coroutine-aware exception context.
- **`match` v1.1** — literal / `_` / `|`-alternatives / inclusive ranges.
- Cross-platform async build (macOS `ucontext`, Windows fibers, MinGW).

## [2.2.0] — 2026-06-01

### Changed
- CI switched to **stable-only versioning** — releases are cut only on `v*` tag
  pushes (no more rolling per-commit releases).

### Fixed
- VM typed-struct params pass by value (mirrors AOT semantics); bool→int
  coercion at typed local var declarations; wired 8 utility builtins (arena,
  cpu/time, input, `string_pin`).

[Unreleased]: https://github.com/hamer1818/TulparLang/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/hamer1818/TulparLang/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/hamer1818/TulparLang/releases/tag/v2.2.0

---
tags: [moc, traps, debugging]
---

# Tuzaklar — bir sorun çıktığında İLK bakılacak yer

> Bu not, bu projede **tekrar tekrar** yaşanmış hata sınıflarının indeksi.
> Yeni bir belirti gördüğünde önce burayı tara: çoğu "yeni" hata buradaki
> kalıplardan birinin başka bir yüzü.

## 1. "Testim yeşil ama hiçbir şey ölçmüyor"
En sık ve en pahalı sınıf. **Her düzeltme, hatayı bilerek enjekte edip doğru
testin kızardığı görülerek doğrulanır** ([[Testing]]).

### 1a. Karar sınandı, ÇAĞRI sınanmadı — BEŞ kez yaşandı
Yardımcı fonksiyonu doğrudan çağıran bir test, o yardımcının **çağrı yerinden
silinmesini** göremiyor. Çağrı çizim/döngü göbeğindeyse pencere olmadan
sürülemiyor ve bozma sessizce kaçıyor.

Yaşananlar: menü dağıtımı (`_s3_menu_act3`), pencere tazeleme
(`_ed_sync_window3`), bırakma kararı (`_dk_drop_result3`), kaydırma adımı
(`_ed_iscroll_step3`), yuva içi sınır (`_dk_div_pick3`/`_dk_div_apply3`).
**Çözüm hep aynı:** kararı ayrı bir SAF fonksiyona çıkar ve testi ONU sürsün.

**Saf fonksiyon YETMEDİĞİNDE:** çağrının kendisi fare/pencere istiyorsa
**KAYNAĞI OKUYAN** bir test yaz — `read_file("lib/scene3d.tpr")` + `split`
ile hem çağrının varlığını hem SIRASINI sına (`t_div_is_wired_into_the_frame`).
Sınırı açık ve dürüstçe yazılmalı: "bir yerden çağrılıyor" der, "her karede
çağrılıyor" demez. Aynı desen `t_no_duplicate_function_names`'te de var.

### 1b. Beklenti sınanan formülün kendisinden türetiliyor
Nişan yönü hatası (2026-08-14) böyle kaçtı: test, beklediği yönü sınadığı
formülden üretiyordu → 180°'lik hatayı onayladı. **Beklenti bağımsız bir
kaynaktan gelmeli.**

### 1c. Test sayıları TAM BÖLÜNÜYOR
Dock testlerinde üç bozma kaçtı çünkü 1440/2 ve 820/2 tam bölünüyordu — "son
panel artanı alsın" kuralı hiç sınanmıyordu. **Tek sayılı ölçü kullan**
(1000 genişlik, 821 yükseklik).

### 1d. Senaryo hiç kurulmuyor
- Yinele yığını testi hiç geri alma yapmıyordu → yığın zaten boştu.
- Tek-atım bölge testi tek gövdeyle senaryoyu üretemiyordu (iki gerekti).
- Üç ışıkta ortadakini silmek, kaydırma ile takasla AYNI sonucu veriyor
  (dört ışık gerekiyordu).

### 1e. Sıra bağımlılığı
Global durum testler arasında taşınıyor. `scene3d_reset()` bir şeyi
sıfırlamıyorsa sonraki test onu miras alır. İlerleme bayrakları (`_lvl_done3`)
ve geri-al yığınları bu yüzden reset'e eklendi.

### 1f. Eşitlik "sığıyor" sayılıyor
`içerik <= yükseklik` iddiası, payı sıfırlayan bozmadan KAÇIYOR. Görünür bir
pay ayrı bir iddia olmalı.

### 1g. Kaçan bozma = ÖLÜ KOD olabilir
Bozma kaçtığında ilk varsayım "test eksik" olmasın: koruma **gereksiz** de
olabilir. Yuva içi sınırda iki koruma böyle silindi — fareyi piksel olarak
kelepçeleyen satır (pay tabanı zaten sağlıyordu) ve "son panel artanı alır"
özel durumu (yığmalı toplamda son panelin bitişi zaten tam T). İki yerde
tutulan koruma, hangisinin yük taşıdığını sınanamaz yapıyor.

### 1i. Soyutlama DEJENERE, bozma bu yüzden kaçıyor
Duraklat menüsünün tür sabitleri 0,1,2,3'tü ve liste sırası da 0,1,2,3 —
yani "türe göre dağıt" eşlemesi birim fonksiyondu. Dağıtıcıyı `a - 1`'e
çeviren bozma **hiçbir davranışı bozmadan** çalıştı ve testten kaçtı.
Soyutlama gerçekten var olsun diye sabitler kaydırıldı (`_PB_* = 10..13`).
**Kural:** bir dolaylılık katmanı ekliyorsan, onu atlayan kestirmenin
GERÇEKTEN farklı sonuç vermesini sağla — yoksa katman yalnız kâğıt üstünde.

### 1j. Disk artığı testler arasında taşınıyor
"Diske yazılmamış olmalı" testi, önceki bir **bozma denemesinin** yazdığı
dosyayı okuyup yanlış yere kızardı. Kayıt/dosya sınayan testler kendi
anahtarını ÖNCE temizlemeli (`save_data(key, "")`); enjeksiyon turları da
diske yazabilir.

### 1h-4. Test ÖLÇEĞİ sızıntıyı görünmez yapıyor
Bölüm ızgarasının "ekranda kalıyor" testi 10 bölümle koşuyordu; dikey
ortalamayı sabit bir tepeye çeviren bozma o boyutta hâlâ sığıyor ve
**kaçıyordu**. Taşma ancak satır sayısı büyüyünce görülüyor.
**Kural:** yerleşim testlerini tek boyutta değil, birkaç ölçekte koştur
(3 / 10 / 25 / 60) — sınır davranışı ancak ucunda görünür. Aynı testte
"kaç bölüm" gibi bir eksen varsa, o eksenin uç değerlerini de ölç.

### 1h-3b. Kurulum, ikinci koşulu zaten sağlamıyor
"Bölümler düğmesi kayıt kapalıyken çıkmamalı" testi kayıt KAPALI ama
**tek bölümlü** bir sahnede koşuyordu; düğme zaten çıkmazdı, yani kayıt
koşulunu silen bozma kaçtı. İki koşullu bir kuralı sınarken, sınanmayan
koşulun **sağlandığından** emin ol — yoksa test öteki koşulu ölçer.

### 1h-2. Testin kendi SIRASI sızıntıyı görünmez yapıyor
Kutu seçimi testinde kameranın arkasındaki cisim, listenin **sonunda**
duruyordu. Yansıtma başarısız olunca sonuç değişkenleri bir önceki cismin
değerlerinde kalıyor — sondaki cisim ekran dışındaki komşusunun değerlerini
miras alıyor ve kutuya girmiyordu. Bayrağı yok sayan bozma bu yüzden
**kaçtı**. Sızıntıyı görünür kılan şey sıralama: arkadaki cismi ekranın
ortasındaki cismin HEMEN ARDINA koymak.
**Kural:** "eski değeri okuma" hatalarını sınarken, okunacak eski değerin
testi kızartacak bir değer olduğundan emin ol.

### 1h-3. Döngünün SONUNDA ölçmek geri koymayı taklit ediyor
Önizlemenin modeli kirletmediğini sınayan test, sol/sağ/alt üçlüsünü
döngüde çağırıp sonunda ölçüyordu. Son çağrı panelin **kendi** yuvasına
olduğu için, geri koymayı silen bozma "geri koymuş" gibi göründü.
**Kural:** tek bir çağrıdan **hemen sonra** ölç ve hedefi asla nesnenin
bulunduğu yer seçme.

### 1h. Testin KENDİ kurulumu sınananı ortadan kaldırıyor
Yan yuvadaki panellerin alt yuvanın üstünde bitmesi hiç sınanmıyordu: paylaşım
testlerinin hepsi konsolu yan yuvaya taşıyor, yani **alt yuva boş kalıyor** ve
`- _dk_bh3()` çıkarmasını silmek hiçbir testi kırmıyordu. Aynı sınıf:
`goto_level3d` testi `_cur_lvl3 == 0` iken koşuyordu.
**Sor:** bu testin kurulumu, sınadığım şeyin ETKİSİNİ sıfırlıyor mu?

## 2. Enjeksiyon harness'ı yalan söylüyor
`lib/*.tpr` içinde **süslü parantez dengesi bozulursa** `cmake --build` yine
BAŞARILI döner (`.tpr` yalnız gömülü bir dize). Hata test programı
`import` edince çıkıyor → **test hiç koşmuyor, çıktı boş**. Boş çıktıyı
"kırılan yok" diye okuyan harness bunu **"KAÇTI"** diye raporluyor.

**Kural:** `Tests:` satırının VARLIĞINI denetle; yoksa "KOŞMADI" yaz.
Enjeksiyonları ifade düzeyinde yap (`if (cond)` → `if (true)`), satır
silerek blok yapısını bozma.

**İkinci yalan biçimi: harness YANLIŞ ADI arıyor.** `FAIL` satırı testin
ETİKETİNİ yazıyor (`"yineleme eklenen bolumu geri getiriyor"`), fonksiyon
adını (`t_redo_restores_a_structural_add`) değil. Beklenen testi fonksiyon
adıyla arayan harness altı bozmanın beşini **"KAÇTI"** diye raporladı —
oysa hepsi yakalanmıştı. Kural: eşleşmeyi `test("<etiket>", "<ad>")`
kayıtlarından KUR, iki adı elle eşleştirme. Beklentiyle çelişen bir "kaçtı"
raporunda önce harness'ın kendisinden şüphelen: kırmızı listesine BAK.

**Üçüncü yalan biçimi: iki `tulpar` aynı geçici yolu paylaşıyordu.**
`aot_compile_and_run_silent` derlediği ikiliyi **sabit** `/tmp/.tulpar_run`
yoluna yazıp çalıştırıp siliyordu. İki `tulpar` aynı anda koşunca biri
ötekinin ikilisini eziyor ve siliyor. Belirtinin iki yüzü var ve ikincisi
çok daha kötü:

- `/tmp/.tulpar_run: Böyle bir dosya yok` → paket boş çıktıyla **FAIL**
  görünüyor (tek başına koşturunca yeşil).
- **Sessizce YANLIŞ program koşuyor:** enjeksiyonla ölçüldü (2026-09-01) —
  iki farklı kaynak, iki süreç, **ikisi de aynı çıktıyı** bastı. Yani paket
  koşucusu bir paketin yerine BAŞKA bir paketin sonucunu raporlayabilir.

**Düzeltildi:** yol artık sürece özgü (`/tmp/.tulpar_run.<pid>`). Ders:
"testte regresyon" görünen bir FAIL'i düzeltmeye başlamadan önce paketi
**tek başına** koştur; eşzamanlı bir şey varsa şüpheyi önce ORAYA yönelt.

**Dördüncü yalan biçimi: `tulpar build` BAYAT ikili veriyordu.** Önbellek
yalnız ana kaynağı ve sürücüyü karşılaştırıyordu; `import` edilen YEREL bir
modülü (`import "lib/scene3d"`, `import "utils"`, `tulpar_modules/...`)
düzeltip yeniden derlemek `[AOT] Cache hit` alıp eski ikiliyi bırakıyordu.
Belirti son derece yanıltıcı: **düzeltmen "işe yaramamış" görünüyor.**
Ölçüldü (2026-09-01): kod üretimi iğnelemesinde üç ayrı bozma da aynı bayat
ikiliyi koşturdu; ikisi hiç ölçülmediği hâlde "yakalandı" gibi göründü ve
düzeltilmiş hâl bile kırmızı çıktı.

**Düzeltildi:** önbellek artık import edilen yerel dosyaların (özyinelemeli)
mtime'ını da okuyor (`newest_local_import_mtime`, `src/main.cpp`). Gömülü
stdlib adları diskte çözülmez — onları sürücünün mtime'ı kapsıyor.
**Kural:** iğneleme koşarken çıktı ikilisini SİL (`rm -f`), önbelleğe güvenme;
ve iğnelemenin gerçekten derlendiğini bir kez gözle doğrula.

**Beşinci yalan biçimi: gürültü tanıyı DIŞARI itiyor.** `build.sh suites`
başarısız pakette `grep -E 'FAIL|hata|error' | head -8` basıyordu. Bir
kütüphane stderr'e gürültü döktüğünde (ALSA'nın "ses aygıtı yok" satırları)
o gürültü `error` ile eşleşip **gerçek FAIL satırlarını dışarı itiyordu** —
CI kırmızı dönüyor ama HANGİ testin düştüğü çıktıda hiç görünmüyor. Ölçüldü
(2026-09-01): bir sürüm turu tam bu yüzden boşa gitti. `head`'in erken çıkması
ayrıca yukarıdaki grep'e SIGPIPE attırıp çıktıya "write error: Broken pipe"
satırları da ekliyordu. **Düzeltildi:** önce `^\s*FAIL` satırları (awk ile,
SIGPIPE'sız), genel gürültü yalnız FAIL satırı HİÇ yoksa yedek olarak.

## 3. Penceresiz ölçüm boşa çıkıyor
`text_width()` / `font_width()` **pencere yokken 0 döner** → her yerleşim
karşılaştırması `0 <= sınır` olur ve **her metin "sığıyor" görünür**.

**Savunma:** karakter bütçesi testi. `_t_fits3(label, boxw)` bütçeyi KUTU
GENİŞLİĞİNDEN türetiyor (`boxw * 2 / 23` ≈ 11.5 px/karakter, ölçülmüş).
Yeni bir panel/pencere eklerken metinlerini bu teste EKLE — eklendiği anda
her seferinde eski taşmalar çıktı ("tek atim: yok" 13/11, kural özeti
462/280, "birinci sahis" 13/11).

Gerçek ölçüm gerekiyorsa: web hedefi + başsız Chrome (CDP). Bu makinede
Chrome/Chromium ve Xvfb **YOK**.

## 3b. "Ses gelmiyor" TEK bir arıza değil
Aynı sessizlik en az **beş** ayrı sebepten geliyor ve hepsi kulakta aynı:

| sebep | gösterge |
|---|---|
| ses aygıtı hiç açılmadı | handle `-1` (stderr'de `[tame] Ses aygiti acilamadi`) |
| dosya bulunamadı | handle `-1`, aygıt açık |
| olay hiç olmadı (bölgeye girilmedi) | giriş sayacı 0 |
| olay oldu ama ses ÇAĞRILMADI | giriş artıyor, `ses_calma_sayisi3d()` artmıyor |
| ses çağrıldı ama SEVİYE 0'dı | çalma artıyor, `ses_son_seviye3d()` = 0 |

Bunları ayırmadan hata aramak, penceresiz sondaların hepsinin yeşil olduğu
(ve gerçekten de doğru olduğu) bir turda saatler yakıyor — **yaşandı**
(2026-09-01, arena bölge sesleri). Motor bu yüzden çalarken UYGULANAN
değerleri kaydediyor: `ses_son_seviye3d()`, `ses_son_kaydirma3d()`,
`ses_calma_sayisi3d()`.

**Araç:** `examples/scene3d_ses_testi.tpr` — beş istasyon, beşi FARKLI ses,
her katman ayrı: elle yükleme+konumsal · varlık kaydı+konumsal · bölgenin
kendi sesi (kutu) · bölgenin kendi sesi (küre) · konumSUZ düz çalma.
1..5 tuşları aynı sesleri bölgeye hiç girmeden çalıyor. Belirti → katman
eşlemesi doğrudan okunuyor.

### 3b-1. Altıncı sebep: ses ÇALDI ama duyulacak kadar sürmedi — **DOĞRULANDI**
Ölçüldü (2026-09-01): `ates.wav` **0.14 s**, `altin.wav` 0.24 s. Arena'nın
zehir havuzu sesini yalnız GİRİŞ kenarında, hasar parçacıklarının altında,
bir kez çalıyor; bonus pedi `bolge_bir_kere3d` olduğu için oturum başına
**bir kez** çalıyor.

**Sonuç:** tanı sahnesinde (`examples/scene3d_ses_testi.tpr`) kullanıcı beş
katmanı da tek tek denedi ve **hepsi sorunsuz çalıştı**. Yani arena'daki
sessizlik bir arıza değildi — **ölçüm penceresiydi**. Sebep listesinin altıncı
maddesi bu ve ötekilerden farkı şu: kodda yanlış bir şey YOK, yalnız geri
bildirim insan tarafından yakalanamayacak kadar kısa ve seyrek.

**Tasarım kuralı buradan çıktı:** tanı sahnesindeki hiçbir bölge tek-atım
DEĞİL (çıkıp gir, istediğin kadar) ve her çalışta ekrana bir bildirim düşüyor,
böylece duyduğunla gördüğün eşleşiyor. Bir geri bildirimi doğrulatacaksan onu
önce **tekrarlanabilir** ve **görülebilir** yap; yoksa kullanıcıdan gelen
"duymadım" cevabı hiçbir şeyi elemez.

## 3c. Geliştiricinin makinesinde OLAN şey, testi yeşil tutuyor
CI kırmızı, yerel yeşil — ve sebep koddaki bir fark değil, **donanım farkı**.
Yaşandı (2026-09-01, v3.13.0 sürüm PR'ı): `scene3d_engine` CI'da 654 testin
2'sini düşürdü, yerelde hepsi geçiyordu. Sebep: **CI'da ses aygıtı yok.**

Sahne denetimi "ses dosyası yüklenemedi" diye uyarıyordu; oysa dosyalar
sağlamdı, aygıt yoktu. Yani **denetimin kendisi 3b'deki hatayı yapıyordu**:
sessizliğin tek sebebi olduğunu varsaymak. Aynı kusur model denetiminde de
vardı (GL bağlamı yoksa her model -1 döner) ama tetiklenmemişti.

**Çare üç parçalı:**
1. Ayrım: *dosya diskte yok* (kesin kusur, aygıttan bağımsız, her zaman söyle)
   ile *dosya var ama yüklenemedi* (ancak o türden bir şey yüklenebiliyorsa
   söyle — `_chk_kind_ok3`).
2. Kararı SAF bir fonksiyona al (`_chk_asset_warn_n3`) ki küresel varlık
   kaydına dokunmadan sınanabilsin. Böylece regresyon testi **ses kartı olan
   makinede de** kırmızıya dönüyor — makineden bağımsız hâle geldi.
3. Sessiz makineyi taklit et ve iki koşumda da koş:
   ```bash
   env -u XDG_RUNTIME_DIR PULSE_SERVER=/nonexistent \
       ALSA_CONFIG_PATH=/nonexistent HOME=/nonexistent ./build.sh suites
   ```

### 3c-1. İki yan bulgu, ikisi de bu turu pahalılaştırdı
- **Testin kendi artığı.** `t_zone_sound_actually_calls_play` kayda sahte
  GEÇERLİ bir tutamak yazıp geri almıyordu; sonraki testler "ses aygıtı
  çalışıyor" sanıyordu. `scene3d_reset()` varlık kaydını temizlemiyor —
  kaydı bozan test onu **kendi geri almalı**. (Bkz. 1e, 1j.)
- **`bool` tipsiz parametreden geçince tag'ini kaybediyor.** `_chk_asset_warn_n3`
  ilk yazımda `if (tur_calisiyor == false)` diyordu ve parametre tipsizdi:
  farklı tipler arası `==` sabit `false` olduğu için fonksiyon HER koşulda
  `yok + bozuk` dönüyordu. Yerelde görünmedi çünkü ses aygıtı olan makinede
  `bozuk` zaten 0. **Parametreyi tiple** (`bool tur_calisiyor`) ve `== false`
  yerine `if (!x)` yaz.

## 4. Grafik/pencere kuralı
**Asla raylib penceresi açma.** Pencere açan komutlar `DISPLAY=` altında
koşar. Görsel/oynanış testini **kullanıcı yapar**.

- `tame_impl_*` bağlamalarının çoğu `tame_window_ready` denetimi yapıyor;
  yapmayan biri pencere yokken **çöker**. (`tame_impl_scissor` böyleydi.)
- `screen_width()` pencere açılmadan **0** döner → 0'a göre kelepçelemek
  kayıtlı yerleşimi siler.

## 5. Koordinat/ölçek karışmaları
- **Kaydırma:** içerik yüksekliğini EKRAN koordinatında saklamak, sınırın
  kaydırdıkça küçülmesi demek → kaydırma yolun yarısında kilitleniyordu.
  Mutlaklaştırmayı **yazan** tarafta yap, okuyan tarafta değil (okuyan tarafta
  bir kare gecikme kalıyor).
- **Kırpma:** `kirp()` yalnız ÇİZİMİ kırpıyor. Widget kuyruğu ve fare denetimi
  ayrıca kırpma-farkında olmalı, yoksa görünmeyen widget hem çakışma
  dedektörüne kırmızı kutu çizdirir hem tıklamaları yer.
- **Yörünge kamerası:** `_cam_dist` YARIÇAP, biçimdeki "dist" YATAY mesafe.
  Ters dönüşüm tek yerde olmalı.

## 5b. İki ayrı "aynı" sayı
- **Bölüm sayısı İKİ yerde:** `level_count3d()` sahne JSON'undaki bölüm
  dizisini sayıyor; elle yazılmış oyunda (`bolum3d(1, "kur1")`) o dizi BOŞ ve
  oynanabilir sayı `_lvlN`'de. Menü ilk yazımda `level_count3d()` kullandı ve
  "Bölümler" düğmesi kod tabanlı oyunlarda hiç çıkmadı. Bir sayıyı sormadan
  önce **hangi soruyu** sorduğunu belirle: "kaç bölüm serileştirildi" ile
  "kaç bölüm oynanabilir" aynı şey değil.

## 6. Dil ve codegen tuzakları
- **Yerel değişken GLOBAL'i gölgeliyor** (açık codegen hatası) → probe/test
  yazarken benzersiz ad kullan.
- **Aynı adlı iki fonksiyon**: derleyici uyarmıyor, biri sessizce ölüyor.
  `bolum_git3d` iki kez tanımlıydı; sonra `_ed_capture3` (0 argümanlı eski
  hâli + yeni 1 argümanlı) aynı tuzağa düştü — belirtisi "düzeltmem hiç
  çalışmıyor" oldu. Koruma çalışıyor: `t_no_duplicate_function_names`
  kaynağı okuyup ADI VEREREK kırmızıya dönüyor. **Yeni yardımcıya ad
  verirken önce `grep "func <ad>("`.**
- **`%` yok** → `mod()`/`fmod()`. **`/` bir operand float ise float bölme.**
- **Çoklu dönüş yok** → sonuç global ile döner (`_dk_rect3`, `_ed_ray3`).
- Ayrılmış kelimeler: `len`, `tip`, `icinde`, `don`, `dene`, `move`, `metin`,
  `tekrar` — yerel değişken adı olarak kullanma.
- `toString(30.0)` bir ara `"3e+01"` veriyordu (düzeltildi) — üretilen Tulpar
  kodunda geçersiz.

## 6b. Önceden derlenmiş arşivler sessizce çürüyor
`wasm/dist` ve `android/dist` **gitignored** ve elle tazeleniyor. Yeni bir
`aot_tm_*` binding'i eklemek arşivi anında BAYAT yapıyor ve o hedefin her
derlemesi `undefined symbol` ile ölüyor — ama masaüstü build'i, süitler ve
örneklerin hepsi yeşil kalıyor, çünkü hiçbiri o hedefi derlemiyor.
- **Ölçüldü:** `wasm/dist` beş gün bayat kaldı; bu sürede scene3d'nin HER web
  derlemesi link'te patlıyordu. `android/dist`'te eksik sembol sayısı 31'e
  çıkmıştı. Uyarı vardı ve kimse okumadı.
- **Zaman damgası uyarısı yetmiyor:** "kaynak daha yeni" der, "kırık" demez.
  Sarı bir "olabilir" satırı birkaç koşumda gürültüye dönüşüyor.
- **Çare:** `tests/dist_archive_audit.py` — builtin tablosunu okuyup arşivde
  eksik sembolleri ADIYLA sayıyor. Arşiv yoksa atlıyor (o hedef
  kullanılmıyor); varsa ve eksikse web'de HATA (emsdk depoda vendored, yani
  tazelenebilir), Android'de UYARI (NDK vendored değil — düzeltilemeyen bir
  kırmızı, kırmızıyı görmezden gelmeyi öğretir).
- **Yeni binding eklerken:** beş noktayı bağladıktan sonra
  `wasm/build_tame_web.sh` (ve NDK varsa `android/build_tame_android.sh`)
  çalıştır, yoksa o hedefi kırmış olursun.

**Derleme YOLU da denetlenmiyordu.** Arşiv sembolleri tamam olsa bile
manifest yazımı / PIC reloc / link bayrakları / NDK bulma kırık olabilir.
Android hedefi "Temmuz'da emülatörde doğrulandı" diye duruyordu ve o günden
bugüne İKİ ayrı kırık sessizce birikti — masaüstü build'i, 59 süit ve tüm
örnekler bu süre boyunca yeşildi, çünkü hiçbiri o hedefi derlemiyordu.
`build.sh suites` artık NDK varsa bir scene3d oyununu Android'e derleyip iki
ABI + manifest üretildiğini denetliyor (~36 sn; scene3d seçildi çünkü tame'i
de içeriyor, tek derleme iki arşivi birden sınıyor). NDK yoksa atlanıyor.
`TULPAR_NO_ANDROID_SMOKE=1` kapatır.

**Yan tuzak: paket kimliği herkeste AYNIYDI.** `tulpar.toml` yazmayan her
oyun `dev.tulparlang.game` alıyordu, yani cihazda ikinci oyunu kurmak
birincisini **siliyordu** — sebebi hiçbir yerde yazmadan. Kimlik artık çıktı
adından türüyor (`dev.tulparlang.<ad>`; geçersiz karakterler `_`, rakamla
başlıyorsa `g` öneki, Java anahtar sözcüğüyse `_` eki, tümü elenirse `game`).
toml'daki `package` yine eziyor ve **yayınlanmış bir oyunda orada
sabitlenmeli**: çıktı adını değiştirmek kimliği değiştirir, cihazdaki kurulum
güncellenmez, yanına ikinci kopya kurulur.

**Yan tuzak: NDK araması İKİ yerde yazılıydı** — sürücüde
(`aot_pipeline.cpp`, derlemeyi yapan) ve betikte
(`build_tame_android.sh`, arşivleri üreten). İkisi de yalnız
`~/Android/android-ndk-*`'a bakıyordu; Android Studio ise NDK'yı SDK'nın
içine (`~/Android/Sdk/ndk/<sürüm>`) kuruyor. Sonuç: makinede çalışır bir NDK
dururken ikisi de "NDK bulunamadı" diyordu ve Android hedefi kullanılamıyordu.
Artık ikisi de aynı beş yere bakıyor ve `dist_archive_audit.py` ayrışmayı
denetliyor — bir kural iki dosyada yazılıysa, aynı olduklarını SINA.

## 6c. Yayınlanan ARAÇLARIN hiç denetimi yoktu
`tulpar fmt` ve `tulpar typecheck` kullanıcıya doğrudan dokunan iki komut ve
**hiçbir otomasyonda yoklardı** (eski `*_smoke.py` harness'ları kaldırılmıştı).
Sonuç: üç ayrı bozulma birden hayatta kaldı ve hiçbiri bir koşumu kızartmadı.

| Hata | Sonuç |
|---|---|
| `i++` → `i + +` | biçimlendirici DERLENMEYEN kod üretiyordu (84 dosya `++` kullanıyor) |
| `=>` → `= >` | `match` ifadesi ayrışmıyordu |
| `/*` → `/ *` | blok yorumun İÇİ kod gibi dolgulanıyor, dosya ayrışmıyordu |
| `typecheck` ayrıştırma hatasında **0** dönüyordu | "ok" yazıp geçiyordu; bozulmayı yakalayacak tek kapı da kördü |

Kök neden ortak: biçimlendirici **karakter düzeyinde** çalışıyor ve iki
karakterli belirteçleri tanımıyordu; `//` ile dizgiler ele alınmıştı, `/*`
alınmamıştı. Blok yorum satırları artık **olduğu gibi** kopyalanıyor — içeride
hizalanmış tablo/şema olabilir ve onu "düzeltmek" biçimlendiricinin işi değil.

`typecheck` tarafında sayaç ZATEN vardı (`parser_get_error_count`, tam bu iş
için belgelenmiş) ve typeinfer ön-geçişi onu okuyordu; eksik olan yalnız bu
komuttu — **parser hatadan kurtulup kısmi AST döndürüyor**, yani `try/catch`
ve `!ast` denetimine güvenmek yetmiyor.

**Çare:** `tests/fmt_audit.sh` (`build.sh suites`, ~2 sn) her `examples/`,
`examples/en/` ve `lib/` dosyası için üç şeyi ölçüyor: fmt çalışıyor,
**idempotent** (fmt∘fmt = fmt — değilse kaydet-biçimlendir döngüsünde dosya
sonsuza kadar değişir), ve çıktı **hâlâ ayrışıyor**. Bozma denendi: `++`
düzeltmesini geri almak denetimi kızartıyor.

### `tulpar doc`: DERLEME başarısı, BELGE ön koşulu sanılıyordu
Belge üreteci kodgen hatasında **her şeyi atıp hiçbir şey basmıyordu**.
Ölçüldü: üç stdlib modülü (`router`, `middleware`, `http_utils`) hiç
belgelenemiyordu — çünkü KARDEŞ modüllerin sembollerine bakıyorlar
(`_router_port`, `_request`, `json_response`) ve tek başlarına derlenmiyorlar;
birlikte import edildiklerinde tamamen geçerliler.

Belge çıkarmak **bildirimlere** bakar, derlemenin başarısına değil — ve
indeks zaten kodgen'den bağımsız kuruluyordu (`aot_check_and_index` onu
koşulsuz inşa ediyor), `doc` yalnız atıyordu. Artık ayrım net: **ayrıştırma**
hatası belgeyi engelliyor (indeks güvenilmez), **kodgen** hatası yalnız bir
uyarı basıyor ve belge bildirimlerden üretiliyor.

### LSP: "ilan et ↔ uygula" ayrışması
`tulpar --lsp` de hiçbir otomasyonda yoktu. Ölçüldü ve **sağlamdı** — hover,
tanım, referans, tamamlama, imza yardımı ve yeniden adlandırma çalışıyor,
bozuk kodda tanı üretiyor. Yani burada düzeltilecek hata değil, korunacak
çalışan bir yüzey vardı. `tests/lsp_audit.py` (~0.03 sn) `initialize`ın
bildirdiği HER `*Provider` yeteneğini gerçekten çağırıyor: ilan edip
uygulamamak editörde "hiçbir şey olmuyor" demek ve tek bir log satırı bile
üretmiyor. Bozma denendi (hover işleyicisi kapatıldı) ve denetim kızardı.

> ⚠️ **Denetimin kendi aritmetiği de bir hata kaynağı.** İlk yazımda imleç
> sütunlarını elle yazdım, sonra örnek metni kısalttım ve aynı sütun
> parantezin içine düştü: denetim sunucuyu değil kendini kızarttı. Konumlar
> artık metinden hesaplanıyor (`line.index("topla") + 2`).

## 7. Derleme / gömülü lib
- `lib/*.tpr` **derleme zamanında gömülüyor** → değişikliği görmek için
  `cmake -S . -B build-linux` **RECONFIGURE** şart; yalnız `--build` yetmez.
- Kökteki bayat `.a` arşivleri taze derlemeyi gölgeler.
- Koşumları paralel çalıştırma: hepsi CWD'ye `a.out` yazar.
- `./build.sh test` + `suites` aynı anda ~18 GB RAM'e çıkabiliyor → OOM ile
  öldürülebilir; adımları AYRI komutlarda çalıştır.

## 8. Sessiz veri kaybı — [[Editor]]'de tam liste
Belirti hep aynı: hata yok, ekranda bir şey görünmüyor, dosya sessizce
yanlış. Dokuz ayrı örneği [[Editor]]'de tabloda. Ortak kök: **bir işlemin
neyi koruyup neyi atacağı belirsiz bırakılmış.**

**Alt sınıf: PARALEL DİZİ alan listesi İKİ yerde.** Bölgeyi silmek her
diziyi kaydırıyor, çoğaltmak her diziyi kopyalıyor — listeler ayrışırsa
silme kaydırır, çoğaltma düşürür ve ikisi de sessiz. Çare: testin listeyi
**kopyalamaması**; silme fonksiyonunun kaynağından okuyup çoğaltmada
aranması. Aynı aile: `trigger3d`/`spawn3` gibi kurucular yalnız birkaç alanı
alıyor, gerisi varsayılana düşüyor — kopya "aynı görünen ama hiçbir şey
yapmayan" nesne oluyor.

**Alt sınıf: ÖNİZLEME ile SONUÇ ayrı formüllerden.** Panel sürüklemesinin
bırakma önizlemesi ekranın üçte birini boyuyordu, oysa panel payına düşeni
alır — vurgulanan alan bırakınca oluşan alan DEĞİLDİ. Çare: önizlemeyi
sonucun kendi fonksiyonundan üretmek (modeli geçici kurup `_dk_rect3`'ü
okumak), yani "iki formül aynı sonucu veriyor mu" diye ummamak.

**Alt sınıf: TERS işlem simetrisi.** Geri alma ↔ yineleme, kes ↔ yapıştır,
kaydet ↔ yükle — bir çiftin iki ucu durum TAKAS eder ve ikisi de aynı
BİÇİMDE saklamalı. Yinelemenin karşı durumu tek bölüm olarak saklanınca
"CTRL+Z, CTRL+Y" sahneyi siliyordu. Test yazarken **gidiş-dönüşü kapat**:
işlemin tersi tek başına yeşil olabilir, çift ise ancak iki adım sonra
bozulur (bir bozma tam bu yüzden ilk turda kaçtı).

**Alt sınıf: BİR YÖN taşıyor, ÖTEKİ düşürüyor.** Aynı veri için üç yol var —
kaydet (JSON), yükle (JSON→sahne) ve **kod üret** (JSON→`.tpr`) — ve üçünün
alan listesi elle eşlenmiş. `scene_code3d()` bölgenin **eylemini/miktarını ve
sesini**, kuralın da **sesini** hiç yazmıyordu (2026-09-01): JSON'da var,
üretilen kodda yok. Kaydet↔yükle gidiş-dönüşü yeşil olduğu için kimse fark
etmedi; kaybeden yalnız "koda dök" yolundan geçen kullanıcıydı.

Denklik denetimi vardı ama **göremiyordu**: denetim sahnesinde (`toplayici`)
hiç bölge yok, yani bölge kod üretiminin tamamı denetimsizdi. Bu, 1d'nin
("senaryo hiç kurulmuyor") uçtan uca ölçekteki hâli — düzenek doğru şeyi
ölçüyor ama **girdisi o dalı hiç uyarmıyor**.

**Çare iki parçalı:** (1) emitter tamamlandı, (2) denetim artık İKİ sahne
koşuyor — demo + kapsamı KASTEN dolduran `tests/kod_uretimi_tam.scene.json`
(kutu+küre bölge, eylem+miktar+ses, tek atım, kapalı bölge, sesli/sessiz
kural). Üç bozmanın **ikisini yalnız yeni sahne** yakalıyor. **Kural: yeni bir
serileştirilebilir alan ekleyen, düzeneği de büyütür** — yoksa alan sessizce
üç yoldan yalnız ikisinde yaşar.

## İlgili
[[Testing]] · [[Editor]] · [[Scene3D]] · [[Build System]] · [[Decisions]]

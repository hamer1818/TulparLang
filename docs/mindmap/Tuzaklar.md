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

## 3d. Denetim, ORTAM EKSİKLİĞİNİ kusur sanıyor
Bir denetim iki ayrı şeyi ayırmak zorunda: **ön koşul yok** (NDK kurulu değil,
`android/dist` arşivleri üretilmemiş, ses aygıtı yok) ile **kod kırık**. İkisi
de "başarısız" görünüyor ama yalnız ikincisi bir kusur.

Yaşandı (2026-09-01): Android derleme denetimi NDK yokluğunu atlıyordu ama
ARŞİV yokluğunu atlamıyordu. `android/dist` gitignore'lu ve yerelde NDK ile
üretiliyor — temiz bir checkout'ta (yani CI'da) hiç yok. Denetim kırmızı döndü,
oysa kodda hiçbir şey yoktu.

**Kural:** ön koşul eksikse **ATLA, ama SESSİZ ATLAMA** — sebebiyle birlikte
tek satır yaz (`android derleme denetimi: android/dist arsivleri yok —
atlandi`). Sessiz atlama, "asla kırmızıya dönemeyen denetim" tuzağıdır (bkz. 1).

### 3d-1. CI'yı yerelde taklit et — üç tur ucuza gitti
Bu sürüm PR'ında CI **üç kez** kırmızı döndü ve üçü de yereldeki ortam
farkındandı: ses aygıtı (3c), zaman bütçesi, Android arşivleri. Her tur ~15
dakika. Yereldeki tek komutla üçü birden yakalanabilirdi:

```bash
mv android/dist android/dist.bak; mv wasm/dist wasm/dist.bak
env -u XDG_RUNTIME_DIR PULSE_SERVER=/nonexistent \
    ALSA_CONFIG_PATH=/nonexistent HOME=/nonexistent ./build.sh suites
mv android/dist.bak android/dist; mv wasm/dist.bak wasm/dist
```

**Sürüm PR'ı açmadan önce bunu koş.** CI ortamı senin makinen değil: ses
aygıtı yok, pencere yok, önceden derlenmiş arşiv yok, çekirdek sayısı ~4 ve
saat daha yavaş.

### 3d-2. Zaman bütçesi de bir ölçüm — tahmin değil
`./build.sh test` adımının 10 dakikalık bütçesi tükenmişti ve **aynı ağacın
iki ardışık koşumu sınırın iki yanına düştü**: 9dk46sn (geçti, 14 sn pay) ve
>10dk (aştı). Bütçeyi koşucunun hız değişkenliği çevirebiliyorsa o bir koruma
değil, kumar.

Sebep dürüst: örnek sayısı 87 → 129 (`examples/en/` ikizleri 2026-08-25'te ilk
kez koşuma girdi) ve `scene3d_*` örnekleri dosya başına çok ağır — **her biri
`lib/scene3d.tpr`'yi (~16k satır) sıfırdan derliyor**, önceden derlenmiş modül
önbelleği yok. Yerel: 16 çekirdek @5GHz'de 100 sn; 4 çekirdekli koşucu ~7×
yavaş → ~12 dk. Bütçe 25 dakika (~2× pay).

**Bir zaman aşımı SONRAKİ HER ADIMI atlar.** O koşumda paketler hiç koşmadı,
ama "suites yeşil" diye okunabilirdi — adım listesine bak, çıktının son satırına
değil.

## 3e. Bir platform HİÇ sınanmıyorsa, orada her şey bozuk olabilir
macOS CI işi **test koşmuyor** — derliyor, artefaktı yüklüyor, bitiyor.
Sonucu (2026-09-02'de ölçüldü): **yayınlanan `tulpar-macos-universal` ile
hiçbir program derlenemiyordu.** `ld: library 'ssl' not found` — CMake OpenSSL'i
bulunca link satırına `-lssl -lcrypto` giriyor ama `-L` yolu girmiyordu ve
Homebrew OpenSSL'i keg-only bir dizinde tutuyor. Linux'ta görünmedi: `libssl`
orada `/usr/lib`de.

Hata aylarca durdu ve ancak TameEngine paketleme adımı macOS'ta **ilk kez bir
`tulpar build` çağırınca** ortaya çıktı. Yani bulunması bir tesadüftü.

**Ders:** "derleniyor" ile "çalışıyor" arasındaki fark bir platformda hiç
ölçülmüyorsa, o platform için hiçbir iddian yok. Bir artefaktı yayınlamak onu
sınamak değildir. Yayınlanan her platformda **en az bir uçtan uca iş** koşmalı
(bir `tulpar build` + çalıştır yeter).

## 3f. `LLVMConstNamedStruct` tip uyuşmazlığını SESSİZCE `undef` yapıyor
Bu, bir performans işinden çıktı ama asıl ders C API'sinin kendisiyle ilgili.

`VMValue` LLVM'de `{i32, [4 x i8], i64}` diye modellenmişti. Dolgu bir **bayt
dizisi** olduğu için üretilen kodda her VMValue kopyası dört ayrı `movzbl` +
dört bayt store'a açılıyordu — dilin HER YERİNDE. Elek kıyaslamasının iç
döngüsünden okundu (2026-09-03):

```
mov    (%r12),%edx        ; etiket
movzbl 0x4(%r12),%esi     ┐
movzbl 0x5(%r12),%edi     │ dolgu, tek tek
movzbl 0x6(%r12),%r8d     │
movzbl 0x7(%r12),%r9d     ┘
mov    0x8(%r12),%rax     ; yük
```

Dolguyu düz `i32` yapmak yerleşimi değiştirmiyor ve ölçülen kazanç gerçek
(A/B, 9 tekrar × 2 tur: `sieve` 24,6/27,5 → 21,9/22,0 ms).

**Ama ilk denemede scene3d'nin 10 çarpışma/fizik testi düştü** ve sebebi üç
yanlış hipotez boyunca bulunamadı. Kilit gözlem şuydu: **düşen testin gövdesi
tek başına çalıştırıldığında DOĞRU sonuç veriyordu.** Aynı test pakette
düşüyordu. "Önceki testlerin bıraktığı belleğe göre değişen bir şey var" =
tanımsız değer izi.

Üretilen IR (`TULPAR_AOT_EMIT_LL_PRE=1`) tek bakışta söyledi:

```llvm
store %struct.VMValue { i32 1, i32 undef, i64 4580687789900693504 }, ptr @DEG2RAD3
```

**Sebep:** sabit VMValue kurucuları dolguyu üç ayrı yerde elle
`LLVMConstNull(LLVMArrayType(i8, 4))` diye kuruyordu. Alan `i32` olunca
`LLVMConstNamedStruct` tip uyuşmayan sabiti **hata vermeden `undef`e
çeviriyor**. Global'lere yazılan undef, hangi testin önce koştuğuna göre
değişen değerler üretti.

**Ders — C API'si tip uyuşmazlığında susuyor.** Bir struct alanının tipini
değiştiren, o alana sabit üreten HER yeri de değiştirmek zorunda; derleyici
uyarmıyor. Çare yazmayı ortadan kaldırmak: sabit artık tipini struct'ın
kendisinden alıyor (`llvm_vm_val_padding_zero` →
`LLVMStructGetTypeAtIndex(vm_value_type, 1)`), yani alan ne olursa olsun uyuyor.

**İkinci ders — "tek başına geçiyor, pakette düşüyor" bir imzadır.** Hesap
yanlış değil; tanımsız bellek okunuyor. Doğrudan IR'a bak, hipotez üretme.
(Denenip elenenler: dolguyu sıfırdan başlatmak, tip cezalandırmasını bit
işlemine çevirmek, alanları ayrı okumak — sonuncusu ayrıca **yavaşlattı**,
LLVM'in tek 16 baytlık taşımasını bozuyor.)

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

## 6d. Struct'a alan eklemek: yeniden adlandırma OKUMAları yakalar, eksik İLKLENDİRMEyi yakalamaz

`ObjArray`'e `idata` alanı eklerken (kutulanmamış diziler, 2026-09-03) bilinçli
bir hile kullanıldı: `items` alanı `items_` olarak yeniden adlandırıldı, böylece
ona **doğrudan dokunan her yer derlenmedi** ve 101 erişim noktası derleyici
tarafından tek tek önüme getirildi. Bu kısım işe yaradı.

**Ama yakalamadığı şey vardı:** `ObjArray` kurucuları alanları tek tek atıyor
(`calloc` değil, `malloc` + alan alan atama). Yeni alanı 20 kurucunun hiçbiri
sıfırlamıyordu ve derleyici bundan hiç şikâyet etmedi — yeni bir alanı
*okumamak* hata değil. Sonuç: `malloc`'tan gelen çöp değer NULL olmadığı için
sahte bir işaretçi `free()` edildi → **`free(): invalid pointer`**, 4 satırlık
bir programda bile.

Belirti aldatıcıydı: çöküş derleyicide değil, **üretilen kullanıcı ikilisinde**
oluyordu ve `tulpar` yalnızca 1 döndürüyordu; suite çıktısında tek satır
`free(): invalid pointer` görünüyordu, hangi testin patladığına dair hiçbir iz
yoktu.

**Kural:** alanları tek tek atayan bir struct'a yeni alan eklerken, kurucuları
saymakla yetinme — `grep`'le *her* kurucunun yeni alanı atadığını **programla**
doğrula:
```
python3 -c "..."  # 'type = OBJ_ARRAY' satirini takip eden satirda alan var mi?
```
Yeniden adlandırma hilesi okuma tarafını kapatır; yazma/ilklendirme tarafını
kapatmaz. İkisi ayrı denetim.

## 6e. TBAA doğruluğu çıktı testiyle ÖLÇÜLEMEZ

Dizi erişimini hızlandırmak için TBAA meta verisi eklendi (eleman deposu ile
ObjArray başlığı ayrı takma-ad sınıfı). TBAA yanlışsa sonuç sessiz
yanlış-derlemedir, o yüzden doğrulamak şart görünüyordu.

Yazılan stres testi (aynı döngüde okuma + `push` büyümesi + döngü ortasında
kutuya dönüş + yazıp hemen okuma) geçti. Sonra **kasten yanlış etiketleme
enjekte edildi** — eleman yazması `header` olarak işaretlendi, yani LLVM'e
"bu yazma eleman okumasını etkilemez" diye yalan söylendi.

**Test yine geçti.** Çünkü LLVM yanlış takma-ad bilgisini *sömürmek zorunda
değil* — sömürebilir. Yani yeşil bir test TBAA'nın doğru olduğunu göstermez;
yalnızca bu derleyici sürümünün bu programda o bilgiyi kullanmadığını gösterir.
Bu, [[Tuzaklar#1|hiçbir şey ölçmeyen test]] sınıfının derleyici hâlidir ve
enjeksiyon disiplininin **sınırını** işaretler.

**Güvence gerekçeden gelmeli:** eleman deposu (`malloc`/arena bloğu) ile başlık
(`ObjArray` struct'ı) ve değişken yuvaları (alloca/global) her zaman **ayrı
ayırmalar** — hiçbir bayt ikisi olarak erişilmiyor. Arena baytları geri
dönüştürebilir, ama bu ancak `arena_restore` çağrısıyla olur ve LLVM, nitelik
taşımayan dış çağrıların ötesine bellek işlemi taşıyamaz. Dolayısıyla tek bir
düz kod bölgesi aynı baytları iki tip olarak göremez.

TBAA değiştirirken: gerekçeyi yaz, teste güvenme.

## 6f. Tek atışlık kıyas ölçümü yalan söyler

Elek optimizasyonunu ayrıştırırken aynı ikili iki kez ölçüldü: **8.9 ms** ve
**12.2 ms**. Fark %37 — yani ölçüm, aranan etkiden büyüktü.

Sebep: her yapılandırma arka arkaya bir kez çalıştırılmıştı. Serideki **ilk**
koşu sistematik olarak yüksek çıkıyor (ısınma / frekans / önbellek). Sırayı
iç içe geçirip (`none, all, all, none` × 4 tur, her turda 5 koşunun en iyisi)
ölçünce tablo kararlı hâle geldi: 9.1 / 12.6.

Bu, "hızlandırdık" derken en kolay kandırılma biçimi — ve bu oturumda daha
önce de olmuştu (şanslı bir best-of-5'ten "18.8 ms" bildirilmişti, gerçek
~20.6–21.4). **Kural:** yapılandırmaları iç içe ve simetrik sırada, birden çok
tur, her turda min al. Tek seride arka arkaya ölçme.

## 6f-2. Tavan ölçümü: iki program TEK bir şeyde ayrılmıyorsa sayı yalan

Bir optimizasyona girişmeden önce "tavan ne kadar" diye C'de model yazmak
doğru refleks. Ama model programı kurmak, ölçmek kadar dikkat ister —
2026-09-06'da aynı gün **iki kez** yanlış sayı üretildi:

| deneme | ne yanlıştı | sonuç |
|---|---|---|
| `ceil.c` A vs B | A'nın DIŞ döngüsü yerel `i`, B'ninki global `g_i` kullanıyordu | bekçiye 0,93 ms fatura edildi, oysa farkın çoğu dış döngüydü |
| `ceil2.c` A/R/G/B | dört bicim `if (m=='A')` ile SICAK dış döngünün içinde seçiliyordu | 5M kez çalışan seçim farkı gizledi, "bekçi bedava" çıktı |

İkisi de kendi içinde tutarlı, ikisi de yanlış. Doğrusu üçüncü denemede:
tek bir kaynak, **tek** `#define` değişiyor (`width.c` / `width2.c`),
dallanma dışarıda. O zaman sayılar oturdu:

| ölçüm (elek, N=5M, pinlenmiş) | ms |
|---|---|
| C, int64 eleman, bekçisiz | 8,38 |
| C, int64 eleman, **bekçi + soğuk yol** | 9,35 |
| C, int32 eleman, bekçisiz | 7,78 |
| Tulpar bugün | 9,36 |

Yani Tulpar tam olarak "bekçili C" hızında; bekçi 0,97 ms, eleman
genişliği 0,60 ms.

**Kural:** tavan modelinde değişen tek şey ölçtüğün şey olmalı. Aynı
kaynak + `-D` ile derle; "mod" seçen bir `if` sıcak döngüye girmesin;
çıktıyı doğrula. Bir sayı beklentiye uymuyorsa önce **modeli** şüphelen.

## 6g. `a[i]` düğümünde taban İKİ ayrı alanda olabilir

Şekil önbelleği kodu yazıldı, testler geçti, tanılama "önbelleğe alındı" dedi
— ama üretilen makine kodu **hiç değişmedi**. Sessiz hiçbir-şey-yapmama.

Sebep: `AST_ARRAY_ACCESS` düğümünde taban ifadesi bazen `node->name`'de,
bazen `node->left`'te duruyor (parser iki biçimi de üretiyor; for-in şeker
açılımı `left` kullanıyor). Arama yalnız `name`e bakıyordu, elek de `left`
biçimini üretiyordu → arama hep boş döndü.

Aldatıcı olan: mevcut codegen zaten `if (node->name) ... else if (node->left)`
diye **iki biçimi de** ele alıyordu, yani doğru kalıp gözümün önündeydi; yeni
kod tek biçime baktı. Aday toplayıcı iki biçimi de ele aldığı için "aday
bulundu" logu doğru çıkıyor ve sorunu gizliyordu.

**Kural:** AST alanına yeni bir yerden erişirken, o alana **zaten erişen**
kodun nasıl yaptığına bak — tek alan yerine `array_base_name()` gibi ortak bir
yardımcı kullan. Ve bir optimizasyon eklendiğinde "test geçti" yetmez:
**üretilen kodun gerçekten değiştiğini** doğrula (objdump / ölçüm). Bu hata
hiçbir testi kırmaz, yalnızca kazanç vermez.

## 6h. Kullanıcı değişkeni libc sembolünü ezerse: derleme yeşil, ikili çöker

`int free = 3;` yazan bir Tulpar programı **derleniyor** — `[AOT] Successfully
created` — sonra ilk serbest bırakmada SIGSEGV atıyordu (çıkış 139), derleyici
tek kelime etmeden. `nm` çıktısı sebebi söylüyor: **`B free`**. Kullanıcı
küreselleri LLVM'e ham adla yazıldığı için üretilen ikilideki `free` sembolü
libc'nin `free()`'sini eziyor ve runtime'ın her `free()` çağrısı bir veri
adresine atlıyordu. `stdout` ve `malloc` da aynı.

Bu sınıfın tehlikesi: **hata derleyicide değil, bağlayıcıda ve sessiz.** Her
katman kendi işini başarıyla yaptığını bildiriyor.

Yanıltıcı olan yanı: `printf`, `strlen`, `memcpy`, `index`, `time`, `log`,
`remove`, `exit` gibi adlar denendiğinde **sağlam çıktı** — ama kural onlarda
da yoktu, yalnızca o sembollerin çözümlenme biçimi denk gelmişti. "Birkaç ad
denedim, çalışıyor" bu sınıfta kanıt değil.

Çözüm: kullanıcı küresellerinin sembol adı `tpr_g_` önekli (`gsym()` —
`llvm_backend.cpp`). Yaratma ve arama noktalarının **hepsi** oradan geçmeli;
biri atlanırsa küresel sessizce bulunamaz.

`internal` linkage de çözerdi ve önce o denendi — **ölçüldü: elek kıyasını %15
yavaşlatıyor** (9.6 → 11.0 ms). Önek bedava. Hipotez ("internal daha iyi
optimize edilir") ölçümle çürüdü; ölçmeden alınsaydı sessiz bir gerileme
girecekti.

Test yazarken düşülen tuzak: değişkenler **üst düzey** olmalı. İlk yazılışında
`func` içine konmuşlardı — yerel oldukları için hiçbir sembol üretmiyorlardı,
test yeşildi ve enjeksiyon hiçbir şey yakalamıyordu.

## 6i. Hızlı yol ile yavaş yol AYNI şeyi yapmalı — yoksa dil kendiyle çelişir

İki hata aynı kalıptan çıktı (2026-09-04, ikisi de sondalamayla bulundu):

**Tamsayı sıfıra bölme.** Kutulu yol doğruydu: "Sifira bolme" basıp 0
dönüyordu. Tipli hızlı yol ham `sdiv` üretiyordu — x86'da #DE, yani SIGFPE.

```
int n = toInt(env("YOK"));   // 0
print(10 / n);               // program BURADA ölüyor, tek kelime etmeden
print("bu satır hiç çalışmıyor");
```

Aynı programda `%` düzgün çalışıyordu. Yani **aynı dilde aynı işlem iki
farklı davranış** gösteriyordu, ve hızlı yolun davranışı sessiz ölümdü.
`INT_MIN / -1` de aynı tuzak ve o kutulu yolda da vardı.

**Yerleşik gölgeleme.** typeinfer'ın belgelenmiş kuralı "yerel tanım her zaman
kazanır"dı; codegen'de yerleşik kazanıyordu. `func exit(int x)` tanımlayan bir
program `exit(5)` çağırınca süreç 5 ile sonlanıyordu. 11 yerleşik aynı.

**Blok kapsamı.** `for` gövdesi yeni kapsam açıyordu, `if`/`while` gövdeleri
açmıyordu. Yani `if (true) { int x = 5; }` dıştaki `x`i eziyordu — sessizce.
Aynı dilde iki farklı kapsam kuralı.

**Kural:** bir işlemin iki uygulaması varsa (tipli/kutulu, satır içi/runtime,
`for` vs `if`), davranışları TEST EDİLEREK eşitlenmeli. "Hızlı yol yalnızca kısayol" demek
yetmez — kanıtlanmalı. Yeni bir hızlı yol eklerken sorulacak soru: *yavaş yol
bu girdide ne yapıyor?*

**Ölçüm tuzağı:** `10 / 0` sabitiyle yazılan bir test bu hatayı GÖREMEZ — LLVM
katlıyor. Bölenin katlanamaz olması gerek (ortamdan gelen değer). İlk sonda
sabitle yazılmıştı ve temiz görünüyordu.

## 6j. Sondalama: "doğru yazılmış program" testleri bu sınıfı hiç görmez

Paketler ve örnekler doğru yazılmış programları koşuyor. `tests/
silent_failure_probe.py` kenar durumlarını koşuyor ve özellikle **derleyicinin
"başarılı" deyip yanlış sonuç ürettiği / ikilinin çöktüğü** sınıfı arıyor.

**Sekiz** gerçek hata bununla bulundu: `int free = 3;` (libc sembol ezme,
[[Tuzaklar#6h]]), `func exit(...)` (yerleşik gölgeleme), `10 / n` (sessiz
SIGFPE), `a[0]++` (sessiz hiç-işlem), `if` gövdesinin kapsam açmaması, float
literallerinin float32'ye kırpılması, biçimleyicinin aynı kırpmayı yapması ve
`print`in `toString`den **farklı sayı** basması ([[Tuzaklar#6k]]). Hiçbiri 66
paketin veya 40 örneğin gözüne çarpmamıştı — çünkü hiçbiri böyle bir program
yazmıyor.

`a[0]++` özellikle öğretici: `parse_postfix`'te `match(TOKEN_PLUS_PLUS)` token'ı
**tüketiyor**, ama hedef `Identifier` değilse gövde çalışmıyordu — `++` yutulup
ifade `a[0];` olarak kalıyordu. **Token tüketen bir `match()`ten sonra her yolda
bir şey üretildiğinden emin ol**; üretmeyen dal sessiz hiç-işlem demek.

### fmt gidiş-dönüş
Sonda ayrıca her kaynağı `tulpar fmt`'den geçirip **yeniden koşuyor** ve aynı
sonucu bekliyor. Güçlü bir değişmez: `%=` eklenirken fmt onu `% =` diye bölüp
kodu BOZUYORDU — ve aynı sınıf daha önce `=>` için de olmuş (formatter.cpp'deki
yorum anlatıyor). Tek operatörü düzeltmek yetmez, değişmezi koy.

Sonda eklemek ucuz: `c("ad", "kaynak", "beklenen çıktı")`. Sonda `LC_ALL=C` ile
koşuyor, çünkü tanı metinleri yerele göre değişiyor.

**Sondanın kendi tuzağı:** bir sonda yanlış yazılırsa sessizce hiçbir şey
ölçmez. Ayrıca sonda bir sınıfı HİÇ göremez: tampon taşmasını görünür
çıktı testiyle yakalayamazsın ([[Tuzaklar#6m]]). `func exit` sondası önce fonksiyon İÇİNE yazılmıştı (yerel değişken →
hiç sembol üretmiyor), `len(dizgi)` sondası dizgiyi indekslemiyordu (önbelleğe
aday bile değil). İkisi de yeşildi ve ikisi de hiçbir şey ölçmüyordu. Her
sondayı enjeksiyonla sına.

## 6k. Bir varsayım üç katmanda tekrarlanınca kendini gizler

2026-09-05: Tulpar'ın `float`u çalışma zamanında **double** —
`backend->float_type = LLVMDoubleType`, `VMValue` payload'ı 8 bayt. Ama üç
ayrı yerde float32 varsayımı vardı ve **her biri diğerini görünmez kılıyordu**:

1. `ASTNode_C.value.float_value` alanı `float` idi (32-bit). Kaynaktaki her
   float literali C-köprüsünde kırpılıyordu: `float pi = 3.141592653589793;`
   gerçekte `3.1415927410125732` derleniyordu.
2. `aot_format_float` değeri önce `(float)`e yuvarlayıp en kısa **float32**
   gösterimini arıyordu. Gerekçesi kendi yorumundaydı: *"the LLVM backend uses
   a 32-bit float type"* — **yanlış**; `LLVMFloatTypeInContext` bu repoda hiç
   olmadı (`git log -S` ile bakıldı).
3. `print` o biçimleyiciyi hiç kullanmıyordu: codegen `aot_print_value` →
   `vm_print_value` (vm.cpp) → düz `printf("%g")`, altı anlamlı hane.

**Neden altı hafta görünmedi:** (1) değeri kırpıyor, (2) kırpılmış değeri
kırpılmış biçimde basıyor. İkisi *aynı yönde* yanlış olduğu için çıktı
tutarlı görünüyordu. (3) ise en üstte oturup ikisini birden `%g`nin arkasına
saklıyordu.

**Ders:** bir tür/genişlik varsayımı katmanlar arasında **tekrarlanıyorsa**,
uçlarını ayrı ayrı ölç. Buradaki ayırıcı sonda, hesabı sonucun kendisine
değil, *beklenen kırpılmış değere olan farka* bakmaktı:
`print((pi - 3.1415927410125732) * 1e12)` → kırpılmışsa tam `0`, double ise
`-87422.8`. `print(pi)` tek başına ikisini de `3.14159` gösteriyordu.

**İkinci ders — testin kendisi hatayı sabitleyebilir:**
`tests/float_format.test.tpr` içinde `toString(0.1+0.2) == "0.3"` yazıyordu.
Bu float32 yuvarlamasının *beklentiye çevrilmiş hâliydi*: düzeltme yapılınca
test kırmızıya döndü ve ilk refleks "düzeltme yanlış" demek oldu. Doğrusu
`0.30000000000000004` (Python/Go/Rust/JS de böyle basar). Bir test bir hatayı
kilitliyorsa, başlığındaki gerekçeyi oku — oradaki cümle de yanlıştı.

### Aynı değer, iki farklı biçimleyici
`print(x)` ile `print("" + x)` **farklı sayılar basıyordu** (`1e+06` ve
`1000000.5`): `toString` doğru biçimleyiciyi kullanıyordu, `print`
kullanmıyordu. `runtime_bindings.cpp`'deki doğru `print_value` /
`print_vm_value` çifti AOT yolunda **ölü koddu** (VM'den kalma). Bu
[[Tuzaklar#6i]]'nin (hızlı yol ↔ yavaş yol ayrışması) biçimleme hâli:
**aynı değeri metne çeviren iki yol varsa aynı fonksiyonu çağırmalılar.**

### Enjeksiyon bunu söyledi, ben değil
(3)'ü geri alınca `float_precision.test.tpr` **yeşil kaldı** — çünkü
assert'ler `toString`den geçiyor, `print`ten değil. `print`in biçimleyicisi
yalnız **stdout karşılaştıran** sondada görünüyor. Bir davranışı test etmek
istiyorsan, testin o davranışın *geçtiği yoldan* geçtiğini enjeksiyonla
doğrula ([[Tuzaklar#6j]] "sondanın kendi tuzağı").

## 6l. Runtime'a düşen her çağrı şekil önbelleğini bayatlatır

`a[i]++` ve `a[i] += 5` runtime'a giriyor: `vm_get_element` →
`vm_array_get` → `arr_items` → **`arr_debox`**, yani KUTUSUZ dizi kutulanıyor
ve `idata` **`free`** ediliyor. Döngü başında önbelleğe alınan `idata` o an
sarkıyor; aynı döngüdeki sonraki önbellekli erişim serbest belleğe yazıyor.
Ölçüldü: `malloc(): unsorted double linked list corrupted`.

Değişmez zaten vardı (`emit_shape_refresh_all`, `vm_get_element` /
`vm_set_element` çağrılarından sonra) — yeni eleman yolu onu **uygulamayı
unuttu**. Sağ taraf `a[j]` okuyabildiği için tazeleme `get`ten **sonra**,
okumadan **önce** olmalı.

**Regresyon testi 4096 eleman kullanıyor, bilerek:** ilk yazılan döngü testi 4
elemanlıydı ve `free` o boyutta belleği işletim sistemine geri vermiyor —
sarkan işaretçi hâlâ eski değerleri okuyor, test **yeşil geçiyordu**. Serbest
bellek hatasını arayan test, tahsisin gerçekten geri verileceği boyutta olmalı.

## 6m. Görünür davranış testi TAMPON TAŞMASINI göremez

2026-09-05: `sb_append(int)` rakamları doğrudan tampona yazacak şekilde
değiştirildi. Ayrılan yeri **24 bayttan 4 bayta düşüren** enjeksiyon
denendi — yani gerçek bir heap taşması — ve **68 paketin, 40 örneğin, 75
sondanın hiçbiri kırılmadı.**

Sebep: taşan baytlar `malloc`'un boş payına düşüyor. Program çökmüyor,
okunan değer doğru çıkıyor, görünür davranış aynı. Test doğru yazılmıştı;
**ölçemeyeceği bir şeyi ölçüyordu**.

Aynı sınıf o gün ikinci kez çıktı: şekil önbelleğindeki sarkan `idata`
([[Tuzaklar#6l]]) yalnız 4096 elemanda görünür oldu — 4 elemanlı ilk test
yeşil geçmişti, çünkü `free` o boyutta belleği işletim sistemine geri
vermiyor.

**Çözüm: `tests/runtime_asan.c` + `tests/run_asan.sh`.** Runtime giriş
noktalarını doğrudan çağıran, ASAN'la derlenen bir C koşum takımı. Aynı
enjeksiyonu deterministik yakalıyor:

```
==ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 1 ... in aot_itoa
```

CI'da değil (ASAN derlemesi ~2 dk). **Runtime'ın belleğe dokunan bir
yerini değiştirdiğinde elle koş** — `bash tests/run_asan.sh`.

**Ders:** bellek güvenliği `.test.tpr` ile sınanamaz. Bir runtime
fonksiyonu ham işaretçiyle yazıyorsa (tampon, dizi, dizgi), doğruluğunun
kanıtı görünür çıktı değil, sanitizer'dır.

## 6n. Sıcak döngüde soğuk yolun VARLIĞI bedava değil

Döngü sürümleme (2026-09-05) buradan çıktı. Üç kademeli tavan ölçümü
(arrayiter, n=5M) kazancın nereden geldiğini söyledi:

| | ms |
|---|--:|
| normal | 5,53 |
| **A**: bütün bekçiler kaldırıldı | 3,51 |
| **B**: dal DURUYOR, yavaş yol ölü | **3,42** |

**B ≈ A.** Dal duruyor, kazanç aynı → maliyet dalın *çalışması* değil,
yanındaki kodun **döngü gövdesinde durması**. O kod LLVM'in sıcak yolu
açmasını/vektörleştirmesini engelliyor ve derleyiciye fazladan tümevarım
değişkeni (`add $0x10,%rbx`) taşıttırıyor.

İki uygulama, ikisi de bu ilkeden:
1. Şekil tazelemesini satır içinden **modül-yerel fonksiyona** almak.
2. `for (i=C; i<len(a); i+=K)` içinde `a[i]` için döngüyü **sürümlemek**
   — kanıtla bekçi de yavaş yol da tümden yok.

## 6o. Yığından TAŞAN OKUMA çıktı testiyle YAKALANMIYOR

`shape_access_proven`'ın "indeks tam olarak `i` olmalı" koşulu
kaldırıldı — yani `a[i + k]` de bekçisiz üretildi — ve **17 testin hepsi
yeşil kaldı**. Üç kez denendi, her seferinde daha agresif:

| deneme | sonuç |
|---|---|
| 64 elemanlı dizide `a[i+1]` | 0 döndü, yeşil |
| aynısı, toplamı denetlenerek | yeşil |
| **4 elemanlı dizide `a[i + 4096]`** (32 KB ötesi) | **0 döndü, yeşil** |

Yığından taşan okuma pratikte sıfır dönüyor: `malloc` bloğu yuvarlıyor,
ötesi de eşlenmiş sıfır sayfa. [[Tuzaklar#6m]]'in (taşan YAZMA) okuma
hâli — ama yazmayı ASAN yakalıyordu, bunu **hiçbir aracımız yakalamıyor**
(ASAN koşum takımı runtime C fonksiyonlarını sınıyor, üretilen kodu
değil).

**Ne yapıldı:** karar tek bir adlandırılmış fonksiyona toplandı
(`shape_access_proven`) ve sınanamadığı **orada** yazıldı. Güvence testte
değil, o fonksiyonun dar ve tek olmasında. Bir gün "indeks varsa yeter"
diye gevşetilirse hiçbir test kırmayacak — bunu bilerek kabul ediyoruz.

**Gelecek iş:** `tulpar build --sanitize` (üretilen IR'a ASan geçişi)
bu sınıfı kapatırdı. Şu an yok.

## 6p. Koşmayan kod, taşınabilirlik hatalarını SAKLAR

macOS işine dil paketleri eklenirken **iki gerçek hata** çıktı; ikisi de
macOS'ta *her* koşumu düşürürdü ve ikisi de fark edilmemişti:

- `build.sh suites` → çıplak `timeout 180`
- `tests/pkg_audit.sh` → çıplak `timeout 30`

`timeout` GNU coreutils'te; **macOS'ta yok** (orada `gtimeout`). Öğretici
olan: `build.sh test` bu sorunu zaten çözmüştü (`TIMEOUT_BIN` + `gtimeout`
yedeği) — yani biri bir kez farkına varmış, ama düzeltme **koşan** koda
girmiş, koşmayana girmemiş. Kod bir platformda hiç koşmuyorsa oradaki
hataları da hiç göstermiyor; hata "yok" değil, **görünmez**.

**Ders:** bir işi yeni bir platformda koşturmadan önce "orada zaten çalışır"
varsayma. Koşturduğun anda taşınabilirlik hataları *ilk kez* görünür hâle
gelir — ve genelde birden fazla olurlar.

### macOS = ARM64: tek doğrulama noktası
`macos-latest` Apple Silicon, Linux işi x86-64. `CMakeLists.txt` mimariye
göre ayrı LLVM backend'i linkliyor ve `vmvalue_abi_uses_sret()` ABI'yi
çalışma zamanında seçiyor — **AArch64 ayrı bir kod üretim yolu.** 2026-09-06
öncesinde o yolu yalnız `tests/aot_smoke.sh`'ın 4 vakası doğruluyordu; duman
"derleyici çalışıyor mu"yu ölçer, "dil doğru mu"yu değil.

### Hedef platformda koşturamıyorsan, en yakınını taklit et
macOS burada yok. Adım körlemesine bırakılmadı: `build/` dizinine temiz
derleme + **kökte arşiv yok** (CI'daki durum) + `TULPAR_NO_HWSTAT=1` ile tam
adım yerelde koşuldu. Kalan risk yalnız ARM64'ün kendisi. Taklit, ortam
farklarının çoğunu (yol, arşiv arama, telemetri) önceden yakalıyor.

## 6q. Yerel LLVM sürümü GEÇERSİZ IR'ı gizler

Döngü sürümlemenin bekçisiz okuma dalı, `arr.chk/fast/slow/done` blokları
**yaratıldıktan sonra** duruyordu ve erken `return` geriye **sonlandırıcısı
olmayan dört boş temel blok** bırakıyordu. Geçersiz IR.

| | sonuç |
|---|---|
| LLVM 22 (yerel) | tolere ediyor, 69/69 **yeşil** |
| LLVM 18 (CI) | **SEGFAULT** |

Yani yerelde yeşil olması bir şey kanıtlamıyordu — geçersiz IR üretiliyordu
ve yerel sürüm onu **temizliyordu**. CI kırmızısı ortam farkı değil, **gerçek
bir codegen hatasıydı**.

**Kural:** `LLVMAppendBasicBlock` ile blok yarattıktan sonra o yoldan
`return` etme. Erken çıkış dalları blok yaratımından **önce** olmalı. Kod
tarafında koşul yorumla işaretlendi ki bir daha aşağı kaymasın.

**Neden testler görmedi:** `.test.tpr` paketleri geçersiz IR'ı göremez —
görebilecekleri tek şey sonucu, ve LLVM 22'de sonuç doğru. Bu sınıfın
doğal aracı IR doğrulayıcısıdır; pipeline'da `VerifyEach` kapalı.

### CI'yı taklit et, 14 dakikalık döngüye mahkûm olma
Reprodüksiyon Docker'da yapıldı: `ubuntu:24.04` + `llvm-18-dev` + `clang`
(+ `zlib1g-dev libzstd-dev libtinfo-dev`, yoksa `LLVMExports.cmake`
`ZLIB::ZLIB` bulamıyor). Kaynak ağacı **salt-okunur mount edilemiyor** —
derleme `src/embedded_libs.h` yazıyor — o yüzden konteyner içinde
yazılabilir bir kopya çıkarılıyor. Daraltma sondası hangi biçimin çöktüğünü
tek tek gösterdi ve hata dakikalar içinde bulundu.

## 6r. Küresel LLVM bağlamı: doğrulayıcı düşer, optimizasyon SESSİZCE iner

En pahalı sessiz hata sınıflarından biri. LLVM-C'nin bazı çağrıları
**küresel bağlamı** kullanıyor; modülümüz ise `LLVMContextCreate()` ile
**ayrı** bir bağlamda:

```c
LLVMBasicBlockRef LLVMAppendBasicBlock(LLVMValueRef Fn, const char *Name) {
  return LLVMAppendBasicBlockInContext(LLVMGetGlobalContext(), Fn, Name);
}                                       // ^^^ KÜRESEL
```

Sonuç: aynı yazılan tip **iki ayrı `Type` nesnesi** oluyor. `<2 x i64>`
ile `<2 x i64>` eşit değil.

**Nasıl ortaya çıkıyor (2026-09-06 ölçümü):**

| belirti | görünen |
|---|---|
| `LLVMVerifyModule` | `MDNode context does not match Module context` |
| aynısı, vektörleşen döngüde | `Both operands to a binary operator are not of the same type!` |
| basılan IR | `add <2 x i64> %vec.ind, <2 x i64> splat (i64 2)` |
| o IR'ı `llvm-as`'e ver | **ayrıştırılamıyor** — `expected type` |

Son iki satır aynı şeyin iki yüzü: yazıcı ikinci işlenenin tipini
**ayrıca** basıyor, çünkü kendi gözünde de tipler farklı. Doğrulayıcının
kendi yazdığı metni kendi ayrıştırıcısı kabul etmiyorsa şüphelenilecek
şey LLVM değil, **bağlam karışmasıdır**.

**Bedeli:** doğrulama düşünce derleyici O3→O2→O1 merdivenine iniyor.
`arrayiter` bu yüzden **O1'de** derleniyordu: bütün testler yeşil, çıktı
doğru, kod **%25 yavaş**. Merdivenin varlığı hatayı gizliyordu —
"geri çekilme" tasarlanmış bir emniyet ağıydı ve tam da bu yüzden kimse
ağın her seferinde tutulduğunu fark etmedi.

**Vektörleşmenin kaybı ayrıca sinsi:** üretici (`IRBuilder`) bağlamını
**eklendiği bloktan** alıyor. Bloklarımız küresel bağlamdaysa, döngü
vektörleştiricinin bizim bloğumuzda ürettiği her sabit yanlış bağlamdan
geliyor.

**Kural:** yeni blok/tip yaratan HER çağrı modülün bağlamından geçmeli.
`append_bb()` (llvm_backend.cpp) tek kapı; 99 çağrı oradan geçiyor.
`LLVMDoubleType()`, `LLVMInt8Type()`, `LLVMVoidType()` gibi bağlamsız
kısayolların **hiçbiri** kullanılmamalı — `backend->float_type` vb. var.

**Nasıl yakalanır:** `./build.sh suites` artık küçük bir doldurma
döngüsü derleyip (a) "aggressive O3 IR invalid" notunun **çıkmadığını**,
(b) üretilen IR'da bekçisiz depo GEP'inin (`set.pep`) **bulunduğunu**
denetliyor. Bu bir HIZ özelliğinin doğruluk paketleri arasında
sınanması: sessizce yavaşlamak da bir gerilemedir ve başka hiçbir test
bunu görmüyor.

⚠ **Denetimin ne ölçtüğüne dikkat.** İlk iki taslak da hiçbir şey
ölçmüyordu ya da yanlış şeyi ölçüyordu:

1. "İkilide SIMD komutu var mı" — statik bağlanan çalışma zamanı zaten
   **6000+** tane içeriyor. Her zaman yeşil.
2. "`main` içinde `movaps|movdqa` var mı" — yığına yapılan sıradan bir
   16 baytlık kopya da onları üretiyor; vektörleşmeyen derleyicide bile
   1 bulunuyordu. Yine her zaman yeşil.
3. "`main` içinde `paddq|movdqu` var mı" — bu gerçekten ayırt ediyor
   (eski 0, yeni 4) **ama LLVM 18'de (CI'ın sürümü) doğru kod için de
   0**: LLVM 18 bu döngüyü vektörleştirmiyor. Docker'da ölçüldü. CI'ı,
   gerçek bir gerileme olmadan kırardı.

Kalan denetim (b) sürümden bağımsız, çünkü **bizim** ürettiğimiz şeyi
soruyor: bekçisiz depo. Vektörleşme LLVM'in aşağı akıştaki kararı ve
onu şart koşmak sürüm tahmini yapmak olurdu.

Ayrıca sonda programı bilerek **yalnız** doldurma döngüsünden ibaret:
bir indirgeme eklenirse o vektörleşir ve denetim yanlış yere yeşil
kalır.

## 6s. Kıyas süresinin YARISI programın kendisi değildi

`print(1)` yazan bir Tulpar ikilisi **1,15 ms** sürüyordu; aynı işi yapan C
programı 0,41. Bu vergi HER kıyasta vardı ve kimse ölçmemişti —
`arrayiter`in 2,2 ms'sinin yarısı, `fib`in dörtte biri.

Sebep bizim `aot_runtime_init`imiz değil (o ~0,08 ms). Bileşen bileşen
ölçüldü (2026-09-06, pinlenmiş, boş programlar):

| ikili | ms |
|---|---|
| düz C | 0,41 |
| C++ (libstdc++.so dinamik) | 0,72 |
| C + OpenSSL | 0,80 |
| C++ + OpenSSL | 1,00 |
| **Tulpar** | **1,08** |

Yani süre **paylaşımlı kütüphane yüklemekten** geliyordu. `print(1)` yazan
bir ikili **13 paylaşımlı nesne** açıyordu: libssl, libcrypto ve onların
libz/brotli/zstd bağımlılıkları dahil.

**Neden:** `runtime_bindings.cpp` — HER AOT ikilisine giren nesne —
OpenSSL'e doğrudan dokunuyordu (TLS sunucu primitifleri + HTTP istemcisi).
Bağlayıcı bir arşiv üyesini yalnız ihtiyaç duyulan bir sembolü tanımladığı
için içeri alır; o nesne her zaman gerektiği için OpenSSL de her zaman
gerekiyordu.

**Çözüm iki parça ve İKİSİ BİRDEN şart:**
1. OpenSSL'e dokunan kod ayrı bir derleme birimine (`src/vm/runtime_net.cpp`).
2. Bağlantıya `-Wl,--as-needed`.

Yalnız (2) işe yaramaz: sembol kullanıldığı sürece kütüphane düşmez.
Yalnız (1) de yaramaz: `-lssl -lcrypto` bayrakları satırda durduğu için
DT_NEEDED yine yazılır.

Sonuç: 13 → 6 paylaşımlı nesne, boş program 1,15 → 0,76 ms, fib
5,08 → 4,54. İkili boyutu değişmedi.

**Kural:** çalışma zamanına dışarıdan bir kütüphaneye dokunan kod
eklerken onu ayrı bir TU'ya koy. Aynı sorun SQLite için hâlâ duruyor —
her ikili 636 KB SQLite taşıyor (boyut; hız değil, statik).

### `-static-libstdc++` denendi ve BIRAKILDI
Açılıştan 0,26 ms daha kazandırıyor (libstdc++.so da yüklenmiyor) ama:
ikili 2,1 → 3,8 MB **ve fib 4,75 → 6,35 ms**. Kod yerleşimi değişiyor;
aynı sınıfta açıklanamayan bir yerleşim etkisi elek'te de ölçüldü
(bkz. Performance.md). Net zarar.

## 6t. Kod YERLEŞİMİ ölçümü ±%27 oynatıyor — tek ikiliye bakma

`-static-libstdc++` bir kez ölçülüp **yanlış** karara bağlandı: fib
4,75 → 6,35 ms göründü, "net zarar" denip geri alındı. Gerçek sebep
bağlama biçimi değil, **fib'in adresiydi**.

Kanıt: aynı `fib_o.o`, aynı bağlama biçimi, tek fark önüne konan
dolgu nesnesinin boyutu (fonksiyonu 16'şar bayt kaydırıyor):

| dolgu | fib@ | ms |
|---|---|---|
| 0 | 0x418520 | 4,11 |
| 1 | 0x418530 | 4,30 |
| 2 | 0x418540 | 4,15 |
| 3 | 0x418550 | **5,59** |
| 4 | 0x418560 | 4,11 |
| 5 | 0x418570 | **5,41** |
| 6 | 0x418580 | 4,14 |
| 7 | 0x418590 | 4,26 |

**Aynı kod, aynı hizalama sınıfı (mod 16 / mod 32 / mod 64 hepsi eşit),
4,06 ile 5,59 arası.** Mekanizma tam belirlenemedi (dal hedefi
önbelleği / op-cache küme çakışması sınıfından; `perf` bu makinede yok),
ama etki tekrarlanabilir ve ikiliye özgü.

Eleme yöntemleri denendi ve hepsi ELENDİ:
- program adı uzunluğu (yığın hizası) — 8 farklı uzunluk, hepsi 5,3
- fonksiyon/döngü hizalaması — s_fib ile t_fib mod32 ve mod64'te AYNI,
  yine 1 ms fark
- ikili boyutu — aynı boyutlu iki ikili 1,45 ms fark ediyor

**Kural:** bir bağlama/kod-üretimi değişikliğini TEK bir ikilinin süresiyle
yargılama. Sekiz farklı yerleşimde ölç, medyanları karşılaştır. Doğru
deney tabloyu tersine çevirdi: statik medyan **4,01**, dinamik **4,20**.

Bu, [[Tuzaklar]] 6f-2'nin (tavan modelinde tek değişken) kardeşi: orada
model programı fazla değişkenliydi, burada ölçüm tek örnekliydi.

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

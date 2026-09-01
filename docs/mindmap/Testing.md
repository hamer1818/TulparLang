---
tags: [moc, testing]
---

# Testing — Koşumlar ve Disiplin

## Üç koşum
| Komut | Kapsam | CI |
|---|---|---|
| `./build.sh test` | `examples/*.tpr` uçtan uca (AOT → çalıştır → çıkış kodu) | ✅ Linux |
| `./build.sh suites` | `tests/*.test.tpr` (59 paket, gömülü `test` kütüphanesi) **+ denetimler** | ✅ Linux |
| `./tests/typeinfer/run.sh` | `tests/typeinfer/{pass,fail}/` fixture'ları | ✅ Linux |

`build.sh suites`, **`Tests:` özeti basmayan** bir süiti başarısız sayar. Gerekçe:
`test_summary()` `exit(1)` çağıran şeydir — onu unutan süit asla kırmızıya dönemez.
Dört süit tam bu durumdaydı ve **hiçbir otomasyonda koşmuyorlardı**; `assert`'in
sessiz no-op olarak aylarca yaşaması bu körlükten.

**Denetimler** paket döngüsünden SONRA koşuyor: builtin · kama mesh · dist arşiv ·
LSP · fmt · doc · pkg · Android derleme dumanı · ayrılmış kelime ve parametre adı
tanılamaları · `packages/wings_jwt` · **kod üretimi denkliği** (iki sahne).
Bunlar test değil, **çürüme dedektörü**: yayınlanan bir aracın veya önceden derlenmiş
bir arşivin, bütün testler yeşilken sessizce bozulduğu bir tur yaşandı → [[Tuzaklar]] §6b, §6c.

> ⚠️ Paket döngüsü tek bir hata bulursa `exit 1` ile duruyor ve **denetimlere hiç
> gelinmiyor**. Yani kırmızı bir paket, denetimlerin sonucunu da gizler — paketi
> düzeltmeden "denetimler temiz" deme.

## Bozma disiplini (bu projede öğrenilmiş)
**Her düzeltme, hatayı bilerek enjekte edip doğru testin kızardığı görülerek doğrulanır.**
Geçen bir test hiçbir şey kanıtlamaz — neyi yakaladığı kanıtlanmalı.

Bu disiplin bu projede defalarca **kendi zayıf testlerimi** yakaladı:
- Eğim kayması testi ters yönde de geçiyordu (entity ayak izinden çıkıyordu).
- `goto_level3d` testi `_cur_lvl3 == 0` iken çalıştığı için hiçbir şey sınamıyordu.
- Tek-atım bölge testi tek gövdeyle senaryoyu **hiç üretemiyordu** (iki gövde gerekti).
- Röntgen testi yanlış sözleşmeyi kodluyordu (kamera dünyayı terk edebilir).

**Beklenti bağımsız bir kaynaktan gelmeli.** Nişan yönü hatası (2026-08-14) tam
bu yüzden testten kaçtı: test, beklediği yönü sınadığı formülün kendisinden
türetiyordu, dolayısıyla 180°'lik hatayı onaylayıp yeşil kaldı. Düzeltilmiş test
beklentiyi `move3d`'nin ürettiği dünya hızından alıyor — kodun başka, bağımsız
bir parçasından.

Ayrıca **gereksiz kodu** da ortaya çıkarır: bir koruma sökülüp hiçbir test kızarmıyorsa
ya koruma gereksizdir ya test eksiktir — ikisi de bilinmeye değer. (Böyle silinenler:
`_ed_anim_clamp3`'in ölü dalı, float biçimlendiricinin `exp10` hesabı, ayraç
köşesindeki ikinci sınır.)

### Bütün sondalar yeşil ama kullanıcı hâlâ "olmuyor" diyorsa
Eksik olan şey bir **düzeltme değil, bir ALET** olabilir. 2026-09-01'de bir tur
tamamen buna gitti: "oyundan ses gelmiyor" raporuna karşı yazılan her penceresiz
sonda yeşildi — ve **hepsi doğruydu**. Sondalar mekanizmayı ölçüyordu; belirtinin
sebebi mekanizmada değildi.

Çıkış yolu tahmin etmeyi bırakıp **belirtiyi ayrıştıran bir alet** yapmaktı:
1. Belirtinin kaç ayrı sebebi olabileceğini **yaz** (seste altı çıktı).
2. Her sebebi **ayrı okunur** hale getir — motor artık çalarken UYGULANAN
   seviye/kaydırmayı ve çalma sayısını kaydediyor, yani "hiç çalmadı" ile
   "seviye 0 ile çaldı" artık aynı sessizlik değil.
3. Katmanları **tek tek sürülebilir** yap — `examples/scene3d_ses_testi.tpr`
   beş ses katmanını beş ayrı istasyona ayırıyor, sesleri farklı ki kulakla
   ayrılsın, ve hiçbiri tek-atım değil ki tekrarlanabilsin.
4. Kullanıcının **tek gözlemi** dalları elesin.

Sonuç: beş katman da çalışıyordu; arıza yoktu, **ölçüm penceresi** vardı
(0.14 sn'lik bir ses, girişte bir kez, parçacıkların altında). Bu sonucu hiçbir
otomatik test veremezdi — ama aleti yapmadan kullanıcı da veremezdi.
→ [[Tuzaklar]] §3b · [[Scene3D]]

### En sık KAÇAN bozma: "karar sınandı, çağrı sınanmadı"
Yardımcıyı doğrudan çağıran bir test, o yardımcının **çağrı yerinden
silinmesini göremiyor**. Çağrı çizim/döngü göbeğindeyse pencere olmadan
sürülemiyor ve bozma sessizce kaçıyor. **Beş kez** yaşandı: menü dağıtımı,
pencere tazeleme, panel bırakma kararı, kaydırma adımı, yuva içi sınır.
Çözüm hep aynı — kararı ayrı bir SAF fonksiyona çıkar, testi ONU sürsün.

Saf fonksiyon yetmediğinde (çağrının kendisi fare istiyorsa) **kaynağı okuyan**
bir test çağrının varlığını VE sırasını sınıyor — `t_div_is_wired_into_the_frame`.

Tam liste ve öteki kalıplar: [[Tuzaklar]].

### ⚠️ Harness yalan söyleyebilir
`.tpr` içinde parantez dengesi bozulursa derleme BAŞARILI döner (gömülü dize),
test hiç koşmaz, çıktı boş kalır — ve boş çıktıyı "kırılan yok" diye okuyan
harness bunu **"KAÇTI"** sanır. `Tests:` satırının VARLIĞINI denetle.

**Bozma DERLENMEMİŞ olabilir (2026-09-01).** `tulpar build` önbelleği yalnız
ana kaynağa ve sürücüye bakıyordu; `lib/*.tpr` gibi import edilen bir modülü
bozup yeniden derlemek `[AOT] Cache hit` alıp **eski ikiliyi** bırakıyordu.
Kod üretimi iğnelemesinde üç bozmanın ikisi hiç ölçülmediği hâlde "yakalandı"
göründü ve düzeltilmiş hâl bile kırmızı çıktı. Önbellek düzeltildi
(`newest_local_import_mtime`), ama **disiplin kalıcı: iğnelemeden önce çıktı
ikilisini `rm -f` et ve derlemenin gerçekten koştuğunu bir kez gözle gör.**

**İki koşum aynı ikiliyi çalıştırabiliyordu.** `tulpar dosya.tpr` derlediğini
sabit `/tmp/.tulpar_run`'a yazıyordu; eşzamanlı iki koşuda **iki farklı kaynak
aynı çıktıyı** bastı (ölçüldü). Düzeltildi — yol artık sürece özgü. Yine de
paket koşumu sırasında elle `tulpar` çalıştırmaktan kaçın: bir paketin FAIL'i
senin başka bir terminalde koşturduğun şey yüzünden olabilir; **böyle bir
FAIL'i düzeltmeye başlamadan önce paketi tek başına koştur.**

### Hızlı bozma yöntemi (derleyici derlemeden)
Test paketini **yol importuna** çevir; kopyalamaya bile gerek yok, `lib/`
doğrudan sürülüyor ve bozmayı asıl dosyaya uyguluyorsun (yedeğini alarak):
```bash
cp lib/scene3d.tpr /tmp/probe/scene3d.bak          # yedek ÖNCE
sed 's|import "scene3d"|import "lib/scene3d"|; s|import "test"|import "lib/test"|' \
    tests/scene3d_engine.test.tpr > /tmp/probe/eng.test.tpr
# lib/scene3d.tpr'ye bozmayı uygula, sonra:
./tulpar /tmp/probe/eng.test.tpr | grep -E '^  FAIL|^Tests:'
cp /tmp/probe/scene3d.bak lib/scene3d.tpr          # GERİ AL, sonra diff -q ile doğrula
```
653 testin tamamı ~55 sn — derleyiciyi yeniden derlemeye (~dakikalar) gerek yok.
Bozmaları **ifade düzeyinde** yap (`if (cond)` → `if (false)`), satır silerek
blok yapısını bozma. C tarafı (`runtime/`) bozmaları gerçek `./build.sh` ister.

## Grafik/pencere kuralı
**Asla raylib penceresi açma.** Pencere açan komutlar `DISPLAY=` altında koşar;
Android doğrulaması `adb screencap` ile (yerel pencere yok). Görsel/oynanış testini
**kullanıcı yapar** — Claude derler, paketler, kurar ve durur.

Bu kısıt motorun tasarımını belirledi: [[Scene3D]]'in **653 testi** pencere açmadan koşuyor
çünkü cihaz okuması tek yere hapsedilmiş ve karar mantığı saf fonksiyonlara ayrılmış.

## Metin genişliği penceresiz ÖLÇÜLEMEZ
`text_width()` pencere yokken **0 döner** → her yerleşim karşılaştırması
"sığıyor" der ve test sessizce boşa çıkar. Savunma: `_t_fits3(label, boxw)`
karakter bütçesi (kutu genişliğinden türüyor, ~11.5 px/karakter — ölçülmüş).
**Yeni panel eklerken metinlerini bu teste EKLE**; eklendiği her seferde eski
taşmalar çıktı. → [[Tuzaklar]]

## Sık karşılaşılan tuzaklar
- **Testler sıraya bağımlı olabilir:** `scene3d_reset()` bir ayarı sıfırlamıyorsa, onu
  değiştiren test sonrakini etkiler. Eğim sınırı ve röntgen bayrağı bu yüzden reset'e eklendi.
- **`lib/test.tpr` yalnız SON hatayı gösterir** — bir fonksiyonda birden çok assert
  düşerse ilk kırılan yer kaybolur → [[Roadmap]].
- **Koşumları paralel çalıştırma:** `--aot` hepsi CWD'ye `a.out` yazar, birbirini
  ezer. (Ayrı bir yarış olan sabit `/tmp/.tulpar_run` 2026-09-01'de düzeltildi —
  ama `a.out` sebebi duruyor.)
- **Kökteki bayat `.a` arşivleri** taze derlemeyi gölgeler; `cp build-linux/libtulpar_*.a ./`.

## İlgili
[[Tuzaklar]] · [[Build System]] · [[Scene3D]] · [[Editor]] · [[Type Inference]] · [[Roadmap]]

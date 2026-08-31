---
tags: [moc, testing]
---

# Testing — Koşumlar ve Disiplin

## Üç koşum
| Komut | Kapsam | CI |
|---|---|---|
| `./build.sh test` | `examples/*.tpr` uçtan uca (AOT → çalıştır → çıkış kodu) | ✅ Linux |
| `./build.sh suites` | `tests/*.test.tpr` (gömülü `test` kütüphanesi) | ✅ Linux |
| `./tests/typeinfer/run.sh` | `tests/typeinfer/{pass,fail}/` fixture'ları | ✅ Linux |

`build.sh suites`, **`Tests:` özeti basmayan** bir süiti başarısız sayar. Gerekçe:
`test_summary()` `exit(1)` çağıran şeydir — onu unutan süit asla kırmızıya dönemez.
Dört süit tam bu durumdaydı ve **hiçbir otomasyonda koşmuyorlardı**; `assert`'in
sessiz no-op olarak aylarca yaşaması bu körlükten.

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

### Hızlı bozma yöntemi (derleyici derlemeden)
Gömülü lib'i kopyalayıp **yol modülü** olarak import et — `./build.sh` beklemeden:
```bash
cp lib/scene3d.tpr /tmp/probe/s3probe.tpr
sed 's/import "scene3d"/import "s3probe"/' tests/scene3d_engine.test.tpr > /tmp/probe/full.test.tpr
# s3probe.tpr'ye bozmayı uygula, sonra:
cd /tmp/probe && tulpar full.test.tpr
```
C tarafı (`runtime/`) bozmaları gerçek `./build.sh` ister.

## Grafik/pencere kuralı
**Asla raylib penceresi açma.** Pencere açan komutlar `DISPLAY=` altında koşar;
Android doğrulaması `adb screencap` ile (yerel pencere yok). Görsel/oynanış testini
**kullanıcı yapar** — Claude derler, paketler, kurar ve durur.

Bu kısıt motorun tasarımını belirledi: [[Scene3D]]'in **632 testi** pencere açmadan koşuyor
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
- **Koşumları paralel çalıştırma:** hepsi CWD'ye `a.out` yazar, birbirini ezer.
- **Kökteki bayat `.a` arşivleri** taze derlemeyi gölgeler; `cp build-linux/libtulpar_*.a ./`.

## İlgili
[[Tuzaklar]] · [[Build System]] · [[Scene3D]] · [[Editor]] · [[Type Inference]] · [[Roadmap]]

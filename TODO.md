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

- [ ] **Kayıt/yükleme 3B'ye bağlı değil.** `kayit_yaz`/`kayit_oku` tame'de var
      ama `scene3d` kullanmıyor: rekor, açılan bölüm, ayarlar kalıcı değil.
      arcade'de bunların hepsi var.

- [ ] **Tetikleyici bölge yok.** "Buraya girince şu olsun" — şu an
      `solid3d(false)` + çarpışma kancasıyla taklit ediliyor. Ucuz ve
      her oyunda lazım.

- [ ] **Gamepad `scene3d`'de okunmuyor.** tame'de binding var
      (`gamepad_down`, `gamepad_pressed`), 3B giriş katmanı yalnız klavye +
      dokunmatik okuyor.

- [ ] **Animasyon geçişi/harmanlama yok.** `anim3d` boşta↔koşu arasında sert
      geçiyor.

---

## 2 — Görsel

- [ ] **Arazi tek renk.** Yükseklik + eğime göre katman boyama (çim/kaya/kar).
      **Maliyetin yarısı ödendi:** eğim sınırı için yazılan yüzey normali
      (`_terrain_normal3`) zaten gereken veriyi veriyor. Görüntüyü en çok
      değiştirecek tek iş.

- [ ] **Su yüzeyi / gündüz-gece döngüsü / gökyüzü dokusu.** Gökyüzü şu an
      yalnız degrade.

- [ ] **Parçacıklarda dönme ve doku atlası yok** — tek boy düz billboard.

---

## 3 — Dil / derleyici borcu

Bunlar 2026-08-06 oturumunda motorun içinde çalışırken **bizzat çarpılan**
boşluklar; backlog'da yoklardı.

- [ ] **Builtin tablosu ↔ codegen ayrışması (İKİ YÖNLÜ).** Ölçüldü, ikisi de
      gerçek:
      - **Tabloda var, codegen'de yok:** `clock()` typeinfer builtin tablosunda
        kayıtlı ama çağırınca "fonksiyon bulunamadı" veriyor. Kullanıcıya
        çarpan kırık vaat.
      - **Codegen'de var, tabloda yok:** `acos`, `atan2` çalışıyor ama typeinfer
        tablosunda yoklar → argümanları/dönüşü denetlenmiyor, dönüş tipi VOID
        çıkıyor (yani sonraki denetimler sessizce atlanıyor) ve LSP'de
        tamamlama/hover yok. `chase3d` `atan2`yi zaten kullanıyor.

      **`assert` hatasıyla aynı sınıf**: tablo gerçekliği yansıtmıyor. İki
      listeyi karşılaştıran bir tarama tüm aileyi kapatır — ucuz ve doğrudan
      güven altyapısı. Bulunanlar 5 noktalık bağlamanın eksik ayaklarına eklenir
      (typeinfer + LSP).

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

- [ ] **Küre ↔ DÖNÜK kutu yaklaşık.** `_sph_box3` kutuyu eksen-hizalı
      varsayıyor; kutu-kutu çifti tam SAT'tan geçiyor, küre-kutu geçmiyor.

---

## 4 — Altyapı ve belge

- [ ] **Çarpışma hâlâ O(n²).** Geniş faz sabiti 14× küçülttü (200 entity
      15.4 → 1.12 ms), ama asimptot duruyor. Uniform grid artık ucuz: düz
      konum/yarıçap dizileri grid'in zaten isteyeceği zemin. ~800 entity
      üstüne çıkılmadıkça getirisi yok.

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

# Editör Tasarım Dili (L7)

> Bu belge editörün GÖRSEL SÖZLEŞMESİDİR. Renk, ölçü ve bileşen davranışı
> buradan türer; koda ham değer yazılmaz. `app/editor_ui.cpp` bu belgenin
> uygulamasıdır, tersi değil.

Tarih: 2026-09-18 · Dal: `engine/editor-ue5-ui`

---

## 1. Neden kendi dilimiz — iki ayrı sebep

**Birincisi hukuki.** Bir editörün *bilgi mimarisi* (sol tarafta yerleştirme,
ortada görünüm, sağda özellikler, altta içerik tarayıcı) işlevseldir ve
sektörde ortaktır; kimsenin tekelinde değil. Ama **ikon çizimleri, birebir
renk değerleri ve genel görünüm-izlenim** öyle değil. Bu yüzden düzen
yakınsayabilir, **yüzey asla kopyalanmaz**.

**İkincisi kimlik.** UE5, Unity ve Godot'un üçü de aynı yerde duruyor: soğuk
mavi-gri nötrler + mavi vurgu. Onlardan birine benzemek, hem kopya riskine
yaklaşmak hem de kendi kimliğini silmek demek. Tulpar'ın zaten bir rengi
vardı (turkuaz) ve o doğru fikirdi — kusuru renginde değil, **doygunluğunda
ve her yerde kullanılmasındaydı**.

**Bu belgenin seçimi: sıcak kömür nötrler + tek bir turkuaz vurgu.**
Sıcak nötr (R > G > B, bir-iki kademe) üç büyük editörün hiçbirinde yok;
profesyonel ses/video araçlarının alanı. 3B görüntüdeki soğuk gökyüzü ve
sıcak güneş tonlarıyla da çakışmaz.

---

## 2. Yüzey ölçeği — altı kademe

Arayüzün derinliği renkle değil **yüzey sırasıyla** kurulur. Kural tek
cümle: **girdi çöker, düğme kabarır, panel arada durur.**

| jeton | değer | nerede |
|---|---|---|
| `void` | `#0A0908` | dok boşluğu, panel arası, güçlü ayraç |
| `sunken` | `#100F0E` | girdi kuyusu, liste zemini, arama alanı |
| `panel` | `#1A1918` | panel gövdesi (ana zemin) |
| `raised` | `#232220` | başlık bandı, sekme şeridi, tablo başlığı |
| `control` | `#302E2B` | düğme, birleşik denetim |
| `control_hi` | `#3C3936` | üzerine gelince |

Ölçü: her kademe bir öncekinden ~%35 daha açık. Bitişik iki kademe
ayırt edilebilir ama gürültü yapmaz.

**Kenarlık kullanılmaz.** Ayrım yüzey tonuyla yapılır. Hem ton hem kenarlık
olunca her kutunun etrafında ikinci bir kontur oluşur ve arayüz "çizgili"
okunur. Tek istisna: pencere dış kenarı ve tablo ızgarası.

---

## 3. Renk

### Vurgu — tek renk, seyrek kullanım

| jeton | değer | kullanım |
|---|---|---|
| `accent` | `#2F9B8F` | seçili sekmenin üst çizgisi, odak halkası, etkin kip |
| `accent_hi` | `#4FC4B6` | imleç, onay işareti, bağlantı |
| `accent_lo` | `#1E645C` | basılı tutamak |
| `select` | `#24403D` | liste/ağaç seçim şeridi (doygun DEĞİL) |

**Kural:** vurgu bir karede ekranın **%5'inden azını** kaplamalı. Kaydırma
tutamağı, slider oluğu, ayraç ve pasif çerçeve nötr kalır. Vurgu "burada
bir şey var" demek içindir; her yerdeyse hiçbir şey demiyordur.

### Anlamsal

| jeton | değer | anlam |
|---|---|---|
| `warn` | `#D9A441` | kaydedilmemiş, sürükleme hedefi, dikkat |
| `error` | `#D2544B` | hata, geçersiz |
| `ok` | `#5FA85E` | yüklendi, geçti |

### Eksen

| | değer |
|---|---|
| X | `#C0534C` |
| Y | `#6B9E45` |
| Z | `#4E79BE` |

Doygunluk bilerek düşük: eksen rengi bir **kenar işareti**dir, yüzey değil.
Üç doygun blok yan yana gelince özellik paneli oyuncak okunur.

### Metin

| jeton | değer | kullanım |
|---|---|---|
| `text` | `#D8D5D0` | ana metin (hafif sıcak beyaz) |
| `text_dim` | `#8C8882` | etiket, ikincil bilgi |
| `text_mute` | `#5E5B57` | pasif, ipucu, yer tutucu |

---

## 4. Tipografi — üç kademe, tek font

Vendored ImGui 1.92 dinamik boyut destekliyor (`PushFont(NULL, px)`), yani
ikinci bir TTF yüklemeden ölçek kurulabilir.

| jeton | boyut (ölçek 1) | kullanım |
|---|---|---|
| `text_lg` | 15 px | panel başlığı, seçili nesne adı |
| `text_md` | 13 px | **taban** — etiket, değer, menü, düğme |
| `text_sm` | 11 px | durum çubuğu, sütun başlığı, üst veri, rozet |

**Ağırlık yok, onun yerine üç araç:** boyut, renk kademesi ve
**BÜYÜK HARF + harf aralığı**. Bölüm başlıkları (`DÖNÜŞÜM`, `BİLEŞENLER`)
`text_sm` + `text_dim` + büyük harf olarak yazılır. Bu, ikinci bir font
yüzü yüklemeden gerçek bir hiyerarşi verir.

---

## 5. Boşluk — 4 piksellik ızgara

Tüm ölçüler 4'ün katı (ya da yarısı). Ölçek 1'de:

| jeton | değer | nerede |
|---|---|---|
| `pad_frame` | `6 × 3` | kutu/düğme iç dolgusu |
| `pad_item` | `6 × 3` | öğeler arası |
| `pad_inner` | `4 × 2` | bileşik denetimin parçaları arası |
| `pad_window` | `8 × 6` | panel iç kenarı |
| `pad_cell` | `6 × 3` | tablo hücresi |
| `indent` | `14` | ağaç girintisi |
| `row_h` | `22` | liste/özellik satırı yüksekliği |

Önceki değerler `8 × 4` / `8 × 5` idi — profesyonel araçlara göre **gevşek**.
Sıkılaştırma tek başına ekrana ~%15 daha fazla bilgi sığdırır ve arayüzü
"uygulama" gibi gösterir.

**Yuvarlaklık:** `2` (pencere `0`). Geniş yarıçap widget seti gibi okunur.

---

## 6. Bileşen reçeteleri

### Sayısal alan (vec3 bileşeni)
```
[▌X  0.000 ]
 ^ 3-4 px eksen çubuğu, alanın sol kenarında, tam yükseklik
   zemin: sunken · harf: eksen rengi · değer: text
```
Eksen rengi **yalnız** çubukta ve harfte. Alanın zemini diğer tüm
girdilerle aynı (`sunken`) — satır tek parça okunur.

### Bölüm başlığı (özellik paneli)
```
 DÖNÜŞÜM                                    ← text_sm, text_dim, BÜYÜK
 ─────────────────────────────────────      ← void, 1 px
```
Bant değil çizgi: bant her bölümü kutulaştırıp paneli parçalıyor.

### Bileşen başlığı (katlanabilir)
```
▾ ◆ Gövde                              ☐  ✕
   ^ raised bandı, row_h + 2, ikon accent_hi
```

### Liste satırı (hiyerarşi / içerik)
- Zemin: şeffaf; alternatif satır `#FFFFFF` %2
- Üzerine: `raised`
- Seçili: `select` + solda 2 px `accent` şerit
- **Doygun dolgu yok** — uzun listede göz yorar

### Düğme
- Normal `control`, üzerine `control_hi`, basılı `raised` (içeri çöker)
- Etkin/açık kip: zemin `select`, metin `accent_hi`

### Araç çubuğu
Gruplar `void` renginde 1 px dikey çizgiyle ayrılır, aralarında
`pad_item.x × 2` boşluk. Birleşik denetimler (Taşı/Döndür/Ölçekle) tek
kapsül; aralarında boşluk yok.

---

## 7. İkon dili

**Hiçbir ikon başka bir editörden kopyalanmaz.** İki kaynak serbest:

1. **Unicode geometrik glifler** (bugün kullanılan yol): ◆ ☀ ◼ ✕ ▾.
   Telifsiz, font zaten taşıyor, ek bağımlılık yok.
2. **Kendi SDF atlasımız** — motorun `content/font.hpp` SDF yolu zaten var.
   İkonlar 16×16 ızgarada, 1.5 px çizgi kalınlığında, **dolu değil kontur**
   çizilir; kontur dili glif diliyle tutarlıdır.

Kural: bir ekranda ikon dili **tek** olmalı. Yarısı dolu yarısı kontur
ikon, en çok göze batan amatörlük işaretlerinden biridir.

---

## 8. Geri bildirim

- Üzerine gelme: yüzey bir kademe yukarı, **anında** (geçiş yok)
- Basma: bir kademe aşağı
- Odak: 1 px `accent` halka, yalnız klavye odağında (fare tıklamasında değil)
- Sürükleme hedefi: `warn` kontur + `warn` %15 dolgu

Animasyon yok. Editörde geçiş efekti gecikme hissi yaratır; profesyonel
araçlar anında tepki verir.

---

## 9. Kapılar

Bu belgenin uygulandığı nasıl ölçülür (piksel testleri, `editor_probe_render`):

| kapı | ne doğrular |
|---|---|
| `editor_theme_surface_order` | `void < sunken < panel < raised < control` sıralaması gerçekten artan parlaklıkta |
| `editor_theme_input_is_sunken` | girdi zemini panel zemininden KOYU |
| `editor_theme_accent_is_rare` | tipik bir karede vurgu tonundaki piksel oranı %5'in altında |
| `editor_widgets_vec3_badges_carry_axis_tones` | eksen çubuğu doğru tonda (mevcut) |
| `editor_type_scale_has_three_steps` | sm < md < lg, her biri en az 2 px ayrık |

---

## 10. Kapsam dışı (bilinçli)

- **Açık tema.** Editör karanlık; 3B görüntünün yanında açık arayüz
  göz uyumunu bozar. İkinci bir tema iki kat bakım demek.
- **Kullanıcı teması.** Renk jetonları koda gömülü; dosyadan okumak ayrı
  bir iş ve bugün kimse istemiyor.
- **Animasyonlu geçişler.** Bkz. §8.

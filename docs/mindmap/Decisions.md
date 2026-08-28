---
tags: [moc, adr, decisions]
---

# Decisions (ADR) — Mimari Kararlar

## AOT-only — VM yok (2026-06-15)
Tulpar C/Rust/Go modeli: **tek AOT/LLVM yürütme yolu**. Bytecode VM interpreter + REPL **kaldırıldı, geri getirme**. `tulpar foo.tpr` AOT derler+çalıştırır, fallback yok (AOT hatası = hard error). `--vm`/`--run` yok sayılır (uyarı); `--repl`/`-i` kaldırma notu basıp çıkar. Yeni özellik yalnız AOT yolunda. Kaldırılan: `compiler.cpp`, `vm_run`, `run_repl`. → [[Runtime]] · [[Architecture]]

## Varsayılan port 8484
`serve()` portsuz → **8484**. Hikaye: ASCII 'T' = 84 = binary `01010100` ("tulpar"ın T'si). Doluysa otomatik +1; açık port doluysa kullanıcı bilgilendirilir. Reklam/marka değeri. → [[Wings]]

## Arena bellek modeli + checkpoint disiplini
GC yok; arena + per-request region + write barrier. `arena_restore` checkpoint korur, **`arena_drop` serbest bırakır** — kalıcı thread'lerde drop şart. → [[Memory Model]] · [[Memory Leak Fixes]]

## DB varsayılanları (2026-06-18)
`db_open` → WAL + busy_timeout varsayılan (write 2.3×). → [[SQLite and DB]]

## Dil tuzakları (C-benzeri DEĞİL)
- **`%` operatörü yok** → `mod()`/`fmod()`.
- **`/` herhangi bir operand float ise float bölme** yapar (declared `int` bile coerce etmez) → `toInt()` ile gerçek int bölme.
→ [[Wings Access Log]]

## Editör ASIL uygulama, oyun ÖNİZLEME (2026-08-27)
`./TameEngine` bir editör; OYNAT sahnenin KOPYASINI koşturur, DUR kopyayı
atar. Bunun sonucu: oyun-bitti ekranında "Çıkış" **programı kapatmaz**,
düzenlemeye döner (`_s3_quit_or_stop3`). Düğmenin etiketi de editörde
"Duzenle" olur — yalan söylemesin. → [[Editor]]

## Panel dock modeli — üç yuva, docking DEĞİL (2026-08-28)
Üç panel × dört yuva (sol/sağ/alt/kapalı). Panel başlığından sürüklenip
bırakılıyor, yuvanın sınırı çekiliyor, kapatılıyor; yerleşim kalıcı.
**Yapılmadı ve bilerek:** yuva içinde sıralama, SEKME, YÜZEN pencere — üçü de
ayrı bir pencere yöneticisi demek. → [[Editor]]

## Menü şeridi AÇILIR MENÜ, düz düğme sırası değil (2026-08-28)
Şerit düz bir düğme sırasıydı ve dolmuştu: sığmayan düğme hiç çizilmiyordu,
yani komut dar pencerede **erişilemez** oluyordu. Kısayollar da hiçbir yerde
yazmıyordu. Dört başlık (Dosya/Duzen/Gorunum/Yardim) + açılır listeler; başlık
sayısı sabit, komut eklemek şeridi büyütmüyor.
- Öğeler **konumla değil KODLA** anılıyor (`MNA_*`) — duruma göre gizlenen bir
  öğe ötekilerin ne yaptığını kaydırmasın diye.
- **Soluk ≠ gizli:** geçici olarak iş görmeyen komut duruyor ve soluk
  çiziliyor; var oluşu duruma bağlı olan (web'in "Indir"i) hiç konmuyor.
- **KURTAR menüde değil**, şeritte: duran bir komut değil geçici bir teklif.
- Menüde yazan kısayolun bağlı olduğunu bir test **tuş işleyicisinin
  kaynağından** doğruluyor. → [[Editor]]

## Yuva içi paylaşım PAY (oran), piksel değil (2026-08-28)
Aynı yuvadaki paneller yuvayı sabit eşit bölüşüyordu ve aralarındaki sınır
çekilemiyordu. Artık her panelin bir **payı** var (tam sayı, varsayılan 1000).
- **Oran, piksel değil:** pencere büyüyünce paneller birlikte büyüyor.
- **Tam sayı,** çünkü kayıt biçimi metin ve `toString(1.0)` = `"1e+00"` —
  ondalık pay her kaydet/aç turunda okunamaz hâle gelirdi.
- **Sınır yalnız ÇİFTİ etkiliyor;** üçüncü panel yerinde kalıyor.
- **Yuvaya yeni gelen panel ORTALAMA pay alıyor** (`_dk_set_slot3` içinde,
  tek çoktan geçilen nokta): kendi payını taşısaydı bir yuvada kıymık olan
  panel yeni yuvasına da kıymık düşerdi. → [[Editor]]

## Kaydedilmemiş iş: soramıyorsak KAYBETMEYELİM (2026-08-27)
raylib'de kapanışı iptal edecek bağlama yok → "emin misin?" **sorulamıyor**.
Onun yerine çıkarken `<dosya>.kurtarma.json` yazılıyor ve açılışta KURTAR
düğmesi çıkıyor. Sessizce geri yüklemek yanlış olurdu (kullanıcı kaydetmemeyi
bilerek seçmiş olabilir); kurtarma geri alınabilir ve kurtarılan iş hâlâ
"kaydedilmemiş" sayılıyor. → [[Editor]]

## Bölge eylemleri kural eylemlerinden AYRI (2026-08-27)
`ZACT_*` ≠ `ACT_*`. Kural eylemleri bir ÇİFT üzerinden konuşuyor ("ötekini
öldür"); bölgede öteki yok, giren gövde var. Ortak liste kullanmak yarısı
sessiz boş işlem olan bir menü üretirdi. → [[Scene3D]] · [[Editor]]

## Pencere boyutlandırma OPT-IN (2026-08-27)
`window_resizable(on)` **`window()`'dan ÖNCE** çağrılmalı (raylib sözleşmesi).
Varsayılan KAPALI: sabit pencere çoğu oyunun istediği şey (arcade mantıksal
bir çözünürlük varsayıyor). scene3d açıyor. → [[Tame]] · [[Editor]]

## Sunset (geri getirme)
Tree-walk interpreter (`src/interpreter/`) + x64 JIT (`src/jit/`) — 2026-05-05 kaldırıldı.

## İlgili
[[Architecture]] · [[Memory Model]] · [[Editor]] · [[Tuzaklar]] · [[Roadmap]]

# Faz 8 — Tulpar Shader Aşaması: tasarım ve fizibilite ölçümü

> Durum: **tasarım + çalışan fizibilite dilimi.** Faz 8 hâlâ kritik yolda değil (PLAN §7, §11).
> Bu belge tahmin değil ölçüm: bütün sayılar `src/` ve `engine/rhi/shaders/` okunarak, prototip
> koşturularak çıkarıldı. Taban: `b6f593e`.
>
> Prototip: [`engine/tools/tpr_shader.py`](../../engine/tools/tpr_shader.py) ·
> Örnekler: `engine/tests/assets/shader/*.tprs` ·
> Kapı: [`tests/faz8_shader_audit.py`](../../tests/faz8_shader_audit.py)

---

## 0. Özet

**Ölçülen üç sonuç:**

1. **Motorun 21 GLSL shader'ının 19'u**, Tulpar sözdizimindeki bir alt kümeden üretilip `glslc` ile
   derlendiğinde depodaki `*_spv.h` dizileriyle **bayt bayt aynı** SPIR-V veriyor. Dışarıda kalan iki
   tanesi `triangle.vert` (dizi kurucu literali) ve `cull.comp` (compute aşaması).
2. Bu 19 dosyanın **11'i bugünkü `./tulpar` ayrıştırıcısından parse hatası olmadan geçiyor.** Kalan 8'i
   **iki** gramer boşluğunda düşüyor: sabit boy dizi (`mat4[3]`) ve bit işlemleri (`& | ^ << >>`).
   Yani bugünkü engel dilin *gramerinde* değil, neredeyse tamamen **tip sisteminde**.
3. Planın Faz 8 kapısının istediği iki şey — **fp16** ve **wave genişliği (spec sabiti + subgroup)** —
   `glslc` üzerinden zaten derleniyor (ölçüldü). Yani o kapıyı geçmek için Slang şart değil.

**Karar (§4):** backend olarak **Slang değil, GLSL üretimi + `glslc`.** Gerekçe kısaca: motorun tek
hedefi SPIR-V (macOS'ta MoltenVK, Metal backend'i yok), `slangc` ne depoda ne de dağıtımlarda var
(Arch'taki `slang` paketi S-Lang yorumlayıcısı, shader Slang değil — doğrulandı), ve GLSL yolu zaten
kanıtlanmış bir boru hattına takılıyor.

---

## 1. Bugün ne var — kaynaktan

### 1.1 Shader tarafı

Shader'lar GLSL 4.50, `engine/tools/compile_shaders.py` onları `glslc -O --target-env=vulkan1.1` ile
derliyor ve `*_spv.h` dizilerini **depoya yazıyor**; CI'da `glslc` gerekmiyor. Bu belge için önemli bir
özelliği ölçüldü:

> `glslc -O` çıktısı **isim bağımsız ve yeniden üretilebilir**. Aynı shader'ın yerel değişken adları
> değiştirilip boşlukları bozulduğunda üretilen SPIR-V baytları birebir aynı kalıyor
> (`bloom_bright.frag`: 1640 bayt, sha256 aynı) ve depodaki `bloom_bright_frag_spv.h` ile de aynı.

Bu, Faz 8 için elimizdeki **en sert kapıyı** mümkün kılıyor: bir çevirici "aynı işi yapıyor" iddiasını
byte-eşitlikle kanıtlayabilir, benzerlik metriğine gerek kalmadan.

### 1.2 Dil tarafı

| Nerede | Ne söylüyor |
|---|---|
| `src/parser/ast_nodes.hpp` (`enum DataType`) | 15 tip: `INT, FLOAT, STRING, BOOL, CUSTOM, ARRAY, ARRAY_*, JSON, VOID, UNSPECIFIED`. **Vektör/matris yok.** |
| `src/aot/llvm_backend.cpp:2420` | `backend->float_type = LLVMDoubleTypeInContext(...)` → Tulpar `float` = **f64**. `int` = i64. **f32 yok, fp16 yok, işaretsiz tip yok.** |
| `src/lexer/lexer.cpp:572–610` | `&` ve `|` yalnız `&&` / `\|\|` (ve `match` için `TOKEN_PIPE`) olarak tanınıyor. Tek başına `&` → `Lexer Error: Unknown character '&'`. **Bit işlemleri sözcük düzeyinde yok.** |
| `src/parser/parser.cpp:1608–1640` (`parse_type`) | `[]` soneki yalnız **boş** biçimde; `T[3]` ayrışmaz. Tanınmayan tanımlayıcı → `TYPE_CUSTOM`. |
| `src/parser/parser.cpp:609–634` (`parse_type_decl`) | Struct alanı katı `<tip> <ad>;`. `field_custom_types` **her zaman `nullopt`** yazılıyor, `field_defaults` her zaman `nullptr`. Yerleşim/hizalama/boyut bilgisi yok. |
| `src/typeinfer/typeinfer.hpp` (`StructTypeInfo`) | Yalnız alan adı + `DataType`. `std140`/`std430` doğrulaması için gereken hiçbir şey yok. |
| PLAN §11 | Struct oluşturma her yerde `vm_allocate_object` ile **heap'te kutulu**; kutusuz struct, işaretçi, atomik yok. |

---

## 2. GPU için gereken ama OLMAYAN her şey

Her satır: **bugün ne var** / **ne gerekiyor** / prototipin o boşlukla ne yaptığı.

| # | Özellik | Bugün ne var | Ne gerekiyor | Prototipte |
|---|---|---|---|---|
| **T1** | `f32` (tek duyarlık) | `float` = **f64** (`LLVMDoubleType`) | Ayrı `f32` tipi; GPU tarafında varsayılan o. f64 GPU'da ya yok ya 1/16 hız | `.tprs` içinde `float` doğrudan GLSL `float`'a (f32) eşleniyor — **anlam kayması**, çeviricinin bildiği ama Tulpar'ın bilmediği bir kural |
| **T2** | `f16` / precision analizi | Yok | Planın Faz 8 kapısının ana maddesi. `glslc` `GL_EXT_shader_explicit_arithmetic_types_float16` ile destekliyor (ölçüldü: 808 bayt, `OpCapability Float16`) | Kapsam dışı |
| **T3** | İşaretsiz tamsayı (`uint`) | Yok (tek `int`, i64) | Maske, indeks, `findLSB`, atomik sayaç | `uint` tipi ve `1u` literali **çeviricide** var; Tulpar `uint`'i sıradan tanımlayıcı sanıyor (tip değil) |
| **T4** | Vektör tipleri (`vec2/3/4`, `ivec`, `uvec`) | Yok | 21/21 shader kullanıyor | `.tprs`'te `vec3 n = ...` **ayrışıyor** (`TYPE_CUSTOM`), anlamı yok |
| **T5** | Matris tipleri (`mat3`, `mat4`) | Yok | 10/21 shader | Aynı — ayrışır, anlamsız |
| **T6** | Swizzle (`.xyz`, `.rgb`, `.xy`) | Üye erişimi olarak **ayrışıyor** (`parse_postfix` → ArrayAccess), anlamı alan araması | Bileşen seçimi + yeniden sıralama, lvalue olarak da (`c.rgb = ...`) | Çevirici swizzle'ı olduğu gibi geçiriyor; doğrulama `glslc`'de |
| **T7** | Kutusuz struct, açık yerleşim | Struct var ama **heap'te kutulu** (PLAN §11) | `std140`/`std430` ile bit uyumlu yerleşim; CPU-GPU layout doğrulaması bunun üstüne kurulur | Blok gövdeleri `struct` olarak yazılıyor, yerleşimi `glslc` hesaplıyor — **doğrulama yok** |
| **T8** | Sabit boy dizi (`T[N]`) — **G3** | `parse_type` yalnız `T[]`; `mat4 x[3];` ve `mat4[3] x;` ikisi de parse hatası | Uniform blokta kademe matrisi, ışık dizisi | Çevirici `T[N]` önek yazımını destekliyor; **Tulpar'dan geçmiyor** → 7 shader |
| **T9** | Dizi kurucu literali (`vec2[](…)`) | Yok | `triangle.vert`'in sabit köşe tablosu | Kapsam dışı → kapsanmayan 1 shader |
| **T10** | Bit işlemleri `& \| ^ ~ << >>` — **G4** | **Lexer'da yok** | Küme maskesi, `gl_VertexIndex` hilesi, prefix sum | Çeviricide tam (öncelik merdiveni GLSL/C sırasında); **Tulpar'dan geçmiyor** → 3 shader |
| **T11** | `findLSB` / `bitCount` | Yok | `mesh.frag` ışık döngüsü | Çeviricide yerleşik listesinde |
| **T12** | Opak tipler (`sampler2D`, `sampler2DShadow`) + dokusal örnekleme | Yok | 7/21 shader | `ornek()` işareti; `texture()` yerleşik |
| **T13** | Bağlama nitelikleri (`set`, `binding`, `location`, `push_constant`, `std430`) | Yok — Tulpar'da hiç **nitelik (attribute) sözdizimi yok** | Her shader | **İşaret çağrısı** ile: `sampler2D u = ornek(0,0);` — sıradan Tulpar değişken bildirimi |
| **T14** | Giriş/çıkış aşama değişkeni, interpolasyon (`flat`) | Yok | 21/21 | `girdi()/cikti()/girdi_duz()/cikti_duz()` |
| **T15** | Spesifikasyon sabiti (`constant_id`) | Yok | `ui_sdf.frag`; wave genişliği için de gerekecek | `ozel(id, varsayilan)` |
| **T16** | `const` niteleyici | **Anahtar kelime yok** (`const float d = …` parse hatası) | Yerel sabitler, dizi literalleri | Çevirici `const`'u düşürüyor; SPIR-V değişmiyor (ölçüldü: `ui_sdf.frag` bayt aynı) |
| **T17** | `discard` | Tanımlayıcı olarak **ayrışıyor**, anlamı yok | Parça atma | Çeviricide deyim; `frag` dışında hata |
| **T18** | Türev fonksiyonları (`fwidth`, `dFdx`) | Yok | `ui_sdf.frag` | Yerleşik listesinde |
| **T19** | Skaler dönüşüm `float(x)` / `int(x)` — **G1** | **Ayrışmıyor**: `float`/`int` anahtar kelime, çağrılabilir ad değil | Her yerde | Alt küme `f32(x)/i32(x)/u32(x)` yazıyor — ad çakışması olmadığı için Tulpar'dan geçiyor |
| **T20** | Compute: `local_size`, `shared`, `barrier()`, `gl_WorkGroupID` | Yok | `cull.comp` | Kapsam dışı → kapsanmayan 1 shader |
| **T21** | `out` / `inout` parametre | Yok (yalnız değer geçişi) | Yeni `mesh.frag` PBR yolu kullanıyor | Kapsam dışı |
| **T22** | Derleme zamanı reflection | Yok (PLAN §11) | Permutation üretimi, uniform packing, CPU-GPU layout doğrulaması — Faz 8'in **asıl değer önerisi** | Kapsam dışı |

**Özet:** GPU tarafı için eksik olan şeyler iki kümeye ayrılıyor ve ikisi çok farklı büyüklükte.

- **Gramer boşluğu — küçük.** Yalnızca dört madde: G1 (`float(x)` çağrısı), G3 (`T[N]`), G4 (bit
  işlemleri), G5 (`const`). Hepsi lexer/parser'da onlarca satırlık iş. G3 + G4 tek başına 8 shader'ı
  kapatıyor.
- **Tip sistemi boşluğu — büyük.** T1, T4, T5, T6, T7 (f32, vektör, matris, swizzle, kutusuz struct)
  Tulpar'ın değer temsilini (`VMValue`: etiket + `int64_t`/`double`/`Obj*`) **doğrudan ilgilendiriyor**
  ve PLAN §11'in "sistem alt kümesi" maddesiyle aynı iş. Bu, Faz 8'in gerçek önkoşulu.

---

## 3. 21 shader'ın dayattığı ASGARİ özellik kümesi

`engine/rhi/shaders/*.{vert,frag,comp}` üzerinde ölçüldü (yorum satırları elendi). "Bu 21 shader bu
özellik olmadan yazılamaz" listesi — uydurma değil sayım:

| Özellik | Kaç shader | Not |
|---|---|---|
| `vec2/3/4` ve vektör kurucusu | **21 / 21** | İstisnasız. Alt kümenin tabanı |
| Kullanıcı fonksiyonu (parametre + dönüş) | **21 / 21** | `oct_decode`, `linear_to_srgb`, `tent`, `cascade_sample` … |
| Uniform blok | 15 | `Frame`, `Motion`, `Push`, `Lights` |
| `push_constant` | 13 | Çizim başına veri |
| Swizzle | 12 | `.rgb`, `.xyz`, `.xy`, `.w` |
| Dizi indeksleme `a[i]` | 11 | Kademe, eklem, ışık, görünürlük listesi |
| `mat3/mat4` ve `mat*vec` | 10 | Tüm vertex yolu |
| `gl_Position` | 10 | Her vertex shader |
| **Blok içinde sabit boy dizi** | **9** | `mat4 light_viewproj[3]`, `PointLight l[32]` |
| `sampler2D` + `texture()` | 7 | Post-process + UI + malzeme |
| SSBO (`buffer`) | 7 | Skin, çizim listesi, küme maskesi |
| `struct` (blok dışı) | 5 | `DrawItem`, `PointLight`, `Inst`, `Batch` |
| `if/else` | 5 | |
| `flat` interpolasyon | 5 | |
| Üçlü işleç `?:` | 5 | |
| Skaler dönüşüm `float()/int()/uint()` | 6 | |
| `uint` | 4 | |
| `gl_VertexIndex` / `gl_InstanceIndex` | 4 | |
| **Bit işlemleri** | **3** | `cull.comp`, `mesh.frag`, `post.vert` |
| `for` / `while` | 2 / 1 | |
| `sampler2DShadow` | 1 | `mesh.frag` |
| `findLSB` | 1 | `mesh.frag` |
| `fwidth` | 1 | `ui_sdf.frag` |
| `constant_id` | 1 | `ui_sdf.frag` |
| `const` | 2 | `triangle.vert`, `ui_sdf.frag` |
| compute (`shared`/`barrier`/`local_size`) | 1 | `cull.comp` |

**Alt sınır okuması:** ilk 14 satır (21 shader'ın hepsini ya da çoğunu kapsayanlar) Faz 8'in **Dilim 1**
hedefi. `findLSB`, `fwidth`, `constant_id` tek shader'lık kuyruk ama ucuz. Compute kendi başına bir
dilim (`shared`, `barrier`, iş grubu kimlikleri, bellek sıralaması) ve PLAN §8/10 mobilde compute'u
zaten sınırlandırıyor — **en son gelir.**

---

## 4. Backend kararı

### Seçenek A — Slang'i backend yap (PLAN EK B.1'in önerisi)

Tulpar → Slang IR / Slang kaynağı → SPIR-V + MSL + WGSL.

- **Artısı:** çok hedef bedava; capability sistemi; Valve'in Source 2 ile kanıtı güçlü.
- **Eksisi (ölçüldü):**
  - `slangc` bu makinede **yok**; Arch deposundaki `slang 2.3.3-4.1` paketi **S-Lang yorumlayıcısı**
    (`pacman -Qs slang` → "S-Lang is a powerful interpreted language"), shader Slang değil. Yani
    vendor etmek ya da ayrı ikili dağıtmak gerekir.
  - Motorun **tek çıktı biçimi SPIR-V**. macOS'ta MoltenVK kullanılıyor (CI: lavapipe / MoltenVK);
    ayrı bir Metal backend'i yok, web hedefi PLAN'da 2026-09-13'te düşürüldü. Slang'in asıl kazancı
    olan çok-hedef çıktısının **bugün alıcısı yok.**
  - Slang'e girdi vermek için ya Slang IR'ı (C++ API, ayrı bir bağımlılık ve ABI) ya da Slang kaynağı
    (yani yine "metin üret") üretmek gerekir — ikinci durumda GLSL yerine Slang üretmiş oluruz ve
    kazanç yalnız çok-hedefte.
  - `*_spv.h` "CI'da derleyici gerekmez" sözleşmesi Slang'le de sürer ama zincire ikinci bir dış araç
    girer.

### Seçenek B — Doğrudan SPIR-V üret

- **Artısı:** tam denetim; fp16/precision kararlarını kendimiz veririz; dış araç yok.
- **Eksisi:** SSA + tip tablosu + decoration + yerleşim kuralları (`std140`/`std430`) + geçerlilik
  bizde. `spirv-val`'i geçen çıktıyı üretmek, üstüne optimizasyon yapmadan, **başlı başına aylık bir
  iş** ve Faz 8'in gerçek değeri olan reflection/permutation/precision işine hiç dokunmuyor. Klasik
  "backend külfetini al, moat'ı bırak" hatası.

### Seçenek C — GLSL üret, `glslc`'ye ver ✅ **KARAR**

- **Artısı:**
  - Var olan boru hattına **takılıyor**: aynı `glslc -O --target-env=vulkan1.1`, aynı `*_spv.h`
    üretimi, CI'da hâlâ derleyici gerekmiyor.
  - Doğrulama bedava ve **sert**: üretilen SPIR-V, elle yazılmış shader'ın SPIR-V'siyle bayt
    karşılaştırılabiliyor (§1.1). Bu prototipte ölçüldü: 19/21.
  - `glslc` **tip denetleyicisi olarak çalışıyor.** Tulpar'ın tip sistemi vektör/matris bilmediği için
    ilk dilimde bir tip denetleyicisi yazmak zorunda kalmıyoruz; kapının pozitif kontrolü bu sınırı
    görünür tutuyor (§6).
  - Planın Faz 8 kapısı bu yoldan **geçilebilir** (ölçüldü):
    fp16 → `GL_EXT_shader_explicit_arithmetic_types_float16` derleniyor (`OpCapability Float16`);
    wave genişliği → `layout(local_size_x_id = 0)` + `GL_KHR_shader_subgroup_ballot` derleniyor.
  - Terk maliyeti düşük: GLSL bir *ara temsil* olarak kullanılıyor, asıl yatırım ön uçta (tip sistemi,
    reflection, permutation). Yarın Slang ya da doğrudan SPIR-V'ye geçilirse **ön uç aynen kalır.**
- **Eksisi:** metin üzerinden gitmek; GLSL'in ifade edemediği bir şey istenirse (SPIR-V'ye özel
  decoration) yol tıkanır. Bugünkü 21 shader'da böyle bir şey yok. İkinci eksi: `glslc` derleme
  zamanı bir dış araç — ama zaten öyle.

**Karar:** **Seçenek C.** Slang kararı (EK B.1) "kendi SPIR-V backend'imizi yazmayalım" derken haklı;
ama aynı külfetten kaçmanın **daha ucuz** ve zaten kurulu olan yolu GLSL + `glslc`. Slang'in tek gerçek
üstünlüğü (MSL/WGSL) bu motorun bugünkü hedef kümesinde **alıcısız**. Metal backend'i gerçekten
gündeme gelirse karar yeniden açılır ve o zaman değişecek olan **yalnız arka uç** olur.

> PLAN EK B.1'e düşülecek not: "Tulpar → Slang → SPIR-V" satırı ⚠️ REV — ölçüm sonrası
> "Tulpar → GLSL → glslc → SPIR-V" oldu, gerekçe yukarıda.

---

## 5. Prototip — ne yaptı, ne kadarı tuttu

### 5.1 Tasarım: `.tprs` ayrı bir dil değil

Alt kümenin en önemli tasarım kararı: **`.tprs` dosyası geçerli Tulpar sözdizimidir.** Shader'a özgü
bildirimler sıradan Tulpar değişken bildirimleridir, başlatıcıları bir **işaret çağrısıdır**:

```
str asama = "frag";                  // aşama
vec2 v_uv     = girdi(0);            // layout(location=0) in vec2 v_uv;
float v_e     = girdi_duz(2);        // flat in
vec4 o_color  = cikti(0);            // layout(location=0) out
sampler2D u_s = ornek(0, 0);         // layout(set=0,binding=0) uniform sampler2D
Frame u       = tekduze(0, 0);       // uniform blok (struct Frame gövde olur)
Push pc       = itme();              // push_constant blok
Skin s        = depo(0, 4);          // readonly std430 SSBO (depo_yaz: writeonly)
float k       = ozel(0, 1.0);        // layout(constant_id=0) const
```

Bunun kârı şu: Tulpar'ın **nitelik (attribute) sözdizimi olmadan** (T13) bağlama bilgisini taşıyabiliyoruz
ve "alt küme Tulpar'dır" iddiası ölçülebilir hale geliyor — `--tulpar-parse` bunu gerçek `./tulpar`
ikilisini çağırarak ölçüyor.

### 5.2 Ölçüm 1 — SPIR-V bayt eşitliği

`python3 engine/tools/tpr_shader.py --check`

| | Sayı |
|---|---|
| Depodaki shader | 21 |
| Alt kümeye taşınan `.tprs` | **19** |
| Üretilen SPIR-V'si depodaki `*_spv.h` ile **bayt bayt aynı** | **19 / 19** — `mesh.frag` için referans `b6f593e` (aşağıdaki not) |
| Taşınamayan | 2 — `triangle.vert` (T9 dizi kurucu literali), `cull.comp` (T20 compute) |

Taşınanlar arasında motorun **en ağır shader'ı** olan `mesh.frag` de var (7972 bayt SPIR-V: CSM atlası,
kümelenmiş ışıklar, `findLSB` döngüsü, `sampler2DShadow`, iç içe `for`). `spirv-val` üretilen çıktıyı
geçerli buluyor.

Satır sayısı olarak alt küme daha uzun değil: 19 dosya için `.tprs` **580** satır, karşılık gelen
`.vert/.frag` **605** satır (`b6f593e`) (işaret çağrıları `layout(...)` satırlarından kısa).

> ⚠️ `mesh.frag` notu: bu belge yazılırken `engine/rhi/shaders/mesh.frag` başka bir çalışmada (PBR)
> değiştirildi. `mesh.frag.tprs` **`b6f593e`'deki** sürümü karşılıyor ve o sürümün SPIR-V'siyle bayt
> aynı (7972 bayt, `git show b6f593e:engine/rhi/shaders/mesh_frag_spv.h` ile doğrulandı). Kapı bunu
> sessizce kırmızı ya da sessizce yeşil yapmıyor: her `.tprs` çevrildiği GLSL kaynağının özetini
> taşıyor (`// kaynak-ozet:`), özet tutmazsa **GÖRÜNÜR ATLANDI** yazıyor (§6).

### 5.3 Ölçüm 2 — bugünkü Tulpar grameri ne kadarını kabul ediyor

`python3 engine/tools/tpr_shader.py --tulpar-parse` (gerçek `./tulpar typecheck` çağrılıyor)

| | Sayı | Hangileri |
|---|---|---|
| **Parse hatası 0** | **11 / 19** | bloom_bright, bloom_down, bloom_up, compose, motion.frag, motion.vert, triangle.frag, ui.frag, ui.vert, ui_overdraw, ui_sdf |
| G3 (sabit boy dizi) yüzünden düşen | 6 | mesh.vert, mesh_cull.vert, mesh_skin.vert, shadow.vert, shadow_cull.vert, shadow_skin.vert |
| G4 (bit işlemleri) yüzünden düşen | 1 | post.vert |
| G3 **ve** G4 | 1 | mesh.frag |
| Beklenmeyen düşüş | **0** | |

Bu 11 dosyada `./tulpar typecheck` yalnız `Unknown type 'vec3'` türü **tip** uyarıları veriyor — tek bir
sözdizimi hatası yok. Ölçümün söylediği:

> Bugünkü Tulpar grameri, motorun shader'larının yarısından fazlasını **olduğu gibi ayrıştırıyor.**
> Faz 8'in gramer maliyeti iki maddeye (G3 + G4) inmiş durumda; geri kalan bütün iş **tip sisteminde**.

Ayrıca G1 (`float(x)` çağrısı) bir *parse* hatası olarak görünmüyor çünkü alt küme onu `f32(x)` yazarak
dolanıyor — yani G1 kapatılana kadar yaşanabilir, G3/G4 değil.

### 5.4 Prototipin kapsamadıkları (dürüst liste)

Prototip **çevirici**dir, derleyici değil. Yapmadıkları:

- **Tip denetimi yok.** `vec3 c = texture(...)` (vec4 → vec3) prototipten geçer, `glslc` yakalar. Kapıda
  bu ayrı bir pozitif kontrol olarak duruyor ki sınır görünür kalsın.
- **Yerleşim doğrulaması yok.** `std140`/`std430` ofsetlerini `glslc` hesaplıyor; CPU tarafındaki C++
  struct'la karşılaştırma **yapılmıyor** — oysa Faz 8'in asıl vaadi bu (T22).
- **Reflection, permutation, precision (fp16) yok.** Planın kapısı olan fp16 kazancı **ölçülmedi**;
  yalnız `glslc`'nin fp16'yı derlediği gösterildi.
- **Compute aşaması yok** (T20), **dizi kurucu literali yok** (T9), **`out` parametre yok** (T21).
- **Tulpar'ın kendi lexer/parser'ı kullanılmıyor.** Prototip Python'da kendi ayrıştırıcısını taşıyor —
  `src/` içine yarım bir dil implementasyonu sızdırmamak için bilinçli. `--tulpar-parse` ise gerçek
  `./tulpar` ikilisini çağırıyor, yani "alt küme Tulpar'dır" iddiası prototipin kendi ayrıştırıcısına
  değil **asıl derleyiciye** karşı ölçülüyor.

---

## 6. Kapı — `tests/faz8_shader_audit.py`

`build.sh`'e **bağlı değil**, elle koşar: `python3 tests/faz8_shader_audit.py`

**[1] Gerçek varlıklar.** 19 `.tprs` çevrilir, `glslc` ile derlenir, depodaki `*_spv.h` ile bayt
karşılaştırılır. Tek fark kırmızı.

**[2] Pozitif kontrol — beş tane, üç ayrı katmanı ayrı ayrı yokluyor:**

| Kontrol | Hangi katman yakalamalı | Ölçülen sonuç |
|---|---|---|
| sözdizimi hatası | çeviricinin ayrıştırıcısı | `satir 3: ')' bekleniyordu, '0.0' bulundu` |
| bilinmeyen tip (`qvec9`) | çeviricinin anlamsal denetimi | `satir 3: bilinmeyen tip 'qvec9'` |
| bilinmeyen fonksiyon | çeviricinin anlamsal denetimi | `bilinmeyen fonksiyon 'kokusuz_fonksiyon'` |
| tip uyuşmazlığı (vec4→vec3) | **çevirici YAKALAMAZ**, `glslc` yakalar | `glslc: error: '=' : cannot convert…` |
| değiştirilmiş sabit (`0.25`→`0.26`) | **bayt karşılaştırmasının kendisi** | farkı gördü (1388 vs 1388 bayt) |

Son satır kritik: o kontrol olmadan "19/19 bayt aynı" boş bir iddia olurdu — aynı boyutta ama farklı
içerikli bir SPIR-V üretilip fark **görüldü**, yani karşılaştırma canlı.

**Uçtan uca kırmızı kanıtı.** Gerçek bir varlıkta tek bir sabit bozulduğunda
(`ui.frag.tprs`: `v_encode > 0.5` → `> 0.6`) kapı `KIRMIZI ui.frag SPIR-V farkli` diyor ve **çıkış kodu
1**; geri alındığında 0.

**`glslc` yoksa.** Sessiz `return` yok: SPIR-V'ye bağlı her adım `ATLANDI` satırı yazar, sayılır ve sonda
`UYARI: N adım ATLANDI … bu denetim TAM koşmadı` basılır. `glslc`'siz koşumda 3 yeşil / 0 kırmızı /
**21 atlandı** görülüyor — yani "yeşil" görünen bir hiçlik değil.

**Hareketli referans koruması.** `engine/rhi/shaders/*` başkası tarafından değiştiğinde kapı ne yanlış
suçlama (sessiz kırmızı) ne de ölçmeden geçme (sessiz yeşil) yapıyor: `.tprs`'teki `// kaynak-ozet:`
özeti tutmazsa görünür `ATLANDI … .tprs yeniden ported edilmeli` yazıyor. Özetleri `--pin` tazeler.

---

## 7. Fazlama

Her dilimin **kapısı** var; kapısı ölçmeyen dilim yok.

| Dilim | İş | Kapı | Tahmini büyüklük |
|---|---|---|---|
| **8.0** | *(bitti — bu belge)* Fizibilite: alt küme + GLSL üretimi + bayt kapısı | 19/21 shader bayt aynı; 11/19 bugünkü gramerden geçiyor | ~1 200 satır Python, **teslim edildi** |
| **8.1 Gramer boşlukları** | G1 (`float(x)` çağrısı), G3 (`T[N]`), G4 (bit işlemleri `& \| ^ ~ << >>` + atamalı biçimleri), G5 (`const`). Lexer + `parse_type` + `parse_type_decl` + öncelik merdiveni | `--tulpar-parse` **19/19** parse hatasız; mevcut `tests/typeinfer` ve `build.sh test/suites` yeşil kalır | Küçük: ~300–500 satır `src/lexer` + `src/parser`, + typeinfer/LSP kuyruğu |
| **8.2 GPU tip sistemi** | `f32`, `uint`, `vec2/3/4`, `ivec/uvec`, `mat3/mat4`, swizzle — **yalnız shader bağlamında**, CPU tarafına dokunmadan. `DataType` yerine ayrı bir GPU tip tablosu | `.tprs` **tip hatası** üretebiliyor: `vec3 c = texture(...)` artık `glslc`'ye kalmadan yakalanıyor; pozitif kontrol o satırın katmanını `cevirici`'ye kaydırır | Orta: yeni tip tablosu + ifade tipleme; ~1 500 satır. **PLAN §11 "sistem alt kümesi" ile aynı iş** |
| **8.3 Ön uç Tulpar'a taşınır** | Python prototipi atılır; `src/` içinde `.tprs` → GLSL. `tulpar shader` alt komutu; `compile_shaders.py` yerini alır | Aynı bayt kapısı **19/19** yeni çeviriciyle; `engine_tests` etkilenmez | Orta: prototip zaten tasarımı sabitledi |
| **8.4 Yerleşim doğrulaması** | `std140/std430` ofsetleri Tulpar tarafında hesaplanır, C++ `Renderer::Frame` / `Push` ile **karşılaştırılır** | Bilerek kaydırılmış bir CPU struct'ı kapıyı kırmızı yapar (pozitif kontrol) | Küçük-orta. **Faz 8'in ilk gerçek kazancı buradan gelir** — bugün hiçbir şey bunu denetlemiyor |
| **8.5 Permutation + reflection** | Sahne bilgisiyle varyant üretimi (gölge açık/kapalı, kademe sayısı, PBR/Lambert); descriptor tablosu shader'dan türetilir | Elle yazılmış varyant sayısı ile üretilen eşleşir; `*_spv.h` toplam boyutu ölçülür | Büyük |
| **8.6 Precision / fp16** | Otomatik `f16` yükseltme + `GL_EXT_shader_explicit_arithmetic_types_float16` | **PLAN'ın kapısı:** aynı ALU işi elle yazılmışa karşı ölçülür, fp16 kazancı telefonda (Mali-G72) gösterilir | Büyük; gerçek cihaz şart |
| **8.7 Compute** | T20: `local_size` (spec sabitli), `shared`, `barrier()`, iş grubu kimlikleri, bellek sıralaması | `cull.comp` bayt aynı → **21/21** | Orta |

**Sıralama gerekçesi.** 8.1 ucuz ve tek başına 8 shader açıyor, ama **tek başına değer üretmiyor** —
`.tprs` hâlâ elle yazılmış GLSL'in uzun yolu olur. Gerçek kazanç 8.4'te başlıyor (bugün hiç kimsenin
denetlemediği CPU-GPU yerleşimi) ve 8.5/8.6'da toplanıyor. Bu yüzden Faz 8'i kritik yoldan çıkaran
karar **hâlâ doğru**: 8.2 zaten PLAN §11'in sistem alt kümesiyle aynı işi istiyor ve o iş L1'in Tulpar'a
taşınması için nasıl olsa yapılacak. Faz 8, o alt kümenin **üstüne** gelir — öncesine değil.

---

## 8. Yapmadıklarım / açık sorular

- **fp16 kazancı ölçülmedi.** `glslc`'nin fp16 derlediği gösterildi; Mali-G72'de ALU kazancı ölçülmedi.
  Planın Faz 8 kapısı budur ve hâlâ açıktır.
- **CPU-GPU yerleşim doğrulaması yok** (8.4). Faz 8'in en somut kazancı ama prototipte hiç yok.
- **`src/` içine hiçbir şey yazılmadı** — bilinçli. Bu belgedeki gramer boşlukları (G1/G3/G4/G5)
  *ölçüldü*, kapatılmadı.
- **`mesh.frag`'ın yeni PBR sürümü taşınmadı** (`out` parametre, `const vec4` yerel, `continue`,
  `exp2` — T16/T21). Kapı bunu görünür `ATLANDI` ile bildiriyor, sessizce geçmiyor.
- **`triangle.vert` ve `cull.comp`** taşınmadı (T9, T20) — 8.7 ve küçük bir 8.1 kuyruğu.
- **Hata mesajları tek dilde.** Prototip Türkçe basıyor; `src/`'ye taşındığında `tr_en(...)`'den
  geçmesi gerekir.

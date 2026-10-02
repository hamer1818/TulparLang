---
tags: [component, ffi, tooling]
---

# Yerel eklentiler (`--ext`, K303)

Bir C kitaplığını **derleyiciyi yeniden derlemeden** Tulpar'a bağlamanın yolu
(2026-10-02). Kurulu/yayınlanmış `tulpar` bir **eklenti dizinini** okur:
bildirim (`tulpar-ext.json`), isteğe bağlı Tulpar modülleri, link arşivleri.
Derleyici hiçbir eklentiyi adıyla tanımaz — `src/` altında eklentiye özgü
tek bir ad yok. İlk tüketici Tulpar Engine (`import "engine"`), ama mekanizma
genel: motor da herhangi bir dış kitaplık gibi bir eklenti paketi sunuyor.

Neden: 2026-09-20'de motor derleyiciden çıkarıldı (#333, kullanıcı kararı:
"dilde yalnız dilin kendi özellikleri"). Motor o günden beri köprüyü
13 dosyaya **ters yama** ile geri takan ayrı bir derleyiciyle çalışıyordu
(motor deposu `tools/motor_derleyici.sh`) ve iki günde iki kez kırıldı.

## Bildirim

```json
{
  "tulpar_ext": 1,
  "name": "ornek",
  "modules": {"ornek": "ornek.tpr"},
  "functions": [
    {"name": "ornek_topla", "params": ["a: i64", "b: i64"], "returns": "i64", "doc": "..."},
    {"name": "ornek_geri", "symbol": "ornek_geri_cagir", "params": ["fn: str", "x: f64"], "returns": "f64"}
  ],
  "link": {
    "linux":   {"lib_dirs": ["lib"], "libs": ["ornek"], "group": false, "flags": []},
    "macos":   {"lib_dirs": ["lib"], "libs": ["ornek"]},
    "windows": {"lib_dirs": ["lib"], "libs": ["ornek"]},
    "android": {"lib_dirs": ["android/{abi}"], "libs": ["ornek_android"]}
  }
}
```

| tip | C | Tulpar (typeinfer) | not |
|---|---|---|---|
| `i32` | `int32_t` | int | `int` → i64 kısaltması |
| `i64` / `int` | `int64_t` | int | |
| `f32` | `float` | float (int de kabul) | |
| `f64` / `float` | `double` | float (int de kabul) | |
| `bool` | `int` (0/1) | int + bool | dönüşte ≠0 → true |
| `str` | `const char *` | str | parametre: kopyasız, çağrı süresince; dönüş: hemen kopyalanır, NULL → "" |
| `void` | — | — | yalnız dönüş |

`symbol` verilmezse `name`. En çok 16 parametre. `lib_dirs` ve `modules`
yolları bildirimin dizinine göre; `{abi}` Android'de `arm64-v8a`/`x86_64`.
`group: true` GNU ld'de `--start-group/--end-group` (macOS ld64'te yok sayılır).

## Bulunma — sıra ve gerekçe

1. `--ext <yol>` (tekrarlanabilir; her komutta, betikten önce)
2. `TULPAR_EXT_PATH` (POSIX `:`, Windows `;` ayrılmış liste)
3. `tulpar.toml` `[ext] paths = ["..."]` — çalışma dizinindeki proje dosyası
   (göreli yollar ona göre); LSP belgenin dizininden **yukarı** arar.

`<yol>` ya içinde `tulpar-ext.json` olan dizin ya da doğrudan bildirim.
Aynı `name` ikinci kez gelirse **ilki kazanır** (komut satırı, ortamdaki eski
kopyayı ezer). İki farklı eklenti aynı fonksiyon/modül adını verirse hata.
**Verilen bir yol bulunamaz/bozuksa sessiz atlama yok** — derleme başlamaz;
ama `tulpar version`/`fmt`/`pkg` eklentiyi hiç yüklemez (bozuk bir
`TULPAR_EXT_PATH` derlemeyen komutu durdurmasın).

Üçünün birden olma sebebi kullanıcılar: editör/araç (motorun F5'i)
alt süreç ortamıyla verir, CLI kullanıcısı bayrakla, proje bir kez
`tulpar.toml`'a yazar ve `tulpar oyun.tpr` doğrudan çalışır.

## Derleyicide nerede

- `src/ext/extensions.{hpp,cpp}` — bulma, bildirim ayrıştırma (cJSON),
  kayıt, link bayrakları. Tek durum: hangi eklenti bu derlemede **kullanıldı**.
- `src/main.cpp` — `take_cli_args` (`--ext`'i argv'den her şeyden önce ayıklar;
  betikten sonraki argümanlar programa aittir), `ensure_extensions_loaded`
  (yerleşik adını taşıyan eklenti fonksiyonu reddedilir:
  `typeinfer_is_builtin_name`). `tulpar build` önbelleği eklentinin bildirim/
  modül/arşiv mtime'ını da girdi sayar.
- `src/aot/llvm_backend.cpp` — `emit_ext_call`: sembol tembel bildirilir,
  argümanlar **tipli yolla** üretilir (`float x` ham double gider), tipi
  belirsiz (kutulu) argüman satır içi çevrilir (int↔float, bool, dizgi
  karakterleri nesnenin içinde: `TULPAR_OBJSTRING_CHARS_OFFSET`). Sonuç ham
  tipli `TypedValue` (kutulama yok). Kullanıcının aynı adlı fonksiyonu kazanır.
  Float→int **doyurarak** (`llvm.fptosi.sat`: NaN/taşma poison değil).
  Modüller `import_load_module`'da gömülüden sonra, diskten önce.
- `src/aot/aot_pipeline.cpp` — yalnız **kullanılan** eklentinin bayrakları,
  tame'den sonra `-ltulpar_runtime`dan önce (native, sessiz, web, Android).
  Kullanılan eklentinin o hedef için `link` bölümü yoksa adıyla durur. Sessiz
  (çalıştır) yolda eklenti kullanıldıysa linker çıktısı atılmaz: düşen linkte
  tanımsız sembolün **adı** basılır.
- `src/typeinfer/typeinfer.cpp` — imzalar yerleşiklerden sonra, kullanıcı
  tanımlarından önce; modüller aynı sırayla çözülür.
- `src/lsp/` — hover (`*(yerel eklenti: ad)*`), tamamlama, imza yardımı;
  eklentiler `--lsp --ext`, ortam ve belgeden yukarı `tulpar.toml`'dan.
- `src/vm/runtime_bindings.cpp` — eklentinin Tulpar'ı **geri** çağırması için
  düz C yüzü (VMValue görmeden):
  `void *tulpar_ext_func_lookup(const char *ad, int *arite)`,
  `int tulpar_ext_call_f64(void *fn, int arite, const double *a, int n)`,
  `double tulpar_ext_eval_f64(...)` (sonuçlu). `call_f64`'ün imzası bilerek
  bir olay-kancası tablosunun alanına birebir uyar (araya katman girmesin —
  ölçüm aşağıda).

## ABI uyumu kimde

Derleyici statik arşivde tip göremez: bildirim `i32` derken C `double`
alıyorsa hata yok, çöp var. Uyum **eklentinin kendi derlemesinde** kilitlenir:
her imzayı tipli bir işlev işaretçisine atayan bir derleme birimi (C++'ta
farklı işlev işaretçisi tipleri arasında örtük çevrim yok; C'de
`-Werror=incompatible-pointer-types`). Örnek eklenti (`ornek_eklenti.c`
ABI_KILIDI) ve motor (`bridge/tulpar_abi.cpp`, SPEC'ten üretilen satırlar)
bunu yapıyor; ikisinin de pozitif kontrolü var (kasıtlı kayma derlemede düşer).

## Kapılar

`tests/yerel_eklenti.sh` (`build.sh suites`, üç CI ayağı): örnek eklenti
statik arşive derlenir, her tip ailesi (10 parametre, NULL dizgi, statik
tampon kopyası, Tulpar'ı geri çağırma, eklentinin modülü/struct'ı) üç bulunma
yolunun her biriyle çalıştırılır, çıktı `kullan.beklenen` ile bayt bayt.
Pozitif kontroller: eklentisiz `import "ornek"` (hata + `--ext` ipucu),
bildirimde bozuk sembol (link o adla düşer; çalıştır ve build), bozuk
parametre tipi (typecheck yakalar), ABI kilidi, bozuk JSON (satırıyla),
olmayan dizin, yerleşik ad, eksik link bölümü, fazla argüman. Ek: eklentiyi
kullanmayan program ona bağlanmaz; `--ext` ortamdakini ezer; kullanıcı
fonksiyonu gölgeler; LSP (python3 varsa) hover/tamamlama/imza yardımı.

Motor tarafı uçtan uca: tulpar-engine `tools/tulpar_dogrula.sh --tam`
(dalga/aksiyon kapı satırları bayt bayt, köprü testi, 6 örnek, 4 dil sondası).

## Ölçüm (2026-10-02, Ryzen 7 9800X3D + RTX 5080, Linux, LLVM 22)

Motor örneği `engine_cagri_olcumu.tpr`, 20M çağrı, eski köprü (ters yamalı
derleyici, `aot_eng_*_ptr` VMValue sarmalayıcısı) → eklenti:

| çağrı | eski | eklenti |
|---|---|---|
| `eng_frame()` → int | 2.32 ns | 0.77–0.97 ns |
| `eng_camera_x()` → float (sonuç karşılaştırmada) | 1.53 ns | 0.77 ns |
| `eng_camera(6 float)` tipli | 3.5–3.8 ns | 1.35 ns |
| `kamera(...)` (tipsiz sarmalayıcı, kutulu arg) | 3.5–3.7 ns | 1.34 ns |
| `eng_key_down("W")` (dizgi arg) | 2.9–3.1 ns | 1.92 ns |
| `f = f + eng_camera_x()` (float birikimci) | 2.30 ns | **4.02 ns** |

Son satır köprü değil, **LLVM'in yazmaç ayırıcısı** (yeniden üretildi ve
kök nedeni ölçüldü 2026-10-02, aynı makine, LLVM 23.1, `taskset -c 10,11`):

- Motor gerekmeden yeniden üretildi: `teng_camera_x`'in aynısı
  (`g ? g->x : 0`) dönen minik bir eklenti, 20M çağrı: `kam_frame()` 0,77 ns,
  `kam_x() > 1.0` 0,77 ns, `f = f + kam_x()` **4,02 ns**. Eski ters yamalı
  derleyicinin ikilisi (gerçek `teng_camera_x`) aynı oturumda 1,92 ns.
- IR'da döngü yalnız `call double @kam_x()` + `fadd` + sayaç — köprüye ait
  hiçbir şey yok. Fark üretilen asm'de: `f` çağrıyı aşıyor (SysV'de hiçbir
  xmm yazmacı çağrıda korunmaz) ve LLVM'in greedy ayırıcısı her turda
  **iki** yığın gidiş-dönüşü üretiyor:
  ```
  movsd %xmm0,-0x30(%rbp)   ; dökme
  call  kam_x
  movsd -0x30(%rbp),%xmm1   ; geri yükle
  addsd %xmm0,%xmm1
  movsd %xmm1,-0x30(%rbp)   ; GEREKSIZ dökme
  movsd -0x30(%rbp),%xmm0   ; GEREKSIZ geri yükleme (phi xmm0'da)
  ```
  Döngü taşıyan bağımlılık iki kez depodan-yüke iletimden geçiyor.
- Aynı `kamera.o` ile üç yerleşim elle yazılmış asm'de (yalnız yazmaç
  yerleşimi farklı): bu çift dökme **4,09 ns**; tek dökme (gcc -O2'nin
  ürettiği: dök, çağır, yükle, topla) **2,30 ns**; birikimci çağrıdan
  korunan bir GPR'de (eski köprünün yerleşimi) **2,30 ns**. Yani gerilemenin
  tamamı ikinci gidiş-dönüş.
- **Eski yol neden hızlıydı:** `f` orada kutulu bir VMValue'ydu (dinamik tür
  dalı); yükünün phi'si `i64` olduğu için LLVM onu çağrıdan korunan `r14`'te
  tuttu ve her turda `movq r14→xmm, addsd, movq xmm→r14` yaptı — bellekten
  hiç geçmedi. "Dönüş `{i64,i64}` tamsayı yazmaçlarındaydı" açıklaması
  yetmiyor: aynı döngüde yalnız dönüşü `{i64,i64}` yapan IR (`bitcast` ile
  fadd) LLVM 23'te AYNI çift dökmeyi üretiyor (ölçüldü).
- **Bizim kodgen'imizin dışında:** eşdeğer C (`for (...) f += kam_x();`)
  clang 23 -O2 ile birebir aynı çift dökmeyi üretiyor; gcc -O2 tek dökme.
  `fadd` işlenen sırasını çevirmek (LLVM kanonikleştiriyor),
  `-regalloc=pbqp`/`basic`, `-split-spill-mode`, `-enable-misched=false`,
  `-exhaustive-register-search`, blok yerleşimi kapatmak: hiçbiri değiştirmedi
  (llc 23.1 ile denendi). Çağrı öznitelikleri (`nounwind`, `willreturn`,
  `memory(...)`) çağrının yazmaç bozduğunu değiştirmez; bildirime `pure`
  alanı eklemek yalnız çağrıyı döngüden DIŞARI çıkarabilirdi (argümansız,
  yazmasız döngüde) — bu ölçüyü sıfırlar ama gerçek bir düzeltme değildir ve
  yanlış bildirilince yanlış kod üretir; bu yüzden eklenmedi.
- Windows x64'te xmm6–15 çağrıdan korunur: bu kalıp orada beklenmez
  (ölçülmedi).

Sonuç: düzeltilmedi; kapsamı dar (çağrı sonucunu sıcak döngüde float
birikimciye toplamak) ve öteki bütün çağrı kalıpları 2–3× hızlandı.

Olay kancası (motor → Tulpar, `tools/kanca_olcumu.py`, 200 boş kanca, 2000
kare, aynı oturumda sırayla): eklenti yolu 5.5–6.0 ns, eski üretilmiş VMValue
bağlaması 5.6–6.2 ns; kalıcı bellek büyümesi ikisinde de kontrolle aynı. İlk
yazımda (yapıştırıcı sarmalayıcı + runtime'da dışarı alınmış 33 kollu dağıtım)
6.4–7.7 ns ölçüldü; `tulpar_ext_call_f64` tek fonksiyonda 0..4 arite satır içi
kollarla düzeltildi.

## Sınırlar

- Yalnız skaler ABI: struct değer geçişi yok (K304), işlev işaretçisi/geri
  çağrı değeri yok (K305 — ad tabanlı `tulpar_ext_func_lookup` var).
- Windows: mekanizma derlenir ve kapı CI'da koşar; motorun bildiriminde
  Windows bölümü yok (ölçülmedi).
- Web: `link.web` verilirse em++ satırına eklenir; ölçülmedi.

## İlgili
[[Imports and Modules]] · [[AOT Backend]] · [[Type Inference]] · [[LSP]] · [[Runtime]] · [[Tooling]]

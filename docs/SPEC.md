# Tulpar Dil Tanımı — v0 (parça parça büyür)

> **Durum (2026-11-26):** ilk sürüm. Bu belge dilin **bugünkü** davranışını tanımlar;
> derleyicinin tek uygulaması (AOT/LLVM) referanstır. Her ` ```tpr ` örneği **çalıştırılır**:
> `tests/spec_ornekleri.py` örneği derler, çıktısını `// cikti:` satırlarıyla, derleme hatasını
> `// hata:` satırıyla karşılaştırır (`build.sh suites`'te). Belge ile derleyici ayrışırsa kapı
> kırmızı — belge eskiyemez. Bir bölüm eksikse bu, o konunun **henüz tanımlanmadığı** anlamına
> gelir; tanımsız davranışa dayanmayın.
>
> Ayrıntılı kaynaklar: bellek için [Memory Model](mindmap/Memory%20Model.md) ve "RFC: Tulpar
> Bellek Sözleşmesi v0"; kararlar için [Decisions](mindmap/Decisions.md); durum için
> [STATUS](../STATUS.md).

## 1. Sözcük yapısı

- Kaynak **UTF-8**; dosya uzantısı `.tpr`. Satır sonu anlamsızdır, deyimler `;` ile biter.
- Yorumlar: `// satır sonuna kadar` ve `/* blok */`.
- Tanımlayıcı: harf ya da `_` ile başlar, harf/rakam/`_` ile sürer. Türkçe harfler geçerlidir.
- **Anahtar kelimeler** İngilizce ve Türkçe eşleriyle (ikisi de ayrılmıştır, ad olarak
  kullanılamaz):

| İngilizce | Türkçe eşleri |
|---|---|
| `if` `else` | `eğer`/`eger`, `yoksa`/`değilse`/`degilse` |
| `while` `for` `in` | `iken`/`tekrar`, `için`/`icin`, `içinde`/`icinde` |
| `break` `continue` `return` | `dur`, `devam`/`devam_et`, `döndür`/`dondur`/`don`/`geri_döndür` |
| `func` `struct`/`type` `enum` | `fonksiyon`/`fonk`/`işlev`, `tip`, `sayım`/`sayim` |
| `var`/`let` `const` `move` | `değişken`/`degisken`, —, `taşı`/`tasi` |
| `try` `catch` `finally` `throw` | `dene`, `yakala`, `sonunda`, `fırlat`/`firlat` |
| `match` `async` `await` `import` | `eşle`/`esle`, —, `bekle`, `içe_aktar`/`ice_aktar` |
| `true` `false` `null` | `doğru`/`dogru`, `yanlış`/`yanlis`, — |
| `int` `float` `str` `bool` `array` `json` | `tamsayi`, `ondalik`, `metin`, `mantiksal`, … |

```tpr
// Ayrılmış kelime ad olamaz — hata kelimenin kendisini söyler.
int dur = 3;
// hata: dur
```

- **Literaller:** tamsayı (64 bit, `0x` onaltılık), ondalık (`1.5`, `1e3`), dizgi (`"..."`,
  kaçışlar `\n \t \" \\`), `true`/`false`, `null`, dizi `[1, 2]`, nesne `{"a": 1}`.

```tpr
print(0x1F + 1);
print(1.5 * 2);
print("a\tb");
print([1, 2, 3]);
// cikti:
// 32
// 3
// a	b
// [1, 2, 3]
```

## 2. Tipler

| Tip | Anlam |
|---|---|
| `int` | 64 bit işaretli tamsayı |
| `float` | 64 bit IEEE-754 |
| `bool` | `true` / `false` |
| `str` | değişmez UTF-8 dizgi |
| `int[]` `float[]` `str[]` `bool[]` `json[]` | tipli dizi (`arrayInt` … eski yazım) |
| `array` | tipsiz dizi (elemanlar her tip) |
| `json` | dinamik değer: nesne, dizi ya da skaler |
| `var` | tip çıkarımı yapılan / dinamik yerel |
| `struct Ad { ... }` | adlandırılmış kayıt |
| `enum Ad { ... }` | adlandırılmış **tamsayı** sabitleri |

Statik tipler **derleme öncesi denetlenir** (`[typecheck]` uyarısı; `--strict` ile hata).
Çalışma zamanında her değer bir tip etiketi taşır; tipli yollarda kutusuz koşulur.

```tpr
int a = 7;
float b = 2.5;
str s = "x";
bool t = true;
print(a / 2);
print(a % 3);
print(b * 2);
print(s + "y");
print(t);
// cikti:
// 3
// 1
// 5
// xy
// true
```

**Tamsayı bölmesi** `int / int` sonucu `int`'tir (sıfıra doğru keser). Farklı skaler tipler
arasında `==` derleme öncesi **sabit** diye raporlanır (çalışma zamanı önce etiketi
karşılaştırır).

### 2.1 Struct ve yöntem

Alanlar bildirim sırasıyla; tüm alanları `int`/`float`/`bool` olan struct kutusuz (değer)
temsil edilir. `func Tip.ad(...)` bir **yöntemdir**; gövdede örtük ilk parametre `self`.
`x.ad()` çağrısı alıcının statik tipine göre çözülür; serbest fonksiyon da yöntem gibi
çağrılabilir (`alan(r)` ≡ `r.alan()`).

```tpr
struct Kare { int k; }
func Kare.alan(): int { return self.k * self.k; }
func cevre(Kare q): int { return 4 * q.k; }
Kare q;
q.k = 3;
print(q.alan());
print(q.cevre());
// cikti:
// 9
// 12
```

### 2.2 Enum

`enum Renk { KIRMIZI, YESIL = 5, MAVI }` → 0, 5, 6. `Renk.YESIL` ayrıştırıcıda tamsayı
sabitine katlanır; `Renk` tip adı olarak `int` demektir. Bildirim sırası önemsizdir.

```tpr
enum Renk { KIRMIZI, YESIL = 5, MAVI }
Renk r = Renk.MAVI;
print(r);
// cikti:
// 6
```

### 2.3 Çoklu dönüş

`func f(): (float, float)` bir tuple döndürür; `float x, y = f();` açar.

```tpr
func bol(int a, int b): (int, int) { return a / b, a % b; }
int q, k = bol(17, 5);
print(q + k * 10);
// cikti:
// 23
```

### 2.4 Jenerikler (tip silme)

`func f<T, U>(...)` ve `struct Kutu<T> { T deger; }`. **Tip silme** ile uygulanır: tek gövde,
`T` çalışma zamanında dinamik (kutulu) değerdir. Tip denetimi `T`'yi her **çağrı yerinde**
bağlar: aynı `T`'nin iki farklı tipe bağlanması ve bağlanan dönüş tipinin yanlış kullanımı
raporlanır. Açık tip argümanı (`f<int>(x)`) yoktur.

```tpr
func ilk<T>(T[] xs): T { return xs[0]; }
struct Kutu<T> { T deger; }
Kutu<str> k;
k.deger = ilk(["a", "b"]);
print(ilk([4, 5]) + 1);
print(k.deger);
// cikti:
// 5
// a
```

## 3. İfadeler

Öncelik (yüksekten düşüğe; aynı satır soldan birleşir):

| Düzey | İşleçler |
|---|---|
| 10 | `*` `/` `%` |
| 9 | `+` `-` |
| 8 | `<<` `>>` (toplamadan **gevşek**) |
| 7 | `<` `>` `<=` `>=` |
| 6 | `==` `!=` |
| 5 | `&` |
| 4 | `^` |
| 3 | `\|` |
| 2 | `&&` |
| 1 | `\|\|` |
| 0 | `c ? a : b` (sağdan birleşir) |

Tekli: `-x`, `!x`, `~x`. `&&` ve `||` kısa devre yapar. `+` iki dizgiyi birleştirir; dizgiye
sayı eklemek için `toString` kullanılır.

```tpr
print(1 + 2 * 3);
print(1 << 2 + 1);
print(true || false && false);
print(5 > 3 ? "evet" : "hayir");
// cikti:
// 7
// 8
// true
// evet
```

**Erişim:** `a[i]` (dizi, 0 tabanlı, sınır dışı çalışma zamanı hatası), `o["ad"]` ve `o.ad`
(nesne/struct alanı). `match` bir ifadedir (bkz. §4.3).

## 4. Deyimler

### 4.1 Kontrol akışı

`if/else`, `while`, C biçimli `for (init; koşul; adım)`, `for (x in xs)`, `break`, `continue`,
`return`. Blok `{ ... }` kendi kapsamını açar; içteki bildirim dışarıda görünmez.

```tpr
int t = 0;
for (int i = 0; i < 10; i++) {
    if (i % 2 == 0) { continue; }
    if (i > 7) { break; }
    t = t + i;
}
for (x in [100, 200]) { t = t + x; }
print(t);
// cikti:
// 316
```

### 4.2 İstisnalar

`throw <değer>` herhangi bir değeri fırlatır; `try { } catch (e) { } finally { }`. Yakalanmayan
istisna mesajı basar ve süreç **1** ile çıkar.

```tpr
func bol(int a, int b): int {
    if (b == 0) { throw "sifira bolme"; }
    return a / b;
}
try {
    print(bol(4, 2));
    print(bol(1, 0));
} catch (e) {
    print("yakalandi: " + e);
} finally {
    print("son");
}
// cikti:
// 2
// yakalandi: sifira bolme
// son
```

### 4.3 `match`

Desenler: sabit, `_`, dizi (`[a, b]`, `[ilk, ..kalan]`), nesne (`{x, y}`, `{tamam: true,
deger}` — sabit alan **kısıt**, ad **bağlama**), enum üyesi.

```tpr
func tarif(json v): str {
    return match v {
        []        => "bos",
        [a]       => "tek",
        [a, ..r]  => "cok " + toString(length(r) + 1),
        _         => "?"
    };
}
print(tarif([]));
print(tarif([7, 8, 9]));
print(match {"x": 3, "y": 4} { {x, y} => x * x + y * y, _ => 0 });
// cikti:
// bos
// cok 3
// 25
```

## 5. Fonksiyonlar ve kapanışlar

- `func ad(int a, b): int { ... }` — tipsiz parametre (`b`) dinamik değerdir. Eksik sondaki
  argümanlar varsayılan değeri (0) alır; fazla argüman hatadır. **Aşırı yükleme yoktur**: aynı
  adlı ikinci üst düzey fonksiyon hatadır.
- Fonksiyon adı bir **değer** olarak geçirilebilir; `call(f, a, b)` adıyla çağırır.
- Lambda: `(x) => x * 2`. Kapanış çevreleyen yerelleri **referansla** yakalar.
- **Döngü kapanışları iterasyon başına bağlanır** (JS `let`, 2026-11-26): `for` başlığında ya da
  gövdesinde bildirilen yakalanan değişken her iterasyonda yenidir; döngüden **önce** bildirilen
  paylaşılır.

```tpr
func uygula(fn, x) { return call(fn, x); }
func iki_kat(x) { return x * 2; }
var fns = [];
for (int i = 0; i < 3; i++) { push(fns, () => i * 10); }
print(uygula(iki_kat, 21));
print(call(fns[0]) + call(fns[1]) + call(fns[2]));
// cikti:
// 42
// 30
```

## 6. Hata değerleri: `Result` ve `?`

`import "sonuc";` (ya da `"result"`) bir sonucu `{"tamam": bool, "deger": v, "hata": e}` olarak
taşır: `tamam(v)`/`hata(e)` (`ok`/`err`), `ac`, `ac_yoksa`, `donustur`, `sonra`, `sarmala`.
**`?`** bir fonksiyon gövdesinde, deyimin **sonunda** (bildirim, atama, `return`, ifade deyimi),
bir çağrının/değişkenin/erişimin ardından yazılır: değer hataysa **aynı sonuç hemen döner**,
değilse `deger` alınır.

```tpr
import "sonuc";
func bol(int a, int b): json {
    if (b == 0) { return hata("sifira bolme"); }
    return tamam(a / b);
}
func toplam(int a, int b, int c): json {
    int x = bol(a, b)?;
    int y = bol(x, c)?;
    return tamam(x + y);
}
print(ac(toplam(100, 5, 2)));
print(hatasi(toplam(1, 0, 2)));
// cikti:
// 30
// sifira bolme
```

```tpr
import "sonuc";
var x = tamam(1)?;
// hata: '?' yalniz fonksiyon govdesinde
```

## 7. Modüller

`import "ad";` çözüm sırası: gömülü standart kitaplık (`wings`, `router`, `test`, `sonuc`, …)
→ yüklü yerel eklentinin modülü (`--ext`) → içe aktaran dosyanın dizininde `ad.tpr` / `ad` →
çalışma dizini → `tulpar_modules/ad/ad.tpr` → `tulpar_modules/ad.tpr`. Modül kimliği mutlak
yoldur; ikinci içe aktarım no-op'tur. `import "m" as k;` modülün üst düzey fonksiyonlarını `k__ad`
olarak adlandırır; `k.ad(...)` ve `k__ad(...)` aynıdır.

## 8. Bellek modeli (özet)

Ayrıntı: "RFC: Tulpar Bellek Sözleşmesi v0" ve [Memory Model](mindmap/Memory%20Model.md).

- **Depolama sınıfları:** geçici dizgiler iplik **arenasında**; geçici dizi/nesneler açık bir
  kontrol noktası varken **bölgede** izlenir; global'e/kalıcı kaba yazılan değer **kalıcıya
  kopyalanır** (yazma bariyeri); dizgi sabitleri ölümsüz.
- **Kontrol noktası:** `int wm = arena_save(); ...; arena_drop(wm);` — arada üretilen geçici
  değerler drop'ta geri alınır. `arena_restore(wm)` aynı geri sarmayı yapar, kontrol noktasını
  bırakmaz. `@frame func f()` bunu otomatik yapar ve dönüş değerini kalıcılaştırır.
- **Kural 2.1:** drop/restore'dan **sonra** o kontrol noktasında üretilmiş bir geçici değeri
  okumak tanımsızdır. `TULPAR_ARENA_ZEHIR=1` bu okumayı görünür kılar.
- Kalıcıya kaçış: global'e atama, async görev argümanı/sonucu, thread argümanı — hepsi kopyalanır.
- `mem_stats()` sayaçları program içinden okur.

```tpr
json g = {};
func doldur() {
    int wm = arena_save();
    g = {"ad": "kalici-" + toString(1)};
    arena_drop(wm);
}
doldur();
print(g["ad"]);
// cikti:
// kalici-1
```

## 9. Eşzamanlılık (özet)

- `thread_create(isci, arg)` yeni işletim sistemi iş parçacığı; `arg` **derin kopyalanır**,
  sonuç `thread_join` ile **kopya** olarak döner. İş parçacığı başına arena.
- Paylaşılan değişebilir global'e `mutex_*` ya da `atomic_*` ile erişilir; korumasız erişim
  tanımsızdır (tip denetimi uyarır). `@thread_local` iş parçacığı başına global.
- `async func` + `await`: tek iş parçacığında kooperatif coroutine'ler (web/Android'de yok).

```tpr
func kare(n) { return n * n; }
int t = thread_create(kare, 12);
print(thread_join(t));
// cikti:
// 144
```

## 10. Yerel eklentiler (FFI, özet)

`tulpar-ext.json` bildirimiyle bir C kitaplığı derleyici yeniden derlenmeden bağlanır. Tipler:
`i32 i64 f32 f64 bool cbool str ptr void` ve `fn(...)` geri çağrı; `tulpar bindgen başlık.h`
bildirimi C başlığından üretir. Ayrıntı: [Eklentiler](mindmap/Eklentiler.md).

## Ek A — Bu sürümde tanımlanmamış

Dizgi indekslemenin bayt/karakter anlamı, `float` biçimlendirme kuralları, sayısal taşma
(64 bit sarma mı), `json` eşitliği, modül döngüleri, `finally` içinde `return`. Bunlara dayanan
kod bugünkü uygulamaya dayanır; sonraki sürümler tanımlayacak.

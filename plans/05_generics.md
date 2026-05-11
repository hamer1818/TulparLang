# Plan 05 — Generics (parametric polymorphism)

**Durum:** PROPOSED
**Tahmin:** 5-8 PR (incremental rollout)
**Risk:** Yüksek — type system overhaul; AST + tip çıkarımı + AOT lowering 4-5 noktada etkilenir
**Mottoya katkı:** Python kolay (no-repeat code) + C kadar hızlı (monomorphize → no runtime cost)

## Hedef

Kullanıcı şunu yazabilsin:

```tpr
func first<T>(array xs): T {
    return xs[0];
}

int n = first<int>([1, 2, 3]);
str s = first<str>(["a", "b"]);
```

Statik tip kontrolü generic parametre `T`'yi her instantiation'da
çözer. AOT path'inde generic her concrete-type kombinasyonu için
ayrı bir monomorphized fonksiyon emit eder — yani `first<int>` ve
`first<str>` LLVM IR'da iki ayrı fonksiyon olur, runtime branching
yok. Bu C++ template / Rust generic / Go generic ile aynı
"zero-cost abstraction" felsefesi.

VM path'inde ya aynı monomorphization yapılır (kolaylık) ya da
boxed `VMValue` ile generic instance tek fonksiyon olur (daha az
kod şişmesi, performans cost'u tolerable çünkü VM zaten boxed).

## Mevcut durum

- AST'de `FunctionDecl` türü yok: `(name, params, return_type, body)`
  — type parametre slot'u yok.
- `parse_function_decl` (`src/parser/parser.cpp:234` civarı) `:` ile
  dönüş tipi okuyor; `<T>` formu parse hatası veriyor.
  ```
  $ ./tulpar.exe -c 'func f<T>(T x): T { return x; }'
  hata: ayrıştırma hatası: fonksiyon adından sonra '(' bekleniyordu
  ```
- `src/typeinfer/` (typeinfer pre-pass) hiçbir generic kavramı yok.
- AOT codegen (`src/aot/llvm_backend.cpp`) her fonksiyon için tek bir
  LLVMValueRef üretiyor; monomorphization slot yok.

## Tasarım kararları (RFC — tartışılacak)

### Syntax

```tpr
func name<T1, T2, ...>(typed_args): RetType { ... }
identity<int>(42)                  // explicit type arg
identity(42)                       // inference (ileri faz)
```

- `<` ve `>` parse'da çakışma yapar (`a < b > c` ifadesi). Çözüm:
  generic instantiation sadece **identifier + `<`** desenini
  görünce parse edilir; `parse_postfix`'te disambiguation gerekir.
- Alternatif: `func name[T](...)` (Scala / Kotlin tarzı). Daha az
  çakışma ama az tanıdık. Bu RFC köşeli `<...>` öneriyor.

### Tip parametrelerinin sınırı (constraint)

MVP: trait sistemi yok, parametre constraintsiz. `T` herhangi bir tip
olabilir. Sonraki faz: `func f<T: Comparable>(...)` (büyük iş).

### Monomorphization stratejisi

- **AOT:** her unique instantiation set (e.g. `(int)`, `(str)`,
  `(json)`) için ayrı LLVMValueRef. İsim mangle: `name__T_int`,
  `name__T_str`. Call sites compile-time'da resolve eder.
- **VM:** İlk MVP'de aynı monomorphization. Sonraki faz: boxed
  `VMValue` ile tek-fonksiyon (kod şişmesini azaltır).

### Tip çıkarımı (inference)

MVP'de **explicit only**: `identity<int>(42)`. Inference (`identity(42)`
otomatik T = int) ileri faz — Hindley-Milner unification gerekir
ve mevcut typeinfer pass'i basit (akış-duyarsız).

### Recursive generics ve özyineleme

`func f<T>(T x) { return f<T>(x); }` çalışmalı. Monomorphization
tablosunda recursion guard gerekir.

## Adımlar (PR sıralama)

### PR 1 — AST + Parser

- `parser/ast_nodes.hpp`: `FunctionDecl` türüne
  `std::vector<std::string> type_params` ekle.
- `parser.cpp::parse_function_decl`: `name` token'ından sonra
  `<` görürse `T1, T2, ...` parse et, `>` ile kapat.
- `parser.cpp::parse_postfix`: `identifier<T1, T2>(args)` desenini
  call site olarak parse et. Disambiguation: `<` görünce
  *speculatively* try; eğer matching `>` + `(` bulamazsa rewind
  (eski binary expression davranışı).
- Test: `tests/typeinfer/pass/generics_parse.tpr` — sadece
  parse'in geçtiğini doğrula.

### PR 2 — Typeinfer + identity check

- `typeinfer/`'a basit generic kayıt mekanizması: her function
  decl'in `type_params` listesi alınır, gövdedeki `T` token'ları
  type-param olarak işaretlenir.
- Call site `identity<int>(42)`: arg sayısı + return type pre-pass
  uyarısı şu ana kadarki sviye'de kalır.
- Test: pass/fail dosyaları, kötü-arg-sayısı uyarısı.

### PR 3 — AOT monomorphization

- `llvm_backend.cpp`: generic function decl'leri "şablon" olarak
  ertele (LLVMFunctionRef create etme).
- Call site `identity<int>(...)`: instantiation key (`name + types`)
  ile lookup; cache'de varsa kullan, yoksa şablonu instantiate et
  → tip-yer-değiştirilmiş AST → normal codegen → cache'e yaz.
- Recursion guard: in-progress instantiation set.
- Test: `examples/generic_identity.tpr` AOT compile + run.

### PR 4 — VM monomorphization

- `vm/compiler.cpp`: aynı strateji bytecode için.
- Veya: boxed-only path (tek fonksiyon, VMValue dispatch).
- Karar burada verilir; RFC'de PR 4'e ertelendi.

### PR 5 — Sınırlı inference

- Tek-tip-parametreli fonksiyonlar için: ilk arg'ın static tipinden
  T çıkar (basit, akış-duyarsız).
- `identity(42)` → `identity<int>(42)` rewrite parse sonrası,
  codegen öncesi.

### PR 6 — Constraint syntax (`T: Trait`)

- Trait kavramı (interface). Büyük iş — Plan 06 olabilir.

### PR 7 — Generic struct types

- `record Box<T> { T value; }` — Plan 04 (native struct) ile
  birleştirilmeli.

### PR 8 — Documentation + örnekler

- `examples/generics_*.tpr`, `tulparlang.dev/docs/generics.md`.

## Açık sorular

1. Syntax: `<T>` mu `[T]` mı? Default önerim: `<T>` (tanıdık).
2. Inference scope: MVP explicit-only mı? Önerim: evet, PR 5'te ekle.
3. Monomorphization vs boxed in VM: PR 4'te karar.
4. Variance (covariance, contravariance)? MVP'de yok.
5. Higher-kinded types (`func<F<_>>(...)`)? Asla yapmayız muhtemelen.

## Risk değerlendirmesi

- **Yüksek:** Parser disambiguation (`<` operator vs generic). C++ bunu
  template `<>` ile çözüyor — bizde de gerekir. Yanlış yaparsak mevcut
  legal kod kırılır.
- **Yüksek:** Monomorphization explosion. 5 type-param × 4 type-class =
  20 instance her bir generic için. AOT IR boyutu şişer; LTO bunu
  azaltır ama dikkat gerekir.
- **Orta:** Type inference yanlış çıkarım ürettiğinde hata mesajı
  okunaklı olmazsa "Python kolay" motoyusu sarsılır. Rust-stil hata
  mesajları temeli (PR #43) yardımcı olur.
- **Düşük:** Performans regression — monomorphization zero-cost, AOT
  benchmark'ları yansıtmamalı.

## İlgili işler

- Plan 04 (`04_native_structs.md`) — generic struct için ön-koşul.
- Plan 01 (method calls) — generic method'lar (`obj.method<T>(x)`)
  için method-receiver semantiği şart.
- typeinfer altyapısı (PR #32) — generic constraint check oraya iner.

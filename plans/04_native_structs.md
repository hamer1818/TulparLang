# Plan 04 — Native Record/Struct Types (Unboxed)

**Durum:** COMPLETED (2026-05, PR'lar #48 / #49 / #50 + sonraki polish
turları) — `struct Point { int x; int y; }` typed alloca + GEP yolundan
LLVM IR'da native i64×N olarak temsil ediliyor; typed fn arg/return,
`print(struct)` formatter, `benchmarks/struct_sum` eklendi. Bkz. STATUS
§ "Çekirdek dil".
**Tahmin:** 4-6 PR (parser → AST → typeinfer → AOT codegen → VM → benchmark/cleanup)
**Risk:** Yüksek (codegen, runtime ABI ve typeinfer üçlüsünü birden değiştirir)
**Mottoya katkı:** C kadar hızlı (somut, ölçülebilir kazanç)

## Hedef

Kullanıcı `struct Point { int x; int y; }` yazdığında, derleyici bu
tipi LLVM IR'da **native i64×N struct** olarak temsil etsin (her field
`VMValue` 16 byte değil, native genişliğinde — int 8 byte, bool 1
byte, vb.). Şu anki "her şey VMValue" boxing'inin mottoya bağlı olarak
en pahalı yeri burası.

Sonuç: `Point p = { x: 1, y: 2 }; p.x = p.x + 1;` üzerinde
- Bellek ayak izi: 32 byte (VMValue×2) → 16 byte (i64×2)
- Field access: `vm_get_element(VMValue, VMValue)` runtime call → tek
  `getelementptr` + `load`
- Optimizer (LLVM O3): inline + scalar replace + register'a yerleştirme

Beklenen performans (`benchmarks/`'a yeni "PointArray sum" testi):
Tulpar AOT şu an Rust ile 4-5× yavaş; struct path'te 1.5-2×'e
yaklaşması beklenir.

## Mevcut durum (kaynak)

### Var olan altyapı

- [src/parser/ast_nodes.hpp:346-355](../src/parser/ast_nodes.hpp#L346-L355)
  `TypeDecl` struct'ı **zaten tanımlı**:
  ```cpp
  struct TypeDecl {
    std::string name;
    std::vector<std::string> field_names;
    std::vector<DataType> field_types;
    std::vector<std::optional<std::string>> field_custom_types;
    std::vector<std::unique_ptr<ASTNode>> field_defaults;
    SourceLocation loc;
  };
  ```
  Ama parser'da `struct ...` keyword'ü için bir kural **yok** —
  TypeDecl üreten bir parse rule mevcut değil.

- [src/aot/llvm_types.cpp:21-35](../src/aot/llvm_types.cpp#L21-L35)
  VMValue tip tanımı: `{i32 type, [4 x i8] pad, i64 payload}` — 16
  byte tagged union.

- [src/aot/llvm_backend.cpp:4323-4331](../src/aot/llvm_backend.cpp#L4323-L4331)
  `native_codegen_supports_body()` zaten **TYPE_INT, TYPE_BOOL,
  TYPE_UNKNOWN** için unboxed yola izin veriyor (EKSIKLER #14, #15, #16
  fix'leri). Aynı altyapı struct field'larına genişletilebilir.

- [src/vm/vm.hpp:215-220](../src/vm/vm.hpp#L215-L220) `ObjArray` RC
  şablonu — heap struct'ları için aynı RC head + payload deseni
  kullanılabilir.

### Eksik

1. `struct` keyword parsing
2. AST_STRUCT_DECL düğümü ve typeinfer'ın bu düğümle ne yapacağı
3. AOT'da `LLVMStructType` üretimi ve field GEP
4. Heap-allocated struct (referans by-default mı yoksa value mi?)
5. VM bytecode op'ları: `OP_STRUCT_NEW`, `OP_STRUCT_GET_FIELD`,
   `OP_STRUCT_SET_FIELD`
6. Garbage collection / ARC entegrasyonu
7. Print/serialize: `print(p)` → `Point { x: 1, y: 2 }` formatı

## Tasarım kararları (kritik)

### Karar 1: Value vs Reference semantiği

**Iki seçenek:**

(a) **Value type:** `Point p = q;` derin kopya. Function pass'inde
   stack-allocated, by-value kopyalanır. C/Rust style.

(b) **Reference type:** Heap-allocated, `Point p = q;` aynı objeyi
   paylaşır (reference count++). Java/Python/JS style.

**Önerim (a) value type.** Sebepler:
- Mottoya uygun (C kadar hızlı = stack alloc, no ARC overhead)
- Mevcut `json` zaten reference semantik sağlıyor (ObjArray RC) →
  iki dünyalı; struct deterministik, json dinamik.
- "Python kolay" tarafında küçük bir eziklik: kullanıcı `func mutate(Point
  p) { p.x = 5 }` yazınca dışardan görmeyecek. **Düzeltme:** explicit
  pointer/reference syntax — `func mutate(Point& p)` veya `*Point`. Plan
  04 v1'de **yok**, value-only başla; v2'de eklenir.

**Performans tarafından:** struct büyürse (>16 byte) by-value pass
maliyetli olur. **Kural:** sizeof(struct) > 16 byte ise compiler
otomatik by-pointer geçirsin (LLVM `byval` attribute zaten var). User
syntax'i etkilenmez.

### Karar 2: Heap'a alınma

`Point p = ...; json arr = []; push(arr, p);` — struct heap'e
gitmek zorunda. Bu durumlarda ARC'lı bir `ObjStruct` wrapper:

```cpp
typedef struct {
  Obj obj;            // Mevcut RC header
  TypeId type_id;     // Hangi struct?
  uint32_t size;      // Toplam byte
  uint8_t data[0];    // Flexible array — field'lar buraya
} ObjStruct;
```

`vm_array_push(arr, point_value)` çağrısı struct'ı heap'e kopyalar,
VMValue'a `OBJ_STRUCT` tag + ObjStruct* payload yazar. Sonraki
`arr[0].x` access'i runtime'da unbox edilip native field load yapar.

### Karar 3: Forward declaration / ordering

`struct A { B b; }` ve `struct B { int x; }` — sıra önemli mi? **C
gibi:** B önce tanımlanmalı, ya da forward decl `struct B;`. Önerim:
ilk versiyonda forward decl yok — ileri ihtiyaç olursa eklenir.

### Karar 4: Methods

Plan 01 (method calls) struct'larla doğal çakışıyor. `func Point.norm():
float` syntax sugar'ı Plan 01 Faz 3'te öneriliyor. **Bu plan onsuz
tamam** — method'lar serbest fonksiyon olarak yazılır, Plan 01'in
resolver'ı receiver tipini görür ve dispatch eder.

## Yaklaşım: Altı PR

### PR 1 — Lexer + parser + AST

**Hedef:** Sadece parse'lansın, codegen ve typeinfer noop. `tulpar fmt`
struct deklarasyonunu doğru bassın.

- [src/lexer/](../src/lexer/) `struct` keyword'ünü ekle (zaten reserve
  olabilir, kontrol et).
- [src/parser/parser.cpp](../src/parser/parser.cpp) yeni
  `parse_struct_decl()`:
  ```
  struct Name { type1 field1; type2 field2; ... }
  ```
  → `TypeDecl` AST düğümü üret.
- [src/parser/ast_nodes.hpp](../src/parser/ast_nodes.hpp) — TypeDecl
  zaten var, sadece variant'a eklendiğini ve AST_STRUCT_DECL enum
  konstantının bulunduğunu doğrula. Yoksa ekle.
- AOT codegen: `case AST_STRUCT_DECL: /* ignore */`. VM compiler aynı.
- Typeinfer: TypeDecl'i context'e kaydet (`ctx->struct_types[name] =
  field_layout`).

**Doğrulama:** `tulpar typecheck struct_decl.tpr` exit 0; `tulpar build
struct_decl.tpr` boş program üretir; struct kullanılmıyor.

### PR 2 — Type system entegrasyonu

**Hedef:** typeinfer struct'ları tip olarak tanısın; kullanıcı
`Point p;` yazabilsin (ama field access yok).

- typeinfer: variable decl `<TypeName> ident;` → DataType
  `TYPE_CUSTOM(name)` (henüz unboxed değil, sadece tag).
- `is_unknown` listesinden TYPE_CUSTOM **çıkarılmaz** (henüz field
  bilgisi codegen'e yansımıyor); Plan 03 PR 1'le hizalı.
- TypeDecl validation: aynı isim çift tanım, isimsiz/tipsiz field, vs.

### PR 3 — AOT codegen native path (value type, küçük struct)

**Hedef:** `struct Point { int x; int y; }` LLVM'de `%struct.Point =
type { i64, i64 }` olarak emit edilsin; field access GEP olarak
lower'lansın.

- [src/aot/llvm_types.cpp](../src/aot/llvm_types.cpp): TypeDecl'i
  LLVMStructType'a çeviren helper (`build_struct_layout(TypeDecl*)`).
- [src/aot/llvm_backend.cpp](../src/aot/llvm_backend.cpp):
  - `case AST_STRUCT_DECL`: type'ı backend->custom_types map'ine kaydet.
  - VAR_DECL with TYPE_CUSTOM(name) → alloca custom struct type
    (entry-hoisted, EKSIKLER #17/#18 lessons learned).
  - ASSIGNMENT `p = struct_literal { x: 1, y: 2 }` → struct literal
    AST düğümü gerekli (PR 1'de eklenir); codegen field-by-field GEP
    + store.
  - Field access `p.x`: parse_postfix:864-872'deki ArrayAccess
    desugaring zaten devrede. Codegen ArrayAccess gördüğünde:
    1. left value tipini sorar — TYPE_CUSTOM(name)?
    2. index string literal mı?
    3. ise `LLVMBuildStructGEP2(builder, struct_ty, ptr, field_idx,
       "p.x")` + load
    4. değilse mevcut `vm_get_element` runtime çağrısına düş.
  - Field set: aynı, store.
- `native_codegen_supports_body()`: TYPE_CUSTOM(name) destekle —
  ama sadece body'de struct'ın **tüm field'ları int/bool** ise
  (string/array/nested struct ilk iterasyonda dışarıda).

**Doğrulama:** `examples/16_structs.tpr` aşağıdaki örnek AOT'da çalışsın:
```tpr
struct Point { int x; int y; }
func main(): int {
    Point p = { x: 3, y: 4 };
    return p.x * p.x + p.y * p.y;  // 25
}
```

### PR 4 — Heap allocation + json interop

**Hedef:** `push(arr, p)` çalışsın, `arr[0]` okunduğunda Point geri
gelsin.

- Runtime: yeni `runtime/tulpar_struct.{h,cpp}` —
  `tulpar_struct_alloc(type_id, size)` ARC'lı allocate.
- AOT: VMValue'a yeni tag `OBJ_STRUCT`. Struct value'sunu json'a (veya
  user fonksiyonuna by-value değil by-pointer) geçirirken otomatik
  promote: stack struct → heap kopyası + tag.
- VM: aynı.
- Field access: VMValue tag'i OBJ_STRUCT ise unbox + native GEP. Diğer
  tag'lar mevcut `vm_get_element` yoluna devam.

**Bu PR riskli** — ABI tutarlılığı, RC dengesizliği, GS-cookie
overrun (EKSIKLER #18 deja vu) tehlikeleri var. Test coverage agresif
olmalı.

### PR 5 — VM bytecode + REPL

- `OP_STRUCT_NEW` (operand: type_id) — yeni struct, default değerlerle
- `OP_STRUCT_GET_FIELD` (operand: field_idx)
- `OP_STRUCT_SET_FIELD` (operand: field_idx)
- VM compiler bu op'ları emit etsin (TYPE_CUSTOM gördüğünde).
- REPL `Point { x: 1, y: 2 }` literal'ini interactive'de kabul etsin.

### PR 6 — print + benchmark + temizlik

- `print(p)` çıktısı: `Point { x: 1, y: 2 }`. Format helper:
  `print_value_inline` switch'e OBJ_STRUCT case'i ekle.
- `lib/test.tpr`: `assert_struct_eq(actual, expected)` helper'ı.
- Benchmark: `benchmarks/struct_sum/`:
  ```tpr
  struct V3 { int x; int y; int z; }
  func sum_v3(int n): int {
      V3 acc = { x: 0, y: 0, z: 0 };
      for (int i = 0; i < n; i = i + 1) {
          V3 v = { x: i, y: i*2, z: i*3 };
          acc.x = acc.x + v.x;
          acc.y = acc.y + v.y;
          acc.z = acc.z + v.z;
      }
      return acc.x + acc.y + acc.z;
  }
  ```
  C, Rust, Go, Tulpar (boxed json baseline), Tulpar (struct) zamanları
  RESULTS.md'ye yazılsın.
- Eski "everything is json" workaround örnekleri varsa (örn.
  `examples/10_for_in_test.tpr` benzeri) struct'a göç eden alternatif
  versiyon ekle.

## Edge case'ler

1. **Recursive struct:** `struct Node { Node next; int val; }` —
   value type'ta sonsuz boyut. **Yasak** — typeinfer hata versin
   ("recursive struct requires reference"). v2'de pointer/ref
   syntax'iyle açılır.
2. **Empty struct:** `struct Empty {}` — geçerli mi? C'de evet (1
   byte). Plan'da: hata vermesin, 0 byte LLVM struct'ı.
3. **Field count:** Çok büyük struct (>32 field)? Performans
   etkilenmez ama derleme zamanı yavaşlar. Limit yok.
4. **Default values:** `struct P { int x = 5; }` — TypeDecl'de
   `field_defaults` zaten var. Constructor olmadan declaration nasıl
   default'larla doldursun? Initializer-list olmadan
   `Point p;` → tüm default'lar uygulansın; `Point p = { x: 1 }` →
   y default'a düşsün, missing default = 0/false/null.
5. **Field reorder vs ABI:** LLVM struct field sırası declaration
   sırası. Bunu bozma; debugger ve manuel layout'lar etkilenir.
6. **Forward decl yok ama mutual reference?** `A` `B`'yi field olarak
   alıyor ve tersi. Yasak (recursive olur). Hata mesajı net olmalı.
7. **TypeDecl & json çakışması:** `Point p = some_json;` — runtime
   coerce mi, type error mi? Önerim: type error (strict). Açık coerce
   için `to_struct(json, "Point")` builtin'i ileride.

## Doğrulama

- **Unit:** `tests/structs.test.tpr` — declaration, init, get/set,
  pass-by-value, mutation, equality.
- **Integration:** `examples/16_structs.tpr` mevcut suite'e dahil.
- **Performans:** `benchmarks/struct_sum/` — Tulpar struct path'i
  Tulpar boxed json path'inden ≥2× hızlı, C/Rust'tan ≤2× yavaş hedefi.
  RESULTS.md'ye yansıt.
- **Regression:** Mevcut `examples/*.tpr` (json/array kullanan tüm
  setler) her PR sonrası 100% pass.

## Açık sorular

- **`struct` mü `record` mü `class` mı?** Önerim: `struct` — C/Rust/Go
  uyumlu, kafa karıştırmıyor. `class` "method'lu" çağrıştırıyor; bu
  fazda olmayacak.
- **Field access syntax: `p.x` mı, `p[x]` mı?** İkisi de çalışmalı
  (parser zaten `.` → `["..."]` desugar ediyor). Ama default
  pretty-print `.` syntax'i kullansın.
- **Tip ihracı:** `import "lib"` ile gelen modülün struct'ları
  görünür mü? Faz 1'de evet — top-level TypeDecl module export.
  `as alias` (Plan 01) ile `lib.Point` syntax'i gerekecek mi?
  Ileride; şimdilik unmangled global.
- **Generics?** `struct Box<T> { T value; }` — bu plan içinde **yok**.
  Generics ayrı bir Plan 05'e ileriden ele alınır.
- **Heap promotion karar verme:** Compiler hangi durumda struct'ı
  heap'e taşımalı? Statik analiz mi, dinamik mi? PR 4'te basit kural:
  json'a push edilirken / variadik fn arg olurken / function
  return-by-value 16 byte üstü ise. v2'de escape analysis.

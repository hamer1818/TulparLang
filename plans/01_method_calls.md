# Plan 01 — Real Object Method Calls (`p.greet("x")`)

**Durum:** PROPOSED
**Tahmin:** 2-3 PR
**Risk:** Düşük-Orta (parser tek nokta + codegen fallback; runtime değişikliği yok)
**Mottoya katkı:** Python kolay (idiomatic OOP-style yazım)

## Hedef

Kullanıcı `p.greet("Ali")` yazabilsin; `p` modül alias'ı değil, normal
bir json/struct/değer olduğunda çağrı, `greet(p, "Ali")` formuna
çözünsün ve mevcut serbest fonksiyon dispatch'i üzerinden çalışsın.

Şu anki davranış (CLAUDE.md'de notlu): `parse_postfix` her
`<id1>.<id2>(args)` ifadesini koşulsuz `<id1>__<id2>(args)` olarak
yeniden yazıyor. Eğer `id1` bir modül alias değilse (`import "x" as id1`
ile gelmediyse), mangled isim çözünmüyor → codegen "fonksiyon
bulunamadı" hatası veriyor.

## Mevcut durum (kaynak)

- [src/parser/parser.cpp:828-872](../src/parser/parser.cpp#L828-L872)
  `parse_postfix`. Lines 846-861: `<Identifier>.<Identifier>(args)` kalıbı
  görüldüğünde mangled `FunctionCall("id1__id2", args)` üretiyor. Lines
  864-872: hatalı bir bağlamda (paren yoksa) `obj.field` kalıbı
  `obj["field"]` ArrayAccess olarak desugarlanıyor (alan okuma için bu
  düzgün çalışıyor).
- [src/parser/import_alias.cpp:85-118](../src/parser/import_alias.cpp#L85-L118)
  `apply_import_alias`. Modül top-level fonksiyon adlarını `<alias>__`
  ile prefix'liyor; modül-içi çağrıları da aynı anda yeniden yazıyor.
  Bu pas parser sonrası bir AST transform.
- [src/parser/parser.cpp:828-872](../src/parser/parser.cpp#L828-L872)
  parse_postfix'in alias bilgisi yok — parser şu an `m.func()` ile
  `obj.method()` arasında ayrım yapamıyor; ikisi de mangle ediliyor.
- Birinci-sınıf fonksiyonlar / `OP_CALL_INDIRECT` **yok**. JSON
  değerleri fonksiyon referansı tutamıyor (bytecode.hpp:79-81).
- typeinfer json field'larını track etmiyor (typeinfer.cpp:266-282
  `TYPE_VOID` döndürüyor).

## Tasarım kararı

İki temel yaklaşım inceledim:

1. **Parse-time alias scan + branch:** Parser dosyayı önceden tarayıp
   `import "x" as foo` ile gelen alias setini çıkarsın. parse_postfix bu
   sete bakıp aliassa mangle'lasın, değilse `method(obj, args)`
   biçimine çevirsin. **Sorun:** parser tek geçişli; alias scan ek bir
   pas gerektiriyor, ya da postfix kararı sonraya ertelenmeli.

2. **Hybrid resolver (önerim):** Parser yeni bir AST düğümü
   üretir — `MethodOrAliasCall { receiver, name, args }`. Codegen / VM
   compile zamanında elindeki tam fonksiyon tablosuna bakar:
   - Eğer `<receiver_name>__<name>` fonksiyonu tanımlıysa → modül alias
     çağrısı (mevcut davranış)
   - Aksi halde `<name>` fonksiyonu varsa ve arity'si `1 + len(args)`
     ise → `name(receiver, args...)` çağrısı
   - İkisi de yoksa → mevcut "function not found" hatası

   **Avantaj:** parser tek geçişte kalıyor; alias kararı codegen'in zaten
   yaptığı sembol araması üzerinden veriliyor; mevcut modül-alias
   davranışı aynen korunuyor (geriye uyumlu).

## Yaklaşım: Hybrid resolver, üç fazda

### Faz 1 (MVP, 1 PR) — Parse + AOT codegen + VM compiler

**Yeni AST düğümü:** `MethodOrAliasCall`
- `unique_ptr<ASTNode>` receiver (`Identifier` veya genel ifade)
- `std::string` method_name
- `std::vector<unique_ptr<ASTNode>>` args
- `SourceLocation` loc

[src/parser/ast_nodes.hpp](../src/parser/ast_nodes.hpp) içine eklenecek.

**Parser değişikliği** ([src/parser/parser.cpp:846-861](../src/parser/parser.cpp#L846-L861)):

```cpp
if (check(TOKEN_LPAREN)) {
  // Eski: koşulsuz mangle
  // Yeni: receiver bare Identifier ise MethodOrAliasCall, değilse
  //       (örn. zincirleme `a.b.c()`) doğrudan FunctionCall(c, [a.b])
  advance(); // '('
  auto args = parse_argument_list();
  expr = std::make_unique<ASTNode>(
      MethodOrAliasCall(std::move(expr), field.value(),
                        std::move(args), dot_loc));
  continue;
}
```

**AOT codegen** ([src/aot/llvm_backend.cpp](../src/aot/llvm_backend.cpp)
içinde yeni `case AST_METHOD_OR_ALIAS_CALL`):

```c
// Çözüm sırası: sembol tablosuna bak.
if (receiver is bare Identifier "id1") {
    if (function_exists("id1__methodname")) {
        // Modül alias çağrısı — mevcut FunctionCall codegen'ine devret
        emit_call("id1__methodname", args);
        break;
    }
}
if (function_exists("methodname")) {
    if (arity_matches(methodname, 1 + args.size())) {
        // Method call — receiver'i ilk arg olarak ekle
        emit_call("methodname", [receiver] + args);
        break;
    }
}
report_error("Method/function 'methodname' not found for receiver");
```

**VM compiler** ([src/vm/compiler.cpp](../src/vm/compiler.cpp)) — aynı
çözüm sırası, sadece OP_CALL emit eder.

**Sembol tablosu erişimi:** AOT'da `predeclare_func_signature` zaten
tüm fonksiyonların imzasını biliyor; codegen sırasında lookup kolay.
VM compiler'ında `function_table` üzerinde isim/arity araması yap.

### Faz 2 (1 PR) — typeinfer integration

typeinfer'da `MethodOrAliasCall` için:
- Receiver tipini infer et
- Eğer receiver `TYPE_CUSTOM` (struct) → `<TypeName>_<methodname>` veya
  `<methodname>_<TypeName>` adında specialized fonksiyon ara (konvansiyon
  kararını burada keserim — Plan 04 ile birlikte düşünmek lazım)
- Genel durumda Faz 1'deki mantığı tekrar et, sadece warning üret
- `--strict` modunda (Plan 03) çözünmeyen method call'ı hata yap

### Faz 3 (opsiyonel, 1 PR) — Self-conventions

Bir `self` parametre adı için syntactic sugar: `func Point.greet(str x)`
yazımı `func greet_Point(json self, str x)` olarak desugar edilsin.
Bu zaten Plan 04 (struct types) ile birlikte daha doğal.

## Edge case'ler

1. **Zincirleme: `a.b.c()` — Faz 1 nasıl davranır?**
   `a.b` ArrayAccess olarak desugarlanıyor (parse_postfix DOT branch).
   Sonra `.c(...)` görüldüğünde receiver artık ArrayAccess; bare
   Identifier değil. Parser doğrudan `FunctionCall(c, [arrayaccess])`
   üretmeli (yani method call yolu). **Test:** `obj.x.greet()` =
   `greet(obj.x)`.

2. **`obj.method` (paren yok) — alan okuma**
   parse_postfix:864-872 zaten bunu `obj["method"]` desugar ediyor.
   Davranış değişmeli mi? İlk faz için **değiştirme** — first-class
   functions yok zaten, alanı okumanın anlamı yok ama hata da vermesin.

3. **Modül alias + method conflict**
   `import "math" as m;` ve sonra `m.sin(x)`. `m__sin` mevcut, yani
   resolver onu seçer. Ama kullanıcı bir `func sin(json self, ...) `
   tanımlasaydı? Resolver `m__sin`'i öncelikli görür — doğru davranış.
   Tersi durum (bare `m` değişkeni var, alias yok) parser zaten module
   alias varsayıyordu; yeni resolver `<m>__sin` yoksa method call'a
   düşecek — düzelmiş davranış.

4. **`new Foo()` benzeri constructor sözdizimi yok**
   Faz 1'de constructor yok; struct yokken anlamsız. Plan 04 ile gelir.

5. **Variadik fonksiyonlar (`print`)**
   Method olarak çağrılabilir mi? `obj.print()`? Resolver `print`'i
   bulur, arity check `print` için variadik — ya `print` özel
   listede tut ve "method olamaz" de, ya da `print(obj)` çağrısına
   çevir. Önerim: variadik builtin'leri method-call hedefi olmaktan
   çıkar (basit blacklist).

## Doğrulama

1. **Yeni test dosyası:** `tests/method_calls.test.tpr` — `lib/test.tpr`
   kullanarak:
   - Bare Identifier receiver + serbest fonksiyon var → method dispatch
   - Bare Identifier receiver + modül alias eşleşmesi var → alias yolu
   - Zincirleme `a.b.c()`
   - Variadik fonksiyon method olarak çağrılırsa hata
   - Olmayan method → "not found" mesajında receiver tipi de geçsin
2. **Mevcut suite:** `./build.sh test` 100% pass kalmalı; özellikle
   `examples/07_modules.tpr` ve `tulpar_api_demo.tpr` (alias ağır
   kullanım) regresyon vermesin.
3. **Yeni example:** `examples/16_methods.tpr` — küçük "Point" gibi
   gösterim (json üzerinde method call), README/QUICKSTART'a 3-4
   satırlık snippet.

## Açık sorular

- Self parametre adı konvansiyonu? `self` rezerv kelime mi olacak,
  yoksa sıradan bir parametre adı olarak mı geçecek? Şu an rezerv
  değil; ileride Faz 3'te değişebilir.
- Method ismi koliziyonu — iki farklı modülde aynı isimle method
  varsa hangisi seçilir? Faz 1'de "ilk bulunan" — typeinfer Faz 2'de
  receiver tipine göre dispatch eder.
- `tulpar fmt` etkisi: yeni AST düğümü için pretty-printer eklenmeli
  ([src/fmt/](../src/fmt/) — kod henüz incelenmedi, format pas listesine
  ekle).

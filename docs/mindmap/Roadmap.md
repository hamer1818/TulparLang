---
tags: [moc, roadmap, gaps]
---

# Roadmap — Açık Eksikler

> **Güncel açık işler artık [TODO.md](../../TODO.md)'de tutuluyor** (işlenebilir
> kontrol listesi; biten madde silinip CHANGELOG'a geçer). Bu not tarihsel/anlatı
> bağlam için duruyor — çelişirlerse TODO.md ve [STATUS.md](../../STATUS.md) esastır.
> 3B motorun durumu: [[Scene3D]], editör: [[Editor]]. Test disiplini: [[Testing]].
> Bir sorun çıktığında ilk bakılacak yer: [[Tuzaklar]].

Tek doğruluk kaynağı [STATUS.md](../../STATUS.md) (🔴 kritik / 🟡 önemli / 🟢 nice-to-have). Çekirdek olgun; kalanlar çoğunlukla DB katmanı + polish.

## ✅ Sahne editörü — TameEngine (2026-08-24 → 2026-08-28)
Ayrıntı: [[Editor]]. Kısa hâli — editör artık kendi başına kullanılabilir bir
araç:
- **Menü**: şerit açılır menülere döndü (Dosya / Duzen / Gorunum / Yardim);
  her komutun kısayolu kendi satırında, iş görmeyen komut soluk.
- **Dosya**: aç / farklı kaydet / geri yükle + **kurtarma dosyası** (kapatırken
  kaybolan iş). Geri-al geçmişi dosya değişince sıfırlanıyor.
- **Yerleşim**: paneller yuvalara takılı, **başlıktan sürüklenip** taşınıyor,
  yuvanın sınırı **ve aynı yuvadaki panellerin arasındaki sınır** çekiliyor,
  kapatılıp açılıyor; yerleşim + paylaşım + ölçek **kalıcı**.
- **Pencere**: boyutlandırılabilir, büyütülebilir, F11 tam ekran.
- **İçerik**: doku, ışık, ses, kamera paneli, bölge eylemleri (`ZACT_*`),
  `ACT_NEXT` ile veriyle bölüm ilerletme, pano (bölümler arası), tümünü seç,
  kısayol listesi (H).
- **Denetim** büyüdü: ölü bölge, etiketsiz bölge, kamera hedefi yok/silinmiş,
  iki oyuncu, iş görmeyen bitince eylemi, eksik model/ses dosyası.
- Bir düzine **sessiz veri kaybı** bulunup kapatıldı (tablo: [[Editor]]).

Açık kalanlar: yuva içinde SIRALAMA / sekme / yüzen pencere (bilerek
yapılmadı → [[Decisions]]; paylaşım artık ayarlanabiliyor, sıra değil),
panel başlığında sürükleme hayaleti yok (yalnız hedef vurgusu var).

## 🟢 DB katmanı (paralel-read kapandı)
- ✅ `db_open` WAL + busy_timeout varsayılan (yapıldı, write 2.3×). → [[SQLite and DB]]
- ✅ `db_last_insert_id`/`db_error` codegen imza fix (yapıldı). → [[AOT Backend]]
- ✅ **Thread/bağlantı-başına DB handle** — handle artık descriptor index'i, her thread kendi `sqlite3` bağlantısını lazily açar → WAL altında paralel read (2026-06-18). → [[SQLite and DB]]
- ✅ **per-connection `cache_size`** (2026-06-22): `db_apply_pragmas` her bağlantıya `PRAGMA cache_size=-2048` (2 MiB, `TULPAR_DB_CACHE_KB` ile tunable) — per-thread bağlantı modelinde RSS'i sınırlar. → [[SQLite and DB]]
- ✅ **prepared-statement cache** (2026-06-22): `db_query` thread-local, bağlantı-başına bounded (64, FIFO) statement cache; aynı SQL'i `sqlite3_reset` ile yeniden kullanır (prepare+finalize yerine). `prepare_v2` şema değişiminde auto-reprepare; `db_close` artık `sqlite3_close_v2` + bu thread'in cache'ini finalize eder. → [[SQLite and DB]]

## ✅ Wings — çözülen bug (2026-06-18)
- **pool'da ~%1.1 sahte 404 → düzeltildi.** Kök neden `toString()`'in non-TLS paylaşılan buffer'ıydı; `thread_local` yapıldı. Pool/evented artık 0 hata. → [[Wings Serve Modes]]

## ✅ Validation şema genişletme (2026-06-22)
- **Zengin obje-spec kısıtları**: str min/max + regex, sayı min/max, dizi öğe-tipi + uzunluk, iç içe obje (dotted path), opsiyonel kısıtlı alanlar (`age?`). `_wings_validate`/`_wings_type_ok` (`lib/wings.tpr`), 12/12 `tests/validation.test.tpr` (commit 0d268c7). → [[Wings Validation and Docs]]
- Karşılaştırma-yoğun olduğu için eski bir **codegen verify hatasını** yüzeye çıkardı (O3/InstCombine geçersiz `phi i1` üretiyordu) → O3 verify-fallback güvencesiyle kapatıldı (2026-06-22). → [[AOT Backend]]

## Framework paritesi (Express/Gin/FastAPI kıyası, 2026-06-22)
- ✅ **Global middleware zinciri (`use`/short-circuit)** — `use("mw")` ile kayıt; `_wings_dispatch_cached` başında sırayla çalışır (tek dispatch noktası → tüm serve modları). Middleware `func mw(req)`: `_status`'lı dict → kısa-devre, `{}` → devam; `req` mutate edilebilir. Boşken zero-cost. `examples/wings_middleware_test.tpr` (401/200 + req.user canlı doğrulandı). Saf `.tpr`. → [[Wings]]
- ✅ **Route grupları / prefix (`group`)** — `group("/api/v1", "register_fn")` register_fn'in kaydettiği tüm path'leri prefix'ler; otomatik restore → nesting destekli. get/post/put/del/cached_get artık `_route_prefix + path` kullanır. `examples/wings_groups_test.tpr` (prefix/nesting/404 canlı doğrulandı). Saf `.tpr`. → [[Wings]]
- ✅ **Static dosya servisi (`static`)** — `static("/static", "./public")` bir dizini URL prefix'i altında sunar; **404-fallback** olarak (gerçek route'lar her zaman önce, static catch-all). Tek enjeksiyon: `_wings_build_404` başında `_wings_static_try` (3 serve modunu da kaplar). `..` traversal reddedilir, text asset'lerine doğru Content-Type. Binary asset'ler (PNG vb. gömülü-NUL) byte-exact round-trip eder. `examples/wings_static/` (canlı doğrulandı: css/html/png 200, eksik/traversal 404). → [[Wings]]
- ✅ **Tipli query-param erişimi (`query`/`query_int`/`query_bool`)** — varsayılan-fallback'li coerce; pagination/filtre tek satır. `_wings_has_key` + `toInt` (bozuk girdide 0) güvenli. `examples/wings_query_test.tpr` (canlı doğrulandı). → [[Wings]]
- ✅ **Response model / çıktı şekillendirme (`response_model`)** — `body_schema`'nın simetriği; başarılı çıktıyı bildirilen alanlara filtreler (password/`_internal` gibi sırlar düşer). Sadece bilinen kontrol key'leri (`_status`/`_raw`/`_content_type`/`_stream`) korunur, hatalar (>=400) bypass. Obje + obje-dizisi. `examples/wings_response_model_test.tpr` (canlı doğrulandı; `_internal` sızıntısı doğrulama aşamasında yakalanıp düzeltildi). → [[Wings]]
- ✅ **Multipart/form-data + dosya yükleme (`parse_multipart` builtin + `form`/`uploaded_files`)** — C parser (`runtime_bindings.cpp`, binary-safe `aot_memfind`), AOT'a `aot_split_ptr` deseninde 2-arg pointer-ABI builtin olarak bağlandı (header+decl+dispatch+builtins.cpp). Wings sarmalayıcıları: `form(req,name,fb)` text alanı, `uploaded_files(req)` → `[{name,filename,content_type,data,size}]`. Dosya `data`'sı ham byte (length-tracked → binary byte-exact). `examples/wings_upload_test.tpr` (canlı doğrulandı: text alan + text/PNG dosya byte-exact kaydedildi). → [[AOT Backend]]
- ✅ **Dependency injection (`depends`/`dep`)** — FastAPI `Depends` tarzı: `depends("fn")` route'a bağımlılık iliştirir, handler'dan önce çalışır, dönüş değeri `dep("name")` ile okunur; response dönerse kısa-devre (per-route auth/guard). Çözülen değerler thread-local `_wings_deps`'te (C: `global_needs_tls` whitelist'ine eklendi) → listen_pool'da sızmaz (20/20 paralel doğrulandı). `examples/wings_di_test.tpr`. → [[Wings]] [[AOT Backend]]

## ✅ Framework paritesi turu TAMAM (2026-06-22)
Express/Gin/FastAPI'ye karşı tüm kapatılabilir boşluklar kapandı: middleware (`use`), route grupları (`group`), static servis (`static`, binary dahil), tipli query-param (`query*`), response model (`response_model`), multipart/upload (`parse_multipart`+`form`/`uploaded_files`), DI (`depends`/`dep`). Biri hariç hepsi saf `.tpr`; multipart C builtin + DI bir satır C (TLS). Her biri canlı doğrulandı; her birine `examples/wings_*_test.tpr`.

## ✅ Wings polish (2026-06-22)
- ✅ **Banner tutarlılığı** — tüm serve modları (`listen`/`listen_pool`/`listen_evented`/`listen_async`) ortak `_wings_print_banner(port, suffix)` helper'ını çağırıyor; renkli kutu + route tablosu her modda aynı, mod Server satırında, sürüm v3.1'e güncellendi. → [[Wings Serve Modes]]
- ✅ **TLS yük altında test edildi** (`api_wings_tls`, OpenSSL 3.5.5) — 1000 istek/50 paralel → 0 hata ~663 req/s, keep-alive ~1.5ms, sunucu stabil, log temiz. → [[Performance]]

## 🟢 Dil/altsistem
- `obj.method(x)` gerçek obje method çağrısı desteklenmiyor. → [[Imports and Modules]]
- pkg registry bağımlılıkları (şu an yalnız `path:`). → [[Tooling]]

## 🎮 Tame — 2D oyun kütüphanesi (Faz 0-5 ✅, v3.10.0, 2026-07-12)
- ✅ Çekirdek (pencere/döngü) + çizim + klavye/fare + adlı renkler + **sprite/texture/font + ses/müzik (otomatik pompalama) + `run(update,draw)` yönetilen döngü (arena bracket) + `triangle`/`screenshot`** — `import "tame"`, vendored raylib, ayrı `libtulpar_tame.a` (yalnız import edilince linklenir). WSLg altında pencereli canlı doğrulama: screenshot piksel-kanıtı, 60 FPS, run() 480 kare stabil. → [[Tame]]
- ✅ Adlı gamepad API'si (2026-07-13): `gamepad_available/name/down/pressed/axis` — buton/eksen adla, donanımsız zarif yol. → [[Tame]]
- ✅ WASM hedefi (2026-07-13): `tulpar build --target=web` → .html/.js/.wasm (em++, ASYNCIFY, wasm/dist arşivleri; VMValue ABI'si wasm32'de sret). → [[Tame]] "Web hedefi"

## İlgili
[[Performance]] · [[SQLite and DB]] · [[Wings]] · [[Decisions]]

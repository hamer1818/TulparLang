// DERLEME ONBELLEGI — ortam degiskeni siniflari (src/aot/aot_cache.cpp okur,
// tests/onbellek_anahtari_kapisi.py denetler).
//
// KURAL (guvenli yon): derleyicinin gordugu HER `TULPAR_*` degiskeni
// onbellek anahtarina girer — adi ve degeri. Fazladan bir degisken yalniz
// gereksiz bir iska uretir; anahtarda EKSIK bir degisken ise eski ikiliyi
// "guncel" diye dondurur ve onu olcen her kapi yalan soyler (olculdu
// 2026-10-05: bir sabotaj olcumunu yapan ajan TULPAR_NO_FVER'i acti, eski
// mtime onbellegi "Cache hit" dedi ve kapi yanlislikla yesil cikti).
// Anahtarin DISINDA kalabilen yalniz bu dosyadaki uc sinif:
//
//   CALISMA_ZAMANI  derlenen PROGRAMIN (ya da duzenegin/motorun) okudugu,
//                   derleyicinin HIC okumadigi degiskenler. Kapi: bu adlar
//                   derleyici tarafinda (src/aot, src/typeinfer, src/parser,
//                   src/lexer, src/ext, src/main.cpp) bir dizgi sabiti olarak
//                   gecerse KIRMIZI. Runtime (src/vm, runtime/) serbest.
//   DENETIM         onbellegin kendi ayarlari (nerede, ne kadar, acik mi).
//                   Ciktiyi degistirmezler; yalniz aot_cache.cpp okuyabilir
//                   (kapi baska bir dosyada gecerse KIRMIZI).
//   GOZLEM          derleyiciyi GOZLEMLEMEK icin var olan degiskenler (IR
//                   dokumu, faz sureleri, karar satirlari). Biri ayarliysa
//                   onbellek HIC kullanilmaz: isabet, gozlemlemek istenen
//                   derlemeyi atlardi (.ll yazilmaz, sure olculmez).
//
// Bicim: X(ad_ya_da_kalip, "kim okur / neden"). ONEK = adin basi, SONEK =
// adin sonu, AD = tam ad. Yeni bir runtime degiskeni eklemek ZORUNLU degil
// (eklenmezse yalniz gereksiz iska olur); bir derleyici degiskenini buraya
// eklemek ise kapiyi kirmizi yapar — yapmasi gereken de bu.

#ifndef ONBELLEK_CALISMA_ZAMANI_ONEK
#define ONBELLEK_CALISMA_ZAMANI_ONEK(ad, neden)
#endif
#ifndef ONBELLEK_CALISMA_ZAMANI_SONEK
#define ONBELLEK_CALISMA_ZAMANI_SONEK(ad, neden)
#endif
#ifndef ONBELLEK_CALISMA_ZAMANI_AD
#define ONBELLEK_CALISMA_ZAMANI_AD(ad, neden)
#endif
#ifndef ONBELLEK_DENETIM_ONEK
#define ONBELLEK_DENETIM_ONEK(ad, neden)
#endif
#ifndef ONBELLEK_DENETIM_AD
#define ONBELLEK_DENETIM_AD(ad, neden)
#endif
#ifndef ONBELLEK_GOZLEM_AD
#define ONBELLEK_GOZLEM_AD(ad, neden)
#endif

// --- CALISMA_ZAMANI ---------------------------------------------------------
ONBELLEK_CALISMA_ZAMANI_ONEK("TULPAR_ENGINE_", "motor koprusu (tulpar-engine), program calisirken")
ONBELLEK_CALISMA_ZAMANI_ONEK("TULPAR_MOTOR_", "motor editoru (tulpar-engine): hangi tulpar/eklenti kosacak")
ONBELLEK_CALISMA_ZAMANI_SONEK("_TANI", "runtime tani sayaclari (src/vm: CALL/OBJ/SPLIT_TANI)")
ONBELLEK_CALISMA_ZAMANI_SONEK("_ZORUNLU", "test duzenegi: atlama yerine dusme (DIST/TLS/DAP_ZORUNLU)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_TEST_JOBS", "build.sh: paralel isci sayisi")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_COMPILE_TIMEOUT", "build.sh: derleme zaman siniri")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_RUN_TIMEOUT", "build.sh: kosu zaman siniri")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_NO_ANDROID_SMOKE", "build.sh: android dumani")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_NO_HWSTAT", "build.sh/tools/hwstat.sh")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_BIN", "test programlari env() ile okur")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_EXE", "tests/wings_tls_smoke.py")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_NO_I32", "runtime: array_fill 32 bit depo (src/vm/runtime_bindings.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_NO_F64", "runtime: array_fill kutusuz double depo (src/vm/runtime_bindings.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_OBJ_INDEKS_X4", "runtime: sozluk indeksi (src/vm/runtime_bindings.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_SOFT_RUNTIME", "runtime: yumusak hata kipi (src/vm/runtime_bindings.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_DB_CACHE_KB", "runtime: SQLite (src/vm/runtime_db.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_DB_DEBUG", "runtime: SQLite (src/vm/runtime_db.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_DB_NO_WAL", "runtime: SQLite (src/vm/runtime_db.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_HTTP_POOL", "runtime: HTTP baglanti havuzu (src/vm/runtime_net.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_TAME_MUTE", "runtime: tame sesi (runtime/tame_impl.c)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_TLS_INSECURE", "runtime: HTTPS dogrulamasi (src/common/http_fetch.cpp)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_CA_BUNDLE", "runtime: HTTPS kok sertifikalari (src/common/http_fetch.cpp)")
// Derleme yolu disindaki araclar (derleyici okumaz; bir jeton degistikce
// iska olmasin, jeton anahtar metnine de girmesin):
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_PUBLISH_TOKEN", "tulpar pkg publish (src/pkg)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_REGISTRY", "tulpar pkg (src/pkg)")
ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_LSP_DEBUG", "tulpar --lsp (src/lsp)")

// --- DENETIM ----------------------------------------------------------------
ONBELLEK_DENETIM_AD("TULPAR_AOT_NOCACHE", "1 = onbellek kapali (eski ad korunuyor)")
ONBELLEK_DENETIM_ONEK("TULPAR_CACHE_", "DIR (kok dizin), MAX_MB (tavan), RAPOR (karar satirlari)")

// --- GOZLEM -----------------------------------------------------------------
ONBELLEK_GOZLEM_AD("TULPAR_AOT_TIME", "faz sureleri: isabet derlemeyi olcmezdi")
ONBELLEK_GOZLEM_AD("TULPAR_AOT_EMIT_LL", "<cikti>.ll: isabette yazilmazdi")
ONBELLEK_GOZLEM_AD("TULPAR_AOT_EMIT_LL_PRE", "<cikti>.pre.ll: isabette yazilmazdi")
ONBELLEK_GOZLEM_AD("TULPAR_AOT_VERBOSE", "derleme adimlari (stdout): isabette basilmazdi")
ONBELLEK_GOZLEM_AD("TULPAR_AOT_STRIP_DEBUG", "ayiklama karar satirlari")
ONBELLEK_GOZLEM_AD("TULPAR_AOT_BOLUM_KORU", "bolum nesnelerini birakir (llvm_bolum.cpp): isabette nesne yazilmazdi")
ONBELLEK_GOZLEM_AD("TULPAR_DBG_VER", "dongu surumu karar satirlari")
ONBELLEK_GOZLEM_AD("TULPAR_DBG_TE", "yerel giris (t_<ad>.n) aday satirlari")
ONBELLEK_GOZLEM_AD("TULPAR_PERF_HINTS", "performans ipuclari")

#undef ONBELLEK_CALISMA_ZAMANI_ONEK
#undef ONBELLEK_CALISMA_ZAMANI_SONEK
#undef ONBELLEK_CALISMA_ZAMANI_AD
#undef ONBELLEK_DENETIM_ONEK
#undef ONBELLEK_DENETIM_AD
#undef ONBELLEK_GOZLEM_AD

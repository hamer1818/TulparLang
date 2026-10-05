#!/usr/bin/env python3
"""ONBELLEK ANAHTARI KAPISI — statik ayak (2026-10-05).

Derleme onbellegi (src/aot/aot_cache.cpp) derleyicinin gordugu HER `TULPAR_*`
ortam degiskenini anahtara katar; disarida kalabilenler yalniz
src/aot/aot_cache_env.inc'teki uc siniftir. Bu kapi o listenin YALAN
SOYLEMEDIGINI olcer:

  1. CALISMA_ZAMANI sinifindaki bir ad (tam ad / onek / sonek) derleyici
     tarafinda — src/aot, src/typeinfer, src/parser, src/lexer, src/ext,
     src/main.cpp — bir DIZGI SABITI olarak gecerse KIRMIZI. Derleyici onu
     okuyorsa ciktiyi degistirebilir; anahtarin disinda kalirsa onu acip
     kapatan her olcum eski ikiliyi alir (olculdu 2026-10-05, TULPAR_NO_FVER:
     mtime onbellegi "Cache hit" dedi, sabotaj olcumu yanlislikla yesildi).
     Runtime (src/vm, runtime/) serbest: orada okunan deger derlenen ikilinin
     calisirken okudugudur.
  2. DENETIM sinifi (onbellegin kendi ayarlari) yalniz src/aot/aot_cache.cpp'de
     gecebilir — baska bir dosya onu okursa ciktiyi etkileyebilir.
  3. .inc bos degil ve aot_cache.cpp onu gercekten #include ediyor (liste
     koddan kopup tek basina "dogru" gorunmesin).

Yorumlar sayilmaz (yalniz dizgi sabitleri): `// TULPAR_ENGINE_X` serbest.

POZITIF KONTROL (her kosumda, ayni denetleyici): yapay bir kaynak agacinda
derleyici dizinine TULPAR_ENGINE_HEADLESS / TULPAR_CALL_TANI /
TULPAR_TEST_JOBS okuyan, llvm_backend'e TULPAR_AOT_NOCACHE okuyan dosyalar
konur — DORT ihlalin dordu de yakalanmali; ayni adlari runtime dizininde ya da
yorumda gecen dosyalar ise ihlal sayilmamali. Biri tutmazsa kapi KIRMIZI:
gercek agactaki "temiz" sonucu hicbir sey kanitlamiyordur.

  python3 tests/onbellek_anahtari_kapisi.py [depo_koku]
"""
import os
import re
import shutil
import sys
import tempfile

DERLEYICI = ["src/aot", "src/typeinfer", "src/parser", "src/lexer", "src/ext", "src/main.cpp"]
INC = "src/aot/aot_cache_env.inc"
DENETIM_IZINLI = {"src/aot/aot_cache.cpp"}
UZANTI = (".cpp", ".hpp", ".h", ".c", ".cc", ".inc")

KURAL_RE = re.compile(r'^\s*ONBELLEK_(CALISMA_ZAMANI|DENETIM|GOZLEM)_(ONEK|SONEK|AD)\("([^"]+)"')


def kurallar(inc_yolu):
    out = {"CALISMA_ZAMANI": [], "DENETIM": [], "GOZLEM": []}
    with open(inc_yolu, encoding="utf-8") as f:
        for satir in f:
            m = KURAL_RE.match(satir)
            if m:
                out[m.group(1)].append((m.group(2), m.group(3)))
    return out


def eslesir(ad, kural_listesi):
    for tur, kalip in kural_listesi:
        if tur == "AD" and ad == kalip:
            return kalip
        if tur == "ONEK" and ad.startswith(kalip):
            return kalip + "*"
        if tur == "SONEK" and ad.endswith(kalip):
            return "*" + kalip
    return None


def dizgiler(metin):
    """C/C++ kaynagindaki dizgi sabitleri (satir no, icerik); yorumlar atlanir."""
    i, n, satir = 0, len(metin), 1
    while i < n:
        c = metin[i]
        if c == "\n":
            satir += 1
            i += 1
        elif metin.startswith("//", i):
            j = metin.find("\n", i)
            i = n if j < 0 else j
        elif metin.startswith("/*", i):
            j = metin.find("*/", i + 2)
            j = n if j < 0 else j + 2
            satir += metin.count("\n", i, j)
            i = j
        elif c == "'":
            j = i + 1
            while j < n and metin[j] != "'" and metin[j] != "\n":
                j += 2 if metin[j] == "\\" else 1
            i = j + 1
        elif c == '"':
            j, buf = i + 1, []
            while j < n and metin[j] != '"' and metin[j] != "\n":
                if metin[j] == "\\" and j + 1 < n:
                    buf.append(metin[j:j + 2])
                    j += 2
                else:
                    buf.append(metin[j])
                    j += 1
            yield satir, "".join(buf)
            i = j + 1
        else:
            i += 1


def denetle(kok):
    """(ihlaller, kurallar) — ihlal: (dosya, satir, ad, sinif, kural)."""
    k = kurallar(os.path.join(kok, INC))
    ihlal = []
    dosyalar = []
    for d in DERLEYICI:
        p = os.path.join(kok, d)
        if os.path.isfile(p):
            dosyalar.append(d)
            continue
        for ust, _, adlar in os.walk(p):
            for a in sorted(adlar):
                if a.endswith(UZANTI):
                    dosyalar.append(os.path.relpath(os.path.join(ust, a), kok).replace(os.sep, "/"))
    for rel in sorted(dosyalar):
        if rel == INC:
            continue
        with open(os.path.join(kok, rel), encoding="utf-8", errors="replace") as f:
            metin = f.read()
        for satir, s in dizgiler(metin):
            if not re.fullmatch(r"[A-Z][A-Z0-9_]*", s):
                continue
            r = eslesir(s, k["CALISMA_ZAMANI"])
            if r:
                ihlal.append((rel, satir, s, "CALISMA_ZAMANI", r))
            r = eslesir(s, k["DENETIM"])
            if r and rel not in DENETIM_IZINLI:
                ihlal.append((rel, satir, s, "DENETIM", r))
    return ihlal, k


def pozitif_kontrol(gercek_kok):
    tmp = tempfile.mkdtemp(prefix="onb_kapi_")
    try:
        for d in ("src/aot", "src/vm", "src/typeinfer"):
            os.makedirs(os.path.join(tmp, d))
        yaz = lambda rel, metin: open(os.path.join(tmp, rel), "w", encoding="utf-8").write(metin)
        # Kendi kural listesi (gercek .inc'in BICIMIYLE, icerigi sabit): beklenen
        # ihlaller gercek listenin degismesine bagli kalmasin. Gercek .inc'in
        # bicimi de buradan okunabilmeli — ilk satirlarini (makro tanimlari)
        # aynen tasiyoruz ki ayristirici onlari kural sanmasin.
        with open(os.path.join(gercek_kok, INC), encoding="utf-8") as f:
            bas = [s for s in f.read().split("\n") if s.startswith(("#ifndef", "#define", "#endif"))]
        yaz(INC, "\n".join(bas) + "\n"
            'ONBELLEK_CALISMA_ZAMANI_ONEK("TULPAR_ENGINE_", "motor")\n'
            'ONBELLEK_CALISMA_ZAMANI_SONEK("_TANI", "runtime tani")\n'
            'ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_TEST_JOBS", "duzenek")\n'
            'ONBELLEK_DENETIM_AD("TULPAR_AOT_NOCACHE", "onbellek ayari")\n'
            '    // ONBELLEK_CALISMA_ZAMANI_AD("TULPAR_NO_FVER", "yorumdaki kural sayilmaz")\n')
        yaz("src/aot/kotu.cpp",
            'const char *a = getenv("TULPAR_ENGINE_HEADLESS");\n'
            'const char *b = getenv("TULPAR_CALL_TANI");\n'
            'const char *c = getenv("TULPAR_TEST_JOBS");\n')
        yaz("src/aot/llvm_backend.cpp", 'int x = getenv("TULPAR_AOT_NOCACHE") != 0;\n')
        # Ihlal SAYILMAMASI gerekenler:
        yaz("src/aot/aot_cache.cpp", 'const char *e = getenv("TULPAR_AOT_NOCACHE");\n')
        yaz("src/vm/runtime.cpp", 'const char *e = getenv("TULPAR_ENGINE_HEADLESS");\n')
        yaz("src/typeinfer/yorum.cpp",
            '// getenv("TULPAR_ENGINE_HEADLESS") yalniz yorum\n'
            '/* "TULPAR_TEST_JOBS" */ const char *s = "TULPAR_NO_FVER";\n')
        yaz("src/main.cpp", 'int main() { return 0; }\n')
        ihlal, _ = denetle(tmp)
        bulunan = sorted((r, a) for r, _, a, _, _ in ihlal)
        beklenen = sorted([
            ("src/aot/kotu.cpp", "TULPAR_CALL_TANI"),
            ("src/aot/kotu.cpp", "TULPAR_ENGINE_HEADLESS"),
            ("src/aot/kotu.cpp", "TULPAR_TEST_JOBS"),
            ("src/aot/llvm_backend.cpp", "TULPAR_AOT_NOCACHE"),
        ])
        return bulunan == beklenen, bulunan, beklenen
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    kok = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), ".."))
    dus = 0

    ok, bulunan, beklenen = pozitif_kontrol(kok)
    if ok:
        print("  gecti  pozitif kontrol: yapay agactaki 4 ihlalin 4'u yakalandi, runtime/yorum serbest")
    else:
        print("  DUSTU  pozitif kontrol: denetleyici yapay ihlalleri dogru saymadi — asagidaki temiz sonuc hicbir sey olcmuyor")
        print("         bulunan :", bulunan)
        print("         beklenen:", beklenen)
        dus = 1

    ihlal, k = denetle(kok)
    n = {s: len(v) for s, v in k.items()}
    if n["CALISMA_ZAMANI"] < 5 or n["DENETIM"] < 1:
        print("  DUSTU  %s okunamadi ya da bos: %s" % (INC, n))
        dus = 1
    else:
        print("  gecti  %s: %d calisma-zamani, %d denetim, %d gozlem kurali"
              % (INC, n["CALISMA_ZAMANI"], n["DENETIM"], n["GOZLEM"]))
    with open(os.path.join(kok, "src/aot/aot_cache.cpp"), encoding="utf-8") as f:
        govde = f.read()
    if govde.count('#include "aot_cache_env.inc"') < 3:
        print("  DUSTU  aot_cache.cpp aot_cache_env.inc'i uc sinif icin #include etmiyor — liste koddan kopuk")
        dus = 1
    else:
        print("  gecti  aot_cache.cpp listeyi #include ediyor (uc sinif)")
    if ihlal:
        dus = 1
        print("  DUSTU  anahtar disi bir degisken derleyici tarafinda okunuyor:")
        for rel, satir, ad, sinif, kural in ihlal:
            neden = ("derleyici okuyorsa anahtarda olmali — %s'ten cikarin" % INC) if sinif == "CALISMA_ZAMANI" \
                else "onbellek ayari yalniz src/aot/aot_cache.cpp'de okunur"
            print("         %s:%d  \"%s\"  (%s %s) — %s" % (rel, satir, ad, sinif, kural, neden))
    else:
        print("  gecti  derleyici tarafinda (%s) anahtar disi degisken okunmuyor" % ", ".join(DERLEYICI))
    print("onbellek anahtari kapisi: %s" % ("DUSTU" if dus else "TAMAM"))
    return dus


if __name__ == "__main__":
    sys.exit(main())

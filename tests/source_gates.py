#!/usr/bin/env python3
"""Kaynak tarayan kapilar — TEK TANIM, ve her kapi KENDI ORNEKLERIYLE sinanir.

Neden bu dosya var (2026-09-11 bagimsiz denetimi): iki kapi `build.sh` icinde
satir ici desenler olarak duruyordu ve IKISI DE kor noktaliydi.

    TypedValue kapisi:  `TypedValue x;` yakaliyor,
                        `TypedValue a, b;` ve `TypedValue d[2];` KACIYORDU.
    #19 tani kapisi:    `printf("... Hatasi ...")` yakaliyor,
                        `fprintf(stdout, ...)` ve `puts(...)` KACIYORDU.

Ikisi de daha once bir kez elle enjeksiyon testinden gecmisti — ve gectikleri
BICIM icin dogruydular. Kacan sey, kapinin korudugunu SANDIGI sinifin obur
bicimleriydi. Yani sorun duyarlilik degil KAPSAM'di: kapi kirmiziya
donebiliyordu, ama iddia ettigi her yere bakmiyordu.

Asil yapisal kusur bu degil, sunun kalici olmamasiydi: kural #10 ("bir
nobetcinin kirmiziya donebildigini gormeden olctugunu varsayma") ELLE
uygulaniyordu. Kapiyi yazan kisi bir kez enjeksiyon yapip yesili goruyor,
sonra o kanit BUHARLASIYOR. Hicbir sey onu tekrar kosmuyor.

Bu dosya kaniti kalici kiliyor:

  * Desen TEK YERDE tanimli (asagidaki GATES). `build.sh` bu dosyayi cagirir,
    kendi kopyasini tutmaz — yoksa self-test, kapinin KENDISINI degil bir
    YENIDEN YAZIMINI sinar (ayni denetimin #9b'de yakaladigi hata).
  * Her kapi bir ornek tablosu tasir: yakalaMASI gereken BAD ornekler ve
    gecirMESI gereken GOOD ornekler. `--selftest` her kosuda bunu dogrular.
  * Yeni bir kacis bicimi bulundugunda once BAD tablosuna eklenir (kirmizi
    verir), sonra desen duzeltilir. Regresyon artik imkansiz.

Kullanim:
    python3 tests/source_gates.py --check      # gercek kaynagi tara (build.sh)
    python3 tests/source_gates.py --selftest   # ornek tablolarini dogrula
    python3 tests/source_gates.py              # ikisi de
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"


# --------------------------------------------------------------------------
# KAPI 1 — ilklendirilmemis `TypedValue` (#32 sinifi)
#
# `TypedValue sc;` yazmak yasak: `.boxed` yigin copu kalir ve llvm_backend.cpp
# icinde 30 yerde okunuyor. Olculdu: kisa-devre duzeltmesindeki TEK bir
# ilklendirilmemis alan, CI'da (LLVM 18) 16 ornegin derleyicisini SEGV etti —
# ama YERELDE Release/LLVM22'de gorunmuyordu. Ortama bagli sessizlik bu
# sinifin imzasi.
#
# Desen: `TypedValue` + bosluk + icinde ; = ( ) { } GECMEYEN bir bildirim
# govdesi + `;`. Ilklendiricisiz HER bildirimi yakalar (virgullu, dizili,
# isaretcili); `= {...}` (esittir var), fonksiyon bildirimi/parametresi
# (parantez var) ve `} TypedValue;` (arada bosluk yok) gecer.
TYPEDVALUE_RE = re.compile(r"\bTypedValue[ \t]+[^;=(){}]*;")

# --------------------------------------------------------------------------
# KAPI 2 — tani TEK KAPIDAN cikar (#19)
#
# Calisma zamani tanilari `aot_runtime_error` / `vm_runtime_error`den gecmeli:
# ikisi de stderr'e yazar ve strict modda firlatir. STDOUT'a yazan bir tani
# (a) programin kendi ciktisina karisir, (b) firlatmaz — surec 0 ile cikar.
#
# Desen COK SATIRLI olmali: ilk yazim satir-bazliydi (`grep printf | grep
# Hatasi`) ve hata metni SONRAKI satirda olan iki sizintiyi kacirdi. Cagri,
# parantez dengesiyle taraniyor.
#
# `f?printf|f?puts`: desen uzun sure yalniz `\bprintf\(` idi ve `\b` yuzunden
# butun fprintf'leri eliyordu. Niyet stderr'i muaf tutmakti, ama
# `fprintf(stdout, ...)` da ayni delikten geciyordu.
DIAG_CALL_RE = re.compile(r"\b(f?printf|f?puts)\s*\(")
DIAG_TEXT_RE = re.compile(r"Hatasi|Runtime Error")
DIAG_STDERR_RE = re.compile(r"\bf(printf|puts)\s*\(\s*stderr\b")
# Yetkili cikis noktalarinin SAYISAL tabani: desen guvenligi tek basina
# yetmez — denetim sessizce korlesirse "sizinti yok" der. Kapilar baska bir
# yola tasinirsa bu esik kirmizi verir.
DIAG_MIN_GATES = 15

# Yorum satiri elemesi: bu denetimin KENDI aciklama satirlari ornek desen
# icerir ve kapi kendi belgesine takilirdi.
COMMENT_RE = re.compile(r"^[ \t]*(//|\*|/\*)")


def _balanced_call(txt, start):
    """`start`taki cagriyi parantez dengesiyle kes (en fazla 600 karakter)."""
    seg = txt[start:start + 600]
    depth = 0
    for i, ch in enumerate(seg):
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                return seg[:i + 1]
    return seg


def scan_typedvalue(txt):
    """Ilklendirilmemis TypedValue bildirimleri -> [(satir, metin)]."""
    hits = []
    for no, line in enumerate(txt.splitlines(), 1):
        if COMMENT_RE.match(line):
            continue
        if TYPEDVALUE_RE.search(line):
            hits.append((no, line.strip()))
    return hits


def scan_diagnostics(txt):
    """STDOUT'a tani yazan cagrilar -> [(satir, metin)]."""
    hits = []
    for m in DIAG_CALL_RE.finditer(txt):
        call = _balanced_call(txt, m.start())
        if DIAG_STDERR_RE.match(call):
            continue          # yetkili: stderr'e yazan f-bicimleri
        if DIAG_TEXT_RE.search(call):
            hits.append((txt[:m.start()].count("\n") + 1,
                         call.splitlines()[0][:70]))
    return hits


# --------------------------------------------------------------------------
# ORNEK TABLOLARI — kapinin KAPSAMI burada kilitleniyor.
#
# BAD: kapi bunlari YAKALAMAK ZORUNDA. Her biri gercek bir kacis bicimi;
#      2026-09-11 denetiminde olculdu ve ucu TypedValue'da, ikisi #19'da
#      GERCEKTEN kaciyordu.
# GOOD: kapi bunlari GECIRMEK ZORUNDA. Yanlis pozitif, kapiyi gurultuye
#       bogar ve insanlar onu susturur — o da kapiyi olduren ikinci yoldur.
GATES = {
    "TypedValue ilklendirmesi": {
        "scan": scan_typedvalue,
        "files": ["src/aot/llvm_backend.cpp"],
        "bad": [
            ("tek degisken",   "  TypedValue bozuk;"),
            ("satir ortasi",   "static void f(void) { TypedValue bozuk; (void)bozuk.value; }"),
            ("virgullu",       "  TypedValue ac, be;"),
            ("dizili",         "  TypedValue dizi[2];"),
            ("isaretcili",     "  TypedValue *p;"),
        ],
        "good": [
            ("uc alan ilklendirilmis", "  TypedValue x = {nullptr, INFERRED_UNKNOWN, nullptr};"),
            ("fonksiyon bildirimi",    "TypedValue codegen_typed_expr(LLVMBackend *b, ASTNode_C *n);"),
            ("parametre",              "LLVMValueRef box_typed_value(LLVMBackend *b, TypedValue tv);"),
            ("typedef kapanisi",       "} TypedValue;"),
            ("yorum satiri",           "      // TypedValue sc; YETMEZ — uc alani da yazin"),
        ],
        "fix": "TypedValue x = {nullptr, INFERRED_UNKNOWN, nullptr};",
        "red_msg": "ILKLENDIRILMEMIS TypedValue — uc alani da yazin!",
        "green_msg": "TypedValue ilklendirmesi tam",
    },
    "tani tek kapi": {
        "scan": scan_diagnostics,
        "files": ["src/vm/runtime_bindings.cpp"],
        "bad": [
            ("ham printf",      'void f(void) { printf("Calisma Zamani Hatasi: x\\n"); }'),
            ("fprintf stdout",  'void f(void) { fprintf(stdout, "Calisma Zamani Hatasi: x\\n"); }'),
            ("puts",            'void f(void) { puts("Calisma Zamani Hatasi: x"); }'),
            ("cok satirli",     'void f(void) {\n  printf("%s\\n",\n         "Runtime Error: x");\n}'),
        ],
        "good": [
            ("yetkili stderr",   'void f(void) { fprintf(stderr, "Runtime Error: x\\n"); }'),
            ("tanisiz printf",   'void f(void) { printf("islem tamam\\n"); }'),
        ],
        "fix": "aot_runtime_error(...) kullanin (stderr + strict modda firlatir)",
        "red_msg": "TANI STDOUT'A SIZIYOR — aot_runtime_error kullanin!",
        "green_msg": "tani tek kapidan cikiyor",
    },
}


def selftest(quiet=False):
    """Her kapiyi KENDI orneklerine karsi kos. Bu, kural #10'un kalici hali."""
    fails = []
    for name, g in GATES.items():
        for etiket, sample in g["bad"]:
            if not g["scan"](sample):
                fails.append("%s: BAD ornegi '%s' YAKALANMADI — kapi bu bicimi "
                             "gormuyor" % (name, etiket))
        for etiket, sample in g["good"]:
            if g["scan"](sample):
                fails.append("%s: GOOD ornegi '%s' yanlis yakalandi — yanlis "
                             "pozitif" % (name, etiket))
    if fails:
        print(RED + "KAPI SELF-TESTI BASARISIZ" + RESET)
        for f in fails:
            print("  " + f)
        return 1
    if not quiet:
        toplam_bad = sum(len(g["bad"]) for g in GATES.values())
        toplam_good = sum(len(g["good"]) for g in GATES.values())
        print(GREEN + "kapi self-testi" + RESET +
              " — %d kapi, %d kacis bicimi yakalaniyor, %d mesru bicim geciyor"
              % (len(GATES), toplam_bad, toplam_good))
    return 0


def check(only=None):
    """Gercek kaynagi tara. build.sh'in cagirdigi yol.

    `only` verilirse yalniz o kapi kosar — build.sh iki kapiyi ayri
    noktalarda cagiriyor ve cikti sirasi korunuyor.
    """
    rc = 0
    for name, g in GATES.items():
        if only and name != only:
            continue
        hits = []
        for rel in g["files"]:
            path = os.path.join(ROOT, rel)
            if not os.path.exists(path):
                print(RED + "KAYNAK YOK" + RESET + ": %s — kapi '%s' hicbir "
                      "sey olcmuyor" % (rel, name))
                rc = 1
                continue
            with open(path, encoding="utf-8", errors="replace") as f:
                txt = f.read()
            hits += [(rel, no, line) for no, line in g["scan"](txt)]
        if hits:
            print(RED + g["red_msg"] + RESET)
            for rel, no, line in hits[:5]:
                print("  %s:%d: %s" % (rel, no, line[:70]))
            print("  dogru kalip: %s" % g["fix"])
            rc = 1
            continue
        if name == "tani tek kapi":
            # SAYISAL BEKLENEN-DEGER: yetkili kapilarin sayisi da raporlanir.
            with open(os.path.join(ROOT, "src/vm/runtime_bindings.cpp"),
                      encoding="utf-8", errors="replace") as f:
                gates = len(re.findall(r"aot_runtime_error\(", f.read()))
            if gates < DIAG_MIN_GATES:
                print(RED + "Yetkili tani kapisi sayisi beklenenin altinda "
                      "(%d < %d)" % (gates, DIAG_MIN_GATES) + RESET)
                print("  Tanilar baska bir yola mi tasindi? Denetim korlesmis olabilir.")
                rc = 1
                continue
            print(GREEN + g["green_msg"] + RESET +
                  " (ham printf yok, %d yetkili kapi)" % gates)
        else:
            print(GREEN + g["green_msg"] + RESET)
    return rc


def main():
    args = sys.argv[1:]
    if "--selftest" in args:
        return selftest()
    only = None
    for a in args:
        if a.startswith("--gate="):
            only = a.split("=", 1)[1]
            if only not in GATES:
                print(RED + "BILINMEYEN KAPI" + RESET + ": %s (var olanlar: %s)"
                      % (only, ", ".join(GATES)))
                return 2
    if "--check" in args or only:
        # Self-test ONCE kosar ve SESSIZ gecer: kapsamini kaybetmis bir kapi,
        # temiz kaynakta da "temiz" der. Once aleti sina, sonra olc (#26).
        rc = selftest(quiet=True)
        return rc if rc else check(only)
    return selftest() or check()


if __name__ == "__main__":
    sys.exit(main())

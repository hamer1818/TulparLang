#!/usr/bin/env python3
"""Yigin sizintisi: DONGU GOVDESINDE alloca yapan codegen yolu var mi?

Bir `alloca` ancak fonksiyon donunce cozulur. Codegen bir alloca'yi
builder'in O ANKI blonuna koyarsa ve o blok bir dongu govdesiyse, her
YINELEME yigin harcar ve program yeterince uzun donen her programda
SIGSEGV ile olur — derleme sessiz, suitler yesil.

Olculdu (2026-09-11, R11): `AST_ARRAY_LITERAL` tam bunu yapiyordu.
    while (i < N) { array j = [1,2,3]; }   ->  N=175 000'de SIGSEGV
    8 MB yigin / (3 eleman x 16 bayt VMValue) = 174 762  (birebir)
Eleman sayisiyla olcekleniyordu: [1,2] 262 144'te, [1..8] 65 536'da.
Kardes yol `AST_OBJECT_LITERAL` dogru yapiyordu (giris blogu) — yani
hata TEK siteydi ve dogru desen zaten repodaydi.

Neden bu sinif gozden kaciyor: cokme `realloc` icinde gorunuyor, yani
YIGIN(heap) bozulmasi gibi okunuyor. RSS de yaniltiyor — `arena_drop`
kolu 11 MB ile cokuyordu, yani "bellek sorunu yok" gorunuyordu. Dogru
cevabi ASAN verdi: stack-overflow.

KIRMIZIYA DONEBILIRLIK (#10): son sekil KONTROL — 2 000 000 seviye derin
ozyineleme, yigini tuketMESI GEREKEN bir program. O sekil TEMIZ cikarsa
duzenek yigin tukenmesini hic gormuyor demektir ve digerlerinin yesili
anlamsizdir.
"""
import os, subprocess, sys, tempfile

N = 2_000_000  # 16 bayt/yineleme bile 524 288'de patlar; 2M genis pay
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RED, GREEN, RESET = "\033[0;31m", "\033[0;32m", "\033[0m"

# (ad, dongu govdesi, on-tanimlar, beklenen toplam)
#
# ⚠ HER GOVDE `n`i BESLEMEK ZORUNDA, ve program `n`i BASAR. Ilk yazilisinda
# govdeler sonucu kullanmiyordu (`array j = [1,2,3];` ve o kadar) — LLVM
# olu kodu atinca dongu de alloca da yok oluyor ve sekil "TEMIZ" diyor,
# HICBIR SEY OLCMEDEN. 14 program 1,5 saniyede bitmisti; suphelenmek icin
# yeterliydi. Cikti-mutabakati o kapiyi kapatiyor: beklenen toplam
# tutmuyorsa dongu ya kosmadi ya yanlis kostu.
SHAPES = [
    ("dizi literali",       "array j = [1, 2, 3]; n = n + len(j);",    "",                           3 * N),
    ("nesne literali",      'json j = {"a": 1, "b": 2}; n = n + j["a"];', "",                        N),
    ("int yerel",           "int x = i; n = n + x;",                   "",                           N * (N - 1) // 2),
    ("str yerel",           'str s = "sabit"; n = n + length(s);',     "",                           5 * N),
    ("json yerel",          'json j = g; n = n + j["k"];',             'json g = {"k": 1};',         N),
    ("erisimci at()",       "n = n + at(g, 0, -1);",                   "array g = [1, 2, 3];",       N),
    ("json_get()",          'n = n + json_get(g, "k", -1);',           'json g = {"k": 1};',         N),
    ("try/catch",           "try { n = n + 1; } catch (e) { }",        "",                           N),
    ("dallanma",            "if (i > 0) { n = n + 1; } else { n = n + 2; }", "",                      N + 1),
    ("fonksiyon cagrisi",   "n = n + yardim(i);",                      "func yardim(int n) { return n + 1; }", N * (N + 1) // 2),
    ("dizgi birlestirme",   'str s = "a" + toString(i); n = n + length(s);', "",                     None),
    ("eleman yazma",        "g[0] = i; n = n + g[0];",                 "array g = [1, 2, 3];",       N * (N - 1) // 2),
    ("ic ice dizi literali", "array j = [[1, 2], [3, 4]]; n = n + len(j);", "",                      2 * N),
]

CONTROL = ("KONTROL: derin ozyineleme",
           "func derin(int n) { if (n <= 0) { return 0; } return 1 + derin(n - 1); }\n"
           "int r = derin(2000000);\nprint(toString(r));\n")


def build_and_run(src_text, tmp, tag, env):
    src = os.path.join(tmp, tag + ".tpr")
    binary = os.path.join(tmp, tag)
    with open(src, "w") as f:
        f.write(src_text)
    b = subprocess.run([os.path.join(ROOT, "tulpar"), "build", src, binary],
                       capture_output=True, env=env, cwd=ROOT)
    if b.returncode != 0:
        return None, b.stdout.decode("utf-8", "replace")[-400:]
    r = subprocess.run([binary], capture_output=True, env=env, timeout=300)
    return r.returncode, r.stdout.decode("utf-8", "replace").strip()


def main():
    tmp = tempfile.mkdtemp(prefix="stack_")
    env = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="")
    fails = []
    print("Yigin sizintisi taramasi (%d yineleme/sekil)" % N)
    for idx, (name, body, pre, expect) in enumerate(SHAPES):
        text = ("%s\nint i = 0;\nint n = 0;\nwhile (i < %d) { %s i = i + 1; }\n"
                "print(toString(n));\n") % (pre, N, body)
        rc, out = build_and_run(text, tmp, "s%d" % idx, env)
        live = out not in ("", "0")                    # dongu gercekten kostu mu
        agree = expect is None or out == str(expect)   # ve DOGRU kostu mu
        ok = rc == 0 and live and agree
        detail = ""
        if rc != 0:
            detail = "rc=%s <- yigin tukendi" % rc
        elif not live:
            detail = "cikti=%r <- dongu elenmis olabilir, sekil HICBIR SEY olcmuyor" % out
        elif not agree:
            detail = "cikti=%s beklenen=%s" % (out, expect)
        print("  %s %-24s %s" % (GREEN + "TEMIZ  " + RESET if ok else RED + "SIZINTI" + RESET,
                                 name, detail))
        if not ok:
            fails.append(name)

    # Kontrol: yigini tuketMESI gereken sekil. Temiz cikarsa duzenek kordur.
    rc, out = build_and_run(CONTROL[1], tmp, "ctrl", env)
    saw = rc is not None and rc != 0
    print("  %s %-24s %s" % (GREEN + "KIRMIZI" + RESET if saw else RED + "KOR    " + RESET,
                             CONTROL[0], "rc=%s <- yigin tukenmesi goruldu" % rc if saw
                             else "TEMIZ cikti: duzenek yigin tukenmesini GORMUYOR"))
    if not saw:
        fails.append("kontrol korlugu")

    if fails:
        print(RED + "Yigin taramasi BASARISIZ" + RESET + ": %s" % ", ".join(fails))
        return 1
    print(GREEN + "Yigin temiz" + RESET + " — %d sekil, kontrol kirmizi verebiliyor" % len(SHAPES))
    return 0


if __name__ == "__main__":
    sys.exit(main())

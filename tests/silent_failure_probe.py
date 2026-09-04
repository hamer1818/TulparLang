#!/usr/bin/env python3
"""Sessiz hata tarayicisi: beklenen ciktisi bilinen kucuk programlar.

NEDEN VAR: paketler ve ornekler "dogru yazilmis" programlari kosuyor. Bu
tarayici KENAR durumlarini kosuyor ve ozellikle SESSIZ bozulmalari ariyor —
derleyicinin "basarili" dedigi, hicbir tani basmadigi, ama sonucun yanlis
oldugu ya da ikilinin coktugu durumlar. O sinif normal testlerde gorunmez.

Bu tarayici iki gercek hatayi boyle buldu (2026-09-04):
  * `int free = 3;` -> ust duzey degisken libc sembolunu eziyordu; derleme
    "Successfully created" diyor, ikili ilk serbest birakmada SIGSEGV.
  * `func exit(int x)` -> kullanici fonksiyonu yerine YERLESIK cagriliyordu;
    surec 5 ile sonlaniyor, tek kelime uyari yok. 11 yerlesik ayni sekilde.

Aranan uc bozulma bicimi:
  * cokme / bos cikti (derleme "basarili" derken ikili patliyor)
  * yanlis sonuc
  * beklenmeyen derleyici hatasi

Sonda eklemek serbest ve tesvik edilir: (ad, kaynak, beklenen cikti).
"""
import os, subprocess, sys, tempfile, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
TULPAR = str(ROOT / "tulpar")
D = pathlib.Path(tempfile.mkdtemp(prefix="probe"))
CASES = []
def c(name, src, expect):
    CASES.append((name, src, expect.strip()))

# --- 1. Ad cakismasi: KULLANICI FONKSIYONLARI ---
c("fonksiyon adi 'free'", 'func free(int x) { return x + 1; }\nprint(free(1));', "2")
c("fonksiyon adi 'malloc'", 'func malloc(int x) { return x * 2; }\nprint(malloc(3));', "6")
c("fonksiyon adi 'exit'", 'func exit(int x) { return x - 1; }\nprint(exit(5));', "4")
c("fonksiyon adi 'strlen'", 'func strlen(int x) { return x; }\nprint(strlen(9));', "9")
c("fonksiyon adi 'abort'", 'func abort(int x) { return x; }\nprint(abort(7));', "7")
c("fonksiyon adi 'memcpy'", 'func memcpy(int x) { return x; }\nprint(memcpy(4));', "4")

# --- 2. Sekil onbellegi kenar durumlari ---
c("bos dizide len dongusu",
  'int[] a = array_fill(0, 0);\nint t = 0;\nfor (int i = 0; i < len(a); i = i + 1) { t = t + 1; }\nprint(t);', "0")
c("dongude break",
  'int[] a = array_fill(10, 3);\nint t = 0;\nfor (int i = 0; i < len(a); i = i + 1) { if (i == 4) { break; } t = t + a[i]; }\nprint(t);', "12")
c("dongude continue",
  'int[] a = array_fill(6, 2);\nint t = 0;\nfor (int i = 0; i < len(a); i = i + 1) { if (i == 2) { continue; } t = t + a[i]; }\nprint(t);', "10")
c("ic ice ayni dizi",
  'int[] a = array_fill(4, 1);\nint t = 0;\nfor (int i = 0; i < len(a); i = i + 1) { for (int j = 0; j < len(a); j = j + 1) { t = t + a[j]; } }\nprint(t);', "16")
c("fonksiyon icinde onbellekli dongu",
  'func topla(int[] a) { int t = 0; for (int i = 0; i < len(a); i = i + 1) { t = t + a[i]; } return t; }\nint[] x = array_fill(5, 4);\nprint(topla(x));', "20")
c("dizi fonksiyona gecirilip degistiriliyor",
  'func ekle(int[] a) { a.push(99); return 0; }\nint[] x = array_fill(2, 1);\nint t = 0;\nfor (int i = 0; i < 3; i = i + 1) { ekle(x); t = len(x); }\nprint(t);', "5")
c("iki farkli dizi ayni donguda",
  'int[] a = array_fill(3, 1);\nint[] b = array_fill(3, 10);\nint t = 0;\nfor (int i = 0; i < len(a); i = i + 1) { t = t + a[i] + b[i]; }\nprint(t);', "33")
c("dizi dongu icinde kutuya donuyor sonra len",
  'int[] a = array_fill(4, 1);\nint son = 0;\nfor (int i = 0; i < 4; i = i + 1) { if (i == 1) { a[i] = 2.5; } son = len(a); }\nprint(son);', "4")

# --- 3. Sayisal kenar durumlar ---
c("negatif indeks okuma", 'int[] a = array_fill(3, 5);\nprint(a[0]);', "5")
c("buyuk int", 'int x = 9223372036854775806;\nprint(x + 1);', "9223372036854775807")
c("negatif array_fill", 'int[] a = array_fill(-5, 1);\nprint(len(a));', "0")
c("sifir array_fill", 'int[] a = array_fill(0, 1);\nprint(len(a));', "0")

# --- 4. Dizgi kenar durumlari ---
c("bos dizgi len", 'str s = "";\nprint(len(s));', "0")
c("utf8 dizgi", 'str s = "çğüöşı";\nprint(len(s));', "12")
c("dizgi indeksleme dongude",
  'str s = "abcde";\nstr son = "";\nfor (int i = 0; i < len(s); i = i + 1) { son = s[i]; }\nprint(son);', "e")

# --- 5. Yerlesik golgeleme (2026-09-04'te bulunan hata) ---
for _b, _v in [("exit", 4), ("len", 101), ("abs", 101), ("round", 101),
               ("upper", 101), ("lower", 101), ("trim", 101), ("keys", 101),
               ("values", 101), ("range", 101), ("sqrt", 101)]:
    c(f"kullanici '{_b}' yerlesigi golgeler",
      f"func {_b}(int x) {{ return x + 100; }}\nprint({_b}(1));",
      "101" if _b != "exit" else "101")

# --- 6. Tamsayi bolme tuzaklari (2026-09-04'te bulunan hata) ---
# `10 / n` (n calisma zamaninda 0) ham sdiv uretiyordu -> SIGFPE, program
# TEK KELIME ETMEDEN oluyordu. Sabit 0 ile yazilirsa LLVM katliyor ve hata
# GORUNMUYOR; o yuzden bolen ortamdan geliyor (katlanamaz).
c("sifira bolme oldurmemeli",
  'int n = toInt(env("KESINLIKLE_YOK_12345"));\nprint(10 / n);\nprint(7);',
  "Runtime Error: Division by zero\n0\n7")
c("sifira mod oldurmemeli",
  'int n = toInt(env("KESINLIKLE_YOK_12345"));\nprint(10 % n);\nprint(7);',
  "Runtime Error: Division by zero\n0\n7")
c("INT_MIN / -1 oldurmemeli",
  'int n = toInt(env("KESINLIKLE_YOK_12345"));\nint m = n - 1;\n'
  'int big = -9223372036854775807 - 1;\nprint(big / m);\nprint(7);',
  "Runtime Error: Integer division overflow\n0\n7")
c("normal bolme bozulmadi", 'print(10 / 3);\nprint(-7 / 2);\nprint(10 % 3);\nprint(-7 % 3);',
  "3\n-3\n1\n-1")

fails = 0
for name, src, expect in CASES:
    safe = "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in name)
    f = D / (safe + ".tpr")
    f.write_text(src, encoding="utf-8")
    # LC_ALL=C: tani metinleri yerele gore degisiyor, beklenen ciktiyi
    # sabitlemek icin ingilizceye pinliyoruz (bkz. tests/typeinfer/run.sh).
    env = dict(os.environ, LC_ALL="C")
    p = subprocess.run([TULPAR, str(f)], capture_output=True, text=True,
                       timeout=90, env=env)
    out = (p.stdout or "").strip()
    # [typecheck] uyarilari stderr'e gidiyor; cikti karsilastirmasini
    # bozmamali (golgeleme sondasi bilerek uyari uretiyor).
    if p.returncode != 0 or out != expect:
        fails += 1
        print(f"  ✗ {name}")
        print(f"      cikis={p.returncode} beklenen={expect!r} alinan={out!r}")
        err = (p.stderr or "").strip()
        if err: print(f"      stderr: {err[:200]}")
print(f"\n{len(CASES)} sonda, {fails} sorun")
sys.exit(1 if fails else 0)

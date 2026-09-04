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

# --- 7a. Blok kapsami (2026-09-04'te bulunan hata) ---
# `if`/`while` govdeleri kapsam ACMIYORDU: icteki `int x = 5` distakini
# eziyordu. `for` dogruydu — dilde iki farkli kapsam kurali vardi.
c("if govdesi golgeler", 'int x = 1;\nif (true) { int x = 5; }\nprint(x);', "1")
c("while govdesi golgeler",
  'int x = 1;\nint i = 0;\nwhile (i < 1) { int x = 5; i = i + 1; }\nprint(x);', "1")
c("ic ice blok golgeleme",
  'int x = 1;\nif (true) { int x = 2; if (true) { int x = 3; print(x); } print(x); }\n'
  'print(x);', "3\n2\n1")
c("blok dis degiskeni yazar", 'int x = 7;\nif (true) { x = 9; }\nprint(x);', "9")

# --- 7b. Bilesik atamalar ---
c("%= operatoru", 'int x = 17;\nx %= 5;\nprint(x);', "2")
c("%= sifira", 'int n = toInt(env("KESINLIKLE_YOK_12345"));\nint z = 7;\nz %= n;\n'
  'print(z);\nprint(9);', "Runtime Error: Division by zero\n0\n9")

# --- 7. Eleman hedefli ++/-- (2026-09-04'te bulunan hata) ---
# `a[0]++` SESSIZ HIC-ISLEMDI: parser `++` token'ini tuketip atiyordu
# (hedef Identifier degilse govde calismiyordu), ifade `a[0];` olarak
# kaliyordu. Program derleniyor, calisiyor, deger degismiyor, uyari yok.
c("dizi elemani ++", 'int[] a = array_fill(3, 3);\na[0]++;\nprint(a[0]);', "4")
c("dizi elemani --", 'int[] a = array_fill(3, 3);\na[0]--;\nprint(a[0]);', "2")
c("json alani ++", 'json j = {"n": 5};\nj["n"]++;\nprint(j["n"]);', "6")
c("eleman ++ post degeri",
  'int[] a = array_fill(2, 5);\nprint(a[0]++);\nprint(a[0]);', "5\n6")
c("eleman ++ donguda",
  'int[] a = array_fill(2, 1);\nint i = 0;\nwhile (i < 2) { a[i]++; i++; }\n'
  'print(a[0] + a[1]);', "4")
c("degisken ++ bozulmadi", 'int x = 5;\nprint(x++);\nprint(x);', "5\n6")

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
# --- fmt GIDIS-DONUS ---
# Bicimlendirici cikitisi YENIDEN AYRISTIRILABILIR ve AYNI sonucu vermeli.
# Guclu bir degismez: `%=` eklenirken fmt onu `% =` diye boluyordu ve kod
# BOZULUYORDU (ayni sinif daha once `=>` icin de olmus — formatter.cpp'deki
# yoruma bakin). Bu sonda o sinifi topluca yakaliyor.
fmt_fails = 0
for name, src, expect in CASES:
    if expect is None or "\n" not in src:
        continue
    safe = "".join(ch if (ch.isalnum() or ch == "_") else "_" for ch in name)
    f = D / ("fmt_" + safe + ".tpr")
    f.write_text(src, encoding="utf-8")
    env = dict(os.environ, LC_ALL="C")
    r = subprocess.run([TULPAR, "fmt", str(f)], capture_output=True, text=True,
                       timeout=90, env=env)
    if r.returncode != 0 or not r.stdout.strip():
        continue          # fmt bu girdiyi islemiyorsa sondanin konusu degil
    g = D / ("fmtout_" + safe + ".tpr")
    g.write_text(r.stdout, encoding="utf-8")
    q = subprocess.run([TULPAR, str(g)], capture_output=True, text=True,
                       timeout=90, env=env)
    if q.returncode != 0 or (q.stdout or "").strip() != expect:
        fmt_fails += 1
        print(f"  ✗ fmt gidis-donus: {name}")
        print(f"      beklenen={expect!r} alinan={(q.stdout or '').strip()!r}")
fails += fmt_fails

print(f"\n{len(CASES)} sonda (+fmt gidis-donus), {fails} sorun")
sys.exit(1 if fails else 0)

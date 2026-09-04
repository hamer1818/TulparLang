#!/usr/bin/env python3
"""Adil dil karşılaştırması.

Eski `benchmarks/` takımının üç ayrı kusuru vardı ve üçü de sonucu
geçersiz kılıyordu (2026-09-02'de ölçüldü):

  1. SABİT PARAMETRE: yalnız Tulpar N'i ortamdan okuyordu; C/Rust/Go/JS/Py
     derleme-zamanı sabiti alıyordu. gcc -O2 ve rustc -O3 `loopsum`u kapalı
     forma KATLIYOR — `objdump` ile doğrulandı, main içinde sıfır atlama
     komutu. Yani C ve Rust hiç döngü koşmadan "kazanıyordu".
  2. FARKLI ALGORİTMA: `sieve`de C `char*` kullanırken Tulpar `json`a push
     ediyordu; `struct_array_push`ta C tek `malloc` yapıp indeksle yazarken
     Tulpar büyüyen diziye push ediyordu.
  3. ÖLÇÜM GÜRÜLTÜSÜ: "best of 1" ve bütün süreler 22-60 ms — yani ölçülen
     şeyin çoğu süreç başlatma maliyetiydi.

Bu koşucu üçünü de kapatıyor:
  * N her dilde `BENCH_N` ortam değişkeninden okunuyor -> kimse katlayamaz.
  * Aynı algoritma + aynı veri yapısı (kaynaklar yan yana okunabilir).
  * Her ölçüm BOŞ program taban çizgisiyle birlikte raporlanıyor ve iş
    yükü başlatmayı gölgede bırakacak kadar büyük seçiliyor.
  * ÇIKTI DOĞRULAMASI: bütün diller aynı şeyi basmazsa satır GEÇERSİZ.
    Bu, "aynı işi yapıyorlar mı" sorusunun tek dürüst cevabı.
"""
import json, os, statistics, subprocess, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
TULPAR = ROOT / "tulpar"
OUT = HERE / "build"
OUT.mkdir(exist_ok=True)

REPEATS = int(os.environ.get("REPEATS", "5"))

# İş yükü boyutları: her biri hızlı dillerde ~0.3-1 sn sürecek şekilde
# seçildi ki süreç başlatma (~1-40 ms) gürültüye insin.
BENCH = {
    "intloop": ("50000000",  "tamsayı aritmetiği (zincirleme bağımlılık)"),
    "fib":     ("32",        "özyineleme (çağrı maliyeti)"),
    "sieve":   ("5000000",   "dizi/bellek erişimi"),
    "strcat":  ("2000000",   "dizgi kurma + tarama"),
    # Gercek kodun en yaygin kalibi: diziyi deyimsel uzunlukla gezmek.
    # Her dil kendi uzunluk erisimini kullaniyor (len(a) / a.len() /
    # a.length); C'de dilde uzunluk yok, n tasiniyor.
    "arrayiter": ("5000000", "dizi yineleme (deyimsel uzunlukla)"),
}

LANGS = ["c", "rust", "go", "java", "node", "python", "tulpar"]
LABEL = {
    "c": "C (gcc -O2)", "rust": "Rust (-O3)", "go": "Go", "java": "Java",
    "node": "Node.js", "python": "Python", "tulpar": "Tulpar AOT",
}


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True,
                          text=True, cwd=str(HERE), **kw)


def build(bench):
    """Her dil için (çalıştırma komutu, hata) döndürür."""
    cmds, errs = {}, {}
    c = OUT / f"{bench}_c"
    r = sh(["gcc", "-O2", f"{bench}.c", "-o", str(c)])
    cmds["c"] = [str(c)] if r.returncode == 0 else None
    if r.returncode: errs["c"] = r.stderr.strip()[:200]

    rs = OUT / f"{bench}_rs"
    r = sh(["rustc", "-C", "opt-level=3", f"{bench}.rs", "-o", str(rs)])
    cmds["rust"] = [str(rs)] if r.returncode == 0 else None
    if r.returncode: errs["rust"] = r.stderr.strip()[:200]

    g = OUT / f"{bench}_go"
    r = sh(["go", "build", "-o", str(g), f"{bench}.go"])
    cmds["go"] = [str(g)] if r.returncode == 0 else None
    if r.returncode: errs["go"] = r.stderr.strip()[:200]

    r = sh(["javac", "-d", str(OUT), f"{bench}.java"])
    cmds["java"] = ["java", "-cp", str(OUT), bench] if r.returncode == 0 else None
    if r.returncode: errs["java"] = r.stderr.strip()[:200]

    cmds["node"] = ["node", str(HERE / f"{bench}.js")]
    cmds["python"] = ["python3", str(HERE / f"{bench}.py")]

    t = OUT / f"{bench}_tulpar"
    r = sh([str(TULPAR), "build", f"{bench}.tpr", str(t)])
    cmds["tulpar"] = [str(t)] if (r.returncode == 0 and t.exists()) else None
    if cmds["tulpar"] is None:
        errs["tulpar"] = (r.stdout + r.stderr).strip()[-200:]
    return cmds, errs


def timeit(cmd, env, repeats):
    """En iyi ve ortanca duvar saati (ms). İlk koşum ısıtma, sayılmıyor."""
    subprocess.run(cmd, capture_output=True, text=True, env=env)  # ısıtma
    times, out = [], None
    for _ in range(repeats):
        t0 = time.perf_counter()
        p = subprocess.run(cmd, capture_output=True, text=True, env=env)
        t1 = time.perf_counter()
        if p.returncode != 0:
            return None, None, (p.stderr or p.stdout).strip()[:200]
        out = p.stdout.strip()
        times.append((t1 - t0) * 1000.0)
    return (min(times), statistics.median(times)), out, None


def baseline():
    """Boş program maliyeti: ölçtüğümüz şeyin ne kadarı başlatma?"""
    base = {}
    progs = {
        "c": ("int main(void){return 0;}", "empty.c"),
        "python": ("pass", "empty.py"),
        "node": ("", "empty.js"),
    }
    (HERE / "empty.c").write_text(progs["c"][0])
    (HERE / "empty.py").write_text("pass\n")
    (HERE / "empty.js").write_text("\n")
    e = OUT / "empty_c"
    if sh(["gcc", "-O2", "empty.c", "-o", str(e)]).returncode == 0:
        base["c"], _, _ = timeit([str(e)], os.environ.copy(), 5)
    base["python"], _, _ = timeit(["python3", str(HERE / "empty.py")], os.environ.copy(), 5)
    base["node"], _, _ = timeit(["node", str(HERE / "empty.js")], os.environ.copy(), 5)
    (HERE / "empty.tpr").write_text('int x = 0;\n')
    et = OUT / "empty_tulpar"
    if sh([str(TULPAR), "build", "empty.tpr", str(et)]).returncode == 0 and et.exists():
        base["tulpar"], _, _ = timeit([str(et)], os.environ.copy(), 5)
    return {k: (v[0] if v else None) for k, v in base.items()}


def main():
    print(f"Adil kıyaslama — {REPEATS} tekrar, en iyi + ortanca\n")
    results, invalid = {}, []
    for bench, (n, desc) in BENCH.items():
        print(f"== {bench} ({desc}), BENCH_N={n}")
        cmds, errs = build(bench)
        env = os.environ.copy(); env["BENCH_N"] = n; env["LC_ALL"] = "C"
        row, outs = {}, {}
        for lang in LANGS:
            cmd = cmds.get(lang)
            if cmd is None:
                row[lang] = None
                print(f"   {LABEL[lang]:16s} DERLENEMEDI: {errs.get(lang,'')[:80]}")
                continue
            t, out, err = timeit(cmd, env, REPEATS)
            if t is None:
                row[lang] = None
                print(f"   {LABEL[lang]:16s} KOSMADI: {err[:80]}")
                continue
            row[lang] = {"best": round(t[0], 1), "median": round(t[1], 1)}
            outs[lang] = out
            print(f"   {LABEL[lang]:16s} {t[0]:9.1f} ms  (ort {t[1]:.1f})   -> {out[:28]}")
        uniq = set(outs.values())
        if len(uniq) > 1:
            invalid.append((bench, outs))
            print(f"   !! CIKTILAR AYRISIYOR -> GECERSIZ: {uniq}")
        results[bench] = {"n": n, "desc": desc, "rows": row,
                          "output": (outs and list(uniq)[0]) or None,
                          "agree": len(uniq) == 1}
        print()
    base = baseline()
    print("Bos program taban cizgisi (ms):",
          {k: (round(v, 1) if v else None) for k, v in base.items()})
    (HERE / "results.json").write_text(json.dumps(
        {"results": results, "baseline": base, "repeats": REPEATS}, indent=2))
    print("\n-> results.json")
    return 1 if invalid else 0


if __name__ == "__main__":
    sys.exit(main())

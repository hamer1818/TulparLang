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
  * Bir dilin araç zinciri yoksa satırı "DERLENEMEDI" olur ve koşum devam
    eder — eksik bir derleyici bütün ölçümü geçersiz kılmaz. (C# için
    `dotnet` ya da `mono`+`mcs` gerekiyor.)
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
    # --- KAYAN NOKTA SETI (2026-09-11) ---------------------------------
    # Uc cekirdek BILEREK farkli yerleri sinar; "float yavas mi" sorusu tek
    # sayiyla cevaplanamaz, cunku iki ayri maliyet var:
    #   * DEGER temsili (her islemde kutu ac/kapa)
    #   * DEPOLAMA temsili (eleman basina 16 bayt VMValue vs 8 bayt double)
    # mandelbrot dizi KULLANMAZ -> yalniz deger temsilini olcer.
    # matmul yalniz dizi gezer    -> depolama + bant genisligini one cikarir.
    # nbody ikisinin gercekci karisimi (+ sqrt).
    # Farki ayirmadan olculurse "float 10x yavas" denir ve HANGI yarinin
    # sucu oldugu bilinmez — unboxing karari tam o ayrima dayaniyor.
    "mandelbrot": ("2000",    "kayan nokta aritmetigi (dizi YOK)"),
    "matmul":     ("640",     "kayan nokta dizisi (depolama + bant genisligi)"),
    "nbody":      ("3000000", "kayan nokta + kucuk dizi + sqrt"),
    # --- GENISLETILMIS SET (2026-09-29) --------------------------------
    # Ilk bes cekirdek tamsayi/dizgi agirlikliydi; README'nin "KANITLAMADIGI
    # seyler" listesi (hash-map, siralama, tahsis baskisi, dolayli cagri,
    # ayristirma) burada kapatiliyor. Her biri bir ALAN temsil ediyor:
    #   hashmap   : dizgi anahtarli sozluk (her dil kendi standart sozlugu)
    #   qsort     : elle Hoare hizli siralama (rastgele erisim + ozyineleme)
    #   particles : oyun dongusu — struct DIZISI uzerinde fizik adimi
    #   callfn    : fonksiyon degeri tablosundan dolayli cagri (kanca kalibi)
    #   parse     : metin kur + bol + tamsayiya cevir (CSV/log okuma)
    "hashmap":   ("1000000",  "dizgi anahtarli sozluk: N ekleme + N arama"),
    "qsort":     ("1000000",  "elle hizli siralama (rastgele erisim + ozyineleme)"),
    "particles": ("1000000",  "struct dizisi uzerinde fizik adimi (50 adim)"),
    "callfn":    ("20000000", "fonksiyon degeri tablosundan dolayli cagri"),
    "parse":     ("5000000",  "metin kur + bol + tamsayiya cevir"),
}

# Tek bir kosumun ust siniri (sn). Bir dil bir cekirdekte asiri yavassa
# (ornek: dogrusal taramali sozluk) butun takim saatlerce beklemesin; satir
# "ZAMAN ASIMI > TIMEOUT_S" olarak GORUNUR yazilir, sessizce dusmez.
TIMEOUT_S = float(os.environ.get("TIMEOUT_S", "60"))

LANGS = ["c", "cpp", "rust", "go", "csharp", "java", "node", "python", "tulpar"]
LABEL = {
    "c": "C (gcc -O2)", "cpp": "C++ (g++ -O2)", "rust": "Rust (-O3)",
    "go": "Go", "csharp": "C# (.NET)", "java": "Java",
    "node": "Node.js", "python": "Python", "tulpar": "Tulpar AOT",
}

# C# iki ayri zincirle kosabiliyor. Once .NET SDK aranıyor ("C#" bugun bunu
# demek: tiered JIT), yoksa mono'ya dusuluyor. Ikisi de yoksa satir
# "DERLENEMEDI" olarak isaretlenip gecilyor — eksik bir arac butun koşumu
# durdurmamali.
def _which(x):
    from shutil import which
    return which(x)

HAS_DOTNET = _which("dotnet") is not None
HAS_MONO = _which("mcs") is not None and _which("mono") is not None


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=isinstance(cmd, str), capture_output=True,
                          text=True, cwd=str(HERE), **kw)


def _build_csharp(bench, errs):
    """C# ikilisi. .NET SDK varsa Release derleyip .dll'i `dotnet` ile
    kosuyoruz; yoksa mono/mcs. Hicbiri yoksa None (satir "DERLENEMEDI")."""
    if HAS_DOTNET:
        proj = OUT / f"cs_{bench}"
        proj.mkdir(parents=True, exist_ok=True)
        # Tek dosyalik minimal proje. Kaynak her seferinde kopyalaniyor ki
        # benchmarks/fair/<bench>.cs tek dogru kaynak kalsin.
        (proj / "Program.cs").write_text((HERE / f"{bench}.cs").read_text())
        (proj / f"{bench}.csproj").write_text(
            '<Project Sdk="Microsoft.NET.Sdk">\n'
            '  <PropertyGroup>\n'
            '    <OutputType>Exe</OutputType>\n'
            '    <TargetFramework>net9.0</TargetFramework>\n'
            '    <Optimize>true</Optimize>\n'
            '    <Nullable>disable</Nullable>\n'
            '    <AssemblyName>bench</AssemblyName>\n'
            '    <RootNamespace>bench</RootNamespace>\n'
            '    <InvariantGlobalization>true</InvariantGlobalization>\n'
            '  </PropertyGroup>\n'
            '</Project>\n')
        r = sh(["dotnet", "build", "-c", "Release", "-v", "q", "--nologo",
                "-o", str(proj / "out"), str(proj / f"{bench}.csproj")])
        dll = proj / "out" / "bench.dll"
        if r.returncode == 0 and dll.exists():
            return ["dotnet", str(dll)]
        errs["csharp"] = (r.stdout + r.stderr).strip()[-200:]
        return None
    if HAS_MONO:
        exe = OUT / f"{bench}_cs.exe"
        r = sh(["mcs", "-optimize+", f"-out:{exe}", f"{bench}.cs"])
        if r.returncode == 0 and exe.exists():
            return ["mono", str(exe)]
        errs["csharp"] = (r.stdout + r.stderr).strip()[-200:]
        return None
    errs["csharp"] = "dotnet ya da mono/mcs bulunamadi"
    return None


# Kaynak dosyasi olmayan dil satiri DUSER, kosum DURMAZ.
#
# Bu koruma olmadan `benchmarks/fair/<bench>.cs` yoksa `_build_csharp`
# FileNotFoundError firlatiyor ve BUTUN kosum cokuyordu — yani tek bir dilin
# eksik kaynagi otekilerin olcumunu da goturuyordu. Tam da README'nin
# "arac zinciri yoksa satir duser, kosum durmaz" kuralinin ihlali; kural
# arac zincirini kapsiyordu, KAYNAGI kapsamiyordu. (2026-09-11, FP setini
# eklerken ortaya cikti.)
def _src(bench, ext):
    return (HERE / f"{bench}.{ext}").exists()


def _size_kb(path):
    try:
        return round(Path(path).stat().st_size / 1024.0, 1)
    except OSError:
        return None


def build(bench):
    """Her dil için (çalıştırma komutu, hata) döndürür.

    Yan urun: BUILD_INFO[bench][dil] = {"ms": derleme suresi, "kb": ikili
    boyutu} — derleme hizi ve dagitim boyutu da olculen alanlar arasinda.
    """
    t_start = time.perf_counter()
    cmds, errs = _build_inner(bench)
    info = {}
    for lang, cmd in cmds.items():
        if not cmd:
            continue
        exe = None
        if lang in ("c", "cpp", "rust", "go", "tulpar"):
            exe = cmd[0]
        elif lang == "csharp" and len(cmd) > 1:
            exe = cmd[1]
        elif lang == "java":
            exe = str(OUT / f"{bench}.class")
        info[lang] = {"kb": _size_kb(exe) if exe else None,
                      "ms": BUILD_MS.get((bench, lang))}
    BUILD_INFO[bench] = info
    return cmds, errs


BUILD_INFO = {}
BUILD_MS = {}


def _timed(bench, lang, fn):
    t0 = time.perf_counter()
    r = fn()
    BUILD_MS[(bench, lang)] = round((time.perf_counter() - t0) * 1000.0, 1)
    return r


def _build_inner(bench):
    cmds, errs = {}, {}
    if not _src(bench, "c"):
        cmds["c"] = None; errs["c"] = "kaynak yok (%s.c)" % bench
    else:
        c = OUT / f"{bench}_c"
        # -lm: C'de libm AYRI baglanir (C++/Rust/Go'da degil). Bu bayrak
        # yokken nbody yalniz C satirinda "DERLENEMEDI" oluyordu ve tablo
        # C'siz yayinlanacakti — yani referans dil eksik kalacakti.
        r = _timed(bench, "c", lambda: sh(["gcc", "-O2", f"{bench}.c", "-o", str(c), "-lm"]))
        cmds["c"] = [str(c)] if r.returncode == 0 else None
        if r.returncode: errs["c"] = r.stderr.strip()[:200]

    if not _src(bench, "rs"):
        cmds["rust"] = None; errs["rust"] = "kaynak yok (%s.rs)" % bench
    else:
        rs = OUT / f"{bench}_rs"
        r = _timed(bench, "rust", lambda: sh(["rustc", "-C", "opt-level=3", f"{bench}.rs", "-o", str(rs)]))
        cmds["rust"] = [str(rs)] if r.returncode == 0 else None
        if r.returncode: errs["rust"] = r.stderr.strip()[:200]

    if not _src(bench, "cpp"):
        cmds["cpp"] = None; errs["cpp"] = "kaynak yok (%s.cpp)" % bench
    else:
        cpp = OUT / f"{bench}_cpp"
        r = _timed(bench, "cpp", lambda: sh(["g++", "-O2", "-std=c++17", f"{bench}.cpp", "-o", str(cpp)]))
        cmds["cpp"] = [str(cpp)] if r.returncode == 0 else None
        if r.returncode: errs["cpp"] = r.stderr.strip()[:200]

    if not _src(bench, "cs"):
        cmds["csharp"] = None; errs["csharp"] = "kaynak yok (%s.cs)" % bench
    else:
        cmds["csharp"] = _timed(bench, "csharp", lambda: _build_csharp(bench, errs))

    if not _src(bench, "go"):
        cmds["go"] = None; errs["go"] = "kaynak yok (%s.go)" % bench
    else:
        g = OUT / f"{bench}_go"
        r = _timed(bench, "go", lambda: sh(["go", "build", "-o", str(g), f"{bench}.go"]))
        cmds["go"] = [str(g)] if r.returncode == 0 else None
        if r.returncode: errs["go"] = r.stderr.strip()[:200]

    if not _src(bench, "java"):
        cmds["java"] = None; errs["java"] = "kaynak yok (%s.java)" % bench
    else:
        r = _timed(bench, "java", lambda: sh(["javac", "-d", str(OUT), f"{bench}.java"]))
        cmds["java"] = ["java", "-cp", str(OUT), bench] if r.returncode == 0 else None
        if r.returncode: errs["java"] = r.stderr.strip()[:200]

    cmds["node"] = ["node", str(HERE / f"{bench}.js")] if _src(bench, "js") else None
    if cmds["node"] is None: errs["node"] = "kaynak yok (%s.js)" % bench
    cmds["python"] = ["python3", str(HERE / f"{bench}.py")] if _src(bench, "py") else None
    if cmds["python"] is None: errs["python"] = "kaynak yok (%s.py)" % bench

    if not _src(bench, "tpr"):
        cmds["tulpar"] = None; errs["tulpar"] = "kaynak yok (%s.tpr)" % bench
        return cmds, errs
    t = OUT / f"{bench}_tulpar"
    r = _timed(bench, "tulpar", lambda: sh([str(TULPAR), "build", f"{bench}.tpr", str(t)]))
    cmds["tulpar"] = [str(t)] if (r.returncode == 0 and t.exists()) else None
    if cmds["tulpar"] is None:
        errs["tulpar"] = (r.stdout + r.stderr).strip()[-200:]
    return cmds, errs


class _Timeout(Exception):
    pass


def _alarm(signum, frame):
    raise _Timeout()


def _run1(cmd, env, timeout):
    """Tek kosum: (sure ms, rc, stdout, stderr, ru_maxrss) ya da zaman asimi.

    ru_maxrss burada KULLANILMAZ (exec oncesi Python kopyasini da sayar; bkz.
    rss_of). Cikti borusu degil GECICI DOSYA ve bekleme BLOKLAYAN wait4 + SIGALRM:
    okuyucu/bekleyici iplik baslatmak bos programin ~0,2 ms'lik suresine
    kendi maliyetini (olculdu: +0,2 ms) eklerdi.
    """
    import signal, tempfile
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        t0 = time.perf_counter()
        p = subprocess.Popen(cmd, stdout=fo, stderr=fe, env=env)
        old = signal.signal(signal.SIGALRM, _alarm)
        signal.setitimer(signal.ITIMER_REAL, timeout)
        try:
            _, status, ru = os.wait4(p.pid, 0)
            t1 = time.perf_counter()
        except _Timeout:
            p.kill()
            os.wait4(p.pid, 0)
            p.returncode = -9
            return None
        finally:
            signal.setitimer(signal.ITIMER_REAL, 0)
            signal.signal(signal.SIGALRM, old)
        p.returncode = os.waitstatus_to_exitcode(status)
        fo.seek(0); fe.seek(0)
        return ((t1 - t0) * 1000.0, p.returncode,
                fo.read().decode(errors="replace"),
                fe.read().decode(errors="replace"), ru.ru_maxrss)


RSSWRAP = OUT / "rsswrap"


def rss_of(cmd, env):
    """Tek ek kosum: tepe yerlesik bellek (KB), rsswrap.c sarmalayicisiyla.

    Sure kosumlarindan AYRI: Python'dan dogrudan alinan ru_maxrss, exec'ten
    once catallanan Python kopyasini da sayiyordu (olculdu 2026-09-29: bos C
    programi bile 17 MB gorunuyordu). Sarmalayicinin kendi kopyasi ~1,7 MB.
    Sarmalayici derlenemezse None: bellek sutunu bos kalir, sure etkilenmez.
    """
    if not RSSWRAP.exists():
        if sh(["gcc", "-O2", "rsswrap.c", "-o", str(RSSWRAP)]).returncode != 0:
            return None
    try:
        p = subprocess.run([str(RSSWRAP)] + list(cmd), capture_output=True,
                           text=True, env=env, timeout=TIMEOUT_S)
    except subprocess.TimeoutExpired:
        return None
    for line in reversed(p.stderr.strip().splitlines()):
        if line.startswith("RSS_KB="):
            return int(line[7:])
    return None


def timeit(cmd, env, repeats, stats=None):
    """En iyi ve ortanca duvar saati (ms). İlk koşum ısıtma, sayılmıyor.

    Isitma kosumu TIMEOUT_S'i asarsa tekrarlar hic kosmaz ve
    `stats["timeout"]` yazilir. Tepe bellek AYRI olculur (rss_of).
    """
    w = _run1(cmd, env, TIMEOUT_S)  # ısıtma
    if w is None:
        if stats is not None:
            stats["timeout"] = TIMEOUT_S
        return None, None, "ZAMAN ASIMI > %.0f sn" % TIMEOUT_S
    times, out = [], None
    for _ in range(repeats):
        r = _run1(cmd, env, TIMEOUT_S)
        if r is None:
            if stats is not None:
                stats["timeout"] = TIMEOUT_S
            return None, None, "ZAMAN ASIMI > %.0f sn" % TIMEOUT_S
        ms, rc, so, se, maxrss = r
        if rc != 0:
            return None, None, (se or so).strip()[:200]
        out = so.strip()
        times.append(ms)
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
    (HERE / "empty.cpp").write_text("int main(){return 0;}\n")
    ec = OUT / "empty_cpp"
    if sh(["g++", "-O2", "empty.cpp", "-o", str(ec)]).returncode == 0:
        base["cpp"], _, _ = timeit([str(ec)], os.environ.copy(), 5)
    (HERE / "empty.cs").write_text("class empty{static void Main(){}}\n")
    cs_cmd = _build_csharp("empty", {})
    if cs_cmd:
        base["csharp"], _, _ = timeit(cs_cmd, os.environ.copy(), 5)
    (HERE / "empty.tpr").write_text('int x = 0;\n')
    et = OUT / "empty_tulpar"
    if sh([str(TULPAR), "build", "empty.tpr", str(et)]).returncode == 0 and et.exists():
        base["tulpar"], _, _ = timeit([str(et)], os.environ.copy(), 5)
    # Genisletilmis set (2026-09-29): baslatma tablosunda Rust/Go/Java da
    # olsun — "Tulpar hizli basliyor" iddiasi yalniz C/Python/Node'a karsi
    # degil, butun satirlara karsi okunabilsin.
    (HERE / "empty.rs").write_text("fn main(){}\n")
    er = OUT / "empty_rs"
    if sh(["rustc", "-C", "opt-level=3", "empty.rs", "-o", str(er)]).returncode == 0:
        base["rust"], _, _ = timeit([str(er)], os.environ.copy(), 5)
    (HERE / "empty.go").write_text("package main\n\nfunc main() {}\n")
    eg = OUT / "empty_go"
    if sh(["go", "build", "-o", str(eg), "empty.go"]).returncode == 0:
        base["go"], _, _ = timeit([str(eg)], os.environ.copy(), 5)
    (HERE / "empty.java").write_text("public class empty{public static void main(String[] a){}}\n")
    if sh(["javac", "-d", str(OUT), "empty.java"]).returncode == 0:
        base["java"], _, _ = timeit(["java", "-cp", str(OUT), "empty"], os.environ.copy(), 5)
    sizes = {"c": _size_kb(e), "cpp": _size_kb(ec), "rust": _size_kb(er),
             "go": _size_kb(eg), "tulpar": _size_kb(et)}
    BASE_SIZES.update({k: v for k, v in sizes.items() if v})
    return {k: (v[0] if v else None) for k, v in base.items()}


BASE_SIZES = {}


def main():
    # `--csv`: yeniden OLCMEDEN, depodaki results.json'dan results.csv uret
    # (tablo dondurulmus olcumden turer; olcum turu dakikalar suruyor).
    if "--rss" in sys.argv[1:]:
        # Yeniden SURE OLCMEDEN yalniz bellek sutununu doldur (sure turu
        # dakikalar suruyor; bellek tek kosum). results.json yerinde guncellenir.
        d = json.loads((HERE / "results.json").read_text())
        for bench, r in d["results"].items():
            cmds, _ = build(bench)
            env = os.environ.copy(); env["BENCH_N"] = r["n"]; env["LC_ALL"] = "C"
            for lang, row in r["rows"].items():
                if row and "best" in row and cmds.get(lang):
                    row["rss_kb"] = rss_of(cmds[lang], env)
            print(bench, {l: (x or {}).get("rss_kb") for l, x in r["rows"].items()})
        (HERE / "results.json").write_text(json.dumps(d, indent=2))
        BASE_SIZES.update(d.get("baseline_size_kb") or {})
        write_markdown(d["results"], d["baseline"])
        write_csv(d["results"], d.get("repeats", REPEATS))
        print("-> results.json + RESULTS.md + results.csv (bellek)")
        return 0
    if "--csv" in sys.argv[1:]:
        d = json.loads((HERE / "results.json").read_text())
        write_csv(d["results"], d.get("repeats", REPEATS))
        print("-> results.csv (results.json'dan)")
        return 0
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
            st = {}
            t, out, err = timeit(cmd, env, REPEATS, st)
            if t is None:
                row[lang] = {"timeout": st["timeout"]} if "timeout" in st else None
                print(f"   {LABEL[lang]:16s} KOSMADI: {err[:80]}")
                continue
            row[lang] = {"best": round(t[0], 1), "median": round(t[1], 1),
                         "rss_kb": rss_of(cmd, env)}
            outs[lang] = out
            print(f"   {LABEL[lang]:16s} {t[0]:9.1f} ms  (ort {t[1]:.1f})   -> {out[:28]}")
        uniq = set(outs.values())
        if len(uniq) > 1:
            invalid.append((bench, outs))
            print(f"   !! CIKTILAR AYRISIYOR -> GECERSIZ: {uniq}")
        results[bench] = {"n": n, "desc": desc, "rows": row,
                          "output": (outs and list(uniq)[0]) or None,
                          "agree": len(uniq) == 1,
                          "build": BUILD_INFO.get(bench, {})}
        print()
    base = baseline()
    print("Bos program taban cizgisi (ms):",
          {k: (round(v, 1) if v else None) for k, v in base.items()})
    (HERE / "results.json").write_text(json.dumps(
        {"results": results, "baseline": base, "baseline_size_kb": BASE_SIZES,
         "repeats": REPEATS, "timeout_s": TIMEOUT_S}, indent=2))
    write_markdown(results, base)
    write_csv(results, REPEATS)
    print("\n-> results.json + RESULTS.md + results.csv")
    return 1 if invalid else 0


def write_csv(results, repeats):
    """Dondurulmus, makinece okunur tablo: kiyas x dil, cikti (checksum) +
    mutabakat kolonuyla.

    RESULTS.md insan icin; baska bir aracla (tablo, grafik, iki turu
    karsilastiran betik) okunacak bir bicim yoktu — FINDINGS'in "donmus 9
    dilli CSV + checksum kolonu" maddesi. `output` her dilin bastigi ortak
    sonuc: bir dil farkli basarsa `agree` 0 olur ve satir GECERSIZDIR
    (hizli ama yanlis bir sonuc kiyas degildir).
    """
    import csv
    with open(HERE / "results.csv", "w", newline="") as fh:
        w = csv.writer(fh, lineterminator="\n")
        # Ilk sekiz kolon 2026-09-11 bicimiyle AYNI (eski okuyucular kirilmasin);
        # yenileri sona: tepe RSS, ikili boyutu, derleme suresi, zaman asimi.
        w.writerow(["kernel", "n", "lang", "best_ms", "median_ms", "output",
                    "agree", "repeats", "rss_kb", "bin_kb", "build_ms",
                    "timeout_s"])
        for bench, r in results.items():
            for lang in LANGS:
                row = r["rows"].get(lang) or {}
                b = (r.get("build") or {}).get(lang) or {}
                w.writerow([bench, r["n"], lang,
                            row.get("best", ""), row.get("median", ""),
                            r["output"] or "", 1 if r["agree"] else 0, repeats,
                            row.get("rss_kb") or "", b.get("kb") or "",
                            b.get("ms") or "", row.get("timeout", "")])


def write_markdown(results, base):
    """results.json'dan okunabilir tablo uret.

    Elle yazilmiyor: depo kokundeki eski `benchmarks/RESULTS.md` elle
    tutuluyordu ve BAYATLADI (2026-05 tarihli, silinmis VM satiri, "best of
    1"). Uretilen tablo bayatlayamaz — kosumun kendisiyle ayni anda dogar.
    """
    lines = ["# Adil dil karsilastirmasi — sonuclar", ""]
    lines.append("Uretildi: `benchmarks/fair/run.py` · %d tekrar, en iyi deger · "
                 "**dusuk = hizli** (ms)." % REPEATS)
    lines.append("")
    lines.append("Her satir ayni algoritmayi ayni veri yapisiyla kosar ve "
                 "**ciktilar dogrulanir** — diller ayni sonucu basmazsa satir "
                 "gecersiz sayilir.")
    lines.append("")
    order = [l for l in LANGS]
    head = "| Dil | " + " | ".join(b for b in results) + " |"
    lines += [head, "|---|" + "---:|" * len(results)]
    for lang in order:
        cells = []
        for b, r in results.items():
            row = r["rows"].get(lang)
            if row and "best" in row:
                cells.append("%.1f" % row["best"])
            elif row and "timeout" in row:
                cells.append("> %.0f s" % row["timeout"])
            else:
                cells.append("—")
        lines.append("| %s | %s |" % (LABEL[lang], " | ".join(cells)))
    lines += ["", "## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)", "",
              "Dusuk = az bellek. Sure ile ayni kosumdan olculur.", "",
              head, "|---|" + "---:|" * len(results)]
    for lang in order:
        cells = []
        for b, r in results.items():
            row = r["rows"].get(lang)
            cells.append(("%.1f" % (row["rss_kb"] / 1024.0)) if row and row.get("rss_kb") else "—")
        lines.append("| %s | %s |" % (LABEL[lang], " | ".join(cells)))
    lines += ["", "## Is yukleri ve cikti mutabakati", "",
              "| Kiyas | BENCH_N | Ne olcer | Ortak cikti |", "|---|---:|---|---|"]
    for b, r in results.items():
        agree = r["output"] if r["agree"] else "**AYRISIYOR — GECERSIZ**"
        lines.append("| `%s` | %s | %s | `%s` |" % (b, r["n"], r["desc"], agree))
    lines += ["", "## Bos program taban cizgisi (ms)", "",
              "Olctugumuz seyin ne kadari surec baslatma? Is yukleri bunu "
              "golgede birakacak kadar buyuk secildi.", ""]
    lines.append("| " + " | ".join(LABEL.get(k, k) for k in base) + " |")
    lines.append("|" + "---:|" * len(base))
    lines.append("| " + " | ".join(("%.1f" % v) if v else "—" for v in base.values()) + " |")
    lines.append("")
    # Derleme suresi ve ikili boyutu: cekirdekler uzerinde ORTANCA (tek bir
    # cekirdegin aykiri degeri tabloyu belirlemesin). Yorumlanan diller
    # (Node/Python) derlenmez; Java .class, C# .dll boyutu calisma zamanini
    # (JVM/CLR) icermez — dogrudan kiyaslanamaz, yalniz bilgi.
    lines += ["## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)", "",
              "| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |",
              "|---|---:|---:|---:|"]
    for lang in order:
        ms = [((r.get("build") or {}).get(lang) or {}).get("ms") for r in results.values()]
        kb = [((r.get("build") or {}).get(lang) or {}).get("kb") for r in results.values()]
        ms = [x for x in ms if x]; kb = [x for x in kb if x]
        lines.append("| %s | %s | %s | %s |" % (
            LABEL[lang],
            ("%.0f" % statistics.median(ms)) if ms else "—",
            ("%.0f" % statistics.median(kb)) if kb else "—",
            ("%.0f" % BASE_SIZES[lang]) if BASE_SIZES.get(lang) else "—"))
    lines.append("")
    (HERE / "RESULTS.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    sys.exit(main())

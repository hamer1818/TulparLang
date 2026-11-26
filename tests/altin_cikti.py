#!/usr/bin/env python3
"""Altin cikti kapisi (2026-11-26) — ornekler CIKTILARIYLA olculur, -O0 ve -O3'te.

NEDEN VAR. `./build.sh test` her ornegi derleyip calistiriyor ama yalnizca
CIKIS KODUNA bakiyor: `print(2+2)` "5" yazsa da yesil. Bu kapi her ornegin
stdout'unu + cikis kodunu `tests/altin/<ad>.out` dosyasiyla karsilastirir ve
bunu IKI optimizasyon seviyesinde yapar (TULPAR_AOT_OPT=0 ve =3). Iki seviye
ayni metni vermek ZORUNDA: farkli cikti, optimizasyona bagli tanimsiz
davranisin (sarkan isaretci, baslatilmamis yuva, yanlis aliasing) belirtisidir
— testin kendisi yanlis olmasa bile.

KAPSAM ELLE TUTULMAZ. Uygun ornek = examples/*.tpr eksi grafik ornekleri
(tame/arcade/scene3d import edenler, build.sh ile ayni desen) eksi build.sh
COMPILE_ONLY_TESTS (listen() bekleyen sunucular). Her uygun ornegin YA altin
dosyasi YA da `tests/altin/KARARSIZ.txt`'te gerekceli bir satiri olmali; ikisi
de yoksa kapi kirmizi — yeni bir ornek sessizce kapsam disinda kalamaz.
KARARSIZ = iki ayni -O3 kosusunda farkli cikti veren (saat, rastgele, adres,
sure olcumu basan) ornek; `--yenile` bunu KENDISI olcerek belirler.

KONTROLLER (kapi her kosuda bunlari da yapar, yani "hicbir sey olcmeyen
yesil" olamaz):
  K1  -O0 ile -O3 ikilileri BAYT BAYT farkli olmali (TULPAR_AOT_OPT gercekten
      okunuyor mu). Ayniysa iki seviye karsilastirmasi bos bir karsilastirma.
  K2  Bir altin dosyasinin bozulmus kopyasiyla kosan kapi KIRMIZI olmali
      (ucdan uca: betik kendini bozuk bir kopya dizinle yeniden calistirir).

Kullanim:
  tests/altin_cikti.py            # kapi
  tests/altin_cikti.py --yenile   # altinlari yeniden uret (degisikligi diff'le incele!)
  tests/altin_cikti.py 02_basics  # tek ornek
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALTIN = os.environ.get("TULPAR_ALTIN_DIZIN") or os.path.join(KOK, "tests", "altin")
KARARSIZ = os.path.join(ALTIN, "KARARSIZ.txt")
# Sure basan satirlar icin ornek basina duzenli ifade -> yerine (SEKME ayrimli):
#   <ad>\t<regex>\t<yerine>
# Ornek DISARIDA birakilmaz; yalnizca o oynak parca maskelenir.
NORMAL = os.path.join(KOK, "tests", "altin", "NORMALLESTIR.txt")
TULPAR = os.path.join(KOK, "tulpar")
GRAFIK = re.compile(r'^\s*import\s+"(tame|arcade|scene3d)"', re.M)
CALISMA_SURESI = 30


def compile_only_listesi():
    metin = open(os.path.join(KOK, "build.sh"), encoding="utf-8").read()
    m = re.search(r"COMPILE_ONLY_TESTS=\((.*?)\)", metin, re.S)
    if not m:
        sys.exit("altin_cikti: build.sh'te COMPILE_ONLY_TESTS bulunamadi")
    return set(re.findall(r'"([^"]+\.tpr)"', m.group(1)))


def uygun_ornekler():
    co = compile_only_listesi()
    out = []
    for ad in sorted(os.listdir(os.path.join(KOK, "examples"))):
        if not ad.endswith(".tpr") or ad in co:
            continue
        yol = os.path.join(KOK, "examples", ad)
        if GRAFIK.search(open(yol, encoding="utf-8", errors="replace").read()):
            continue
        out.append(ad[:-4])
    return out


def kararsiz_oku():
    d = {}
    if os.path.exists(KARARSIZ):
        for satir in open(KARARSIZ, encoding="utf-8"):
            satir = satir.strip()
            if not satir or satir.startswith("#"):
                continue
            ad, _, neden = satir.partition(" ")
            d[ad] = neden.strip()
    return d


def normal_oku():
    d = {}
    if os.path.exists(NORMAL):
        for satir in open(NORMAL, encoding="utf-8"):
            if not satir.strip() or satir.startswith("#"):
                continue
            ad, rx, yerine = satir.rstrip("\n").split("\t")
            d.setdefault(ad, []).append((re.compile(rx), yerine))
    return d


NORMAL_KURALLAR = normal_oku()


def normallestir(ad, metin):
    for rx, yerine in NORMAL_KURALLAR.get(ad, []):
        metin = rx.sub(yerine, metin)
    return metin


def ortam(opt):
    e = dict(os.environ)
    e["TULPAR_AOT_OPT"] = str(opt)
    e["DISPLAY"] = ""
    e["WAYLAND_DISPLAY"] = ""
    e["LC_ALL"] = "C"
    e.pop("TULPAR_PERF_HINTS", None)
    return e


def derle(ad, opt, gecici):
    cikti = os.path.join(gecici, f"{ad}.O{opt}")
    r = subprocess.run(
        [TULPAR, "build", os.path.join("examples", ad + ".tpr"), cikti],
        cwd=KOK, env=ortam(opt), capture_output=True, text=True, timeout=300)
    if r.returncode != 0 or not os.path.exists(cikti):
        return None, (r.stdout + r.stderr)[-2000:]
    return cikti, ""


def calistir(ikili, ad, opt):
    girdi_yolu = os.path.join(KOK, "examples", "inputs", ad + ".txt")
    girdi = open(girdi_yolu, "rb").read() if os.path.exists(girdi_yolu) else b""
    try:
        r = subprocess.run([ikili], cwd=KOK, env=ortam(opt), input=girdi,
                           capture_output=True, timeout=CALISMA_SURESI)
        rc = r.returncode
        out = r.stdout
    except subprocess.TimeoutExpired as t:
        rc = "zaman-asimi"
        out = t.stdout or b""
    return normallestir(ad, out.decode("utf-8", errors="replace") + f"\n[cikis={rc}]\n")


def ornek_kos(ad, gecici, tekrar=False):
    """{opt: metin} ya da hata metni doner."""
    sonuc = {}
    for opt in (3, 0):
        ikili, hata = derle(ad, opt, gecici)
        if ikili is None:
            return None, f"-O{opt} derlenemedi:\n{hata}"
        sonuc[opt] = calistir(ikili, ad, opt)
        if opt == 3 and tekrar:
            sonuc["3b"] = calistir(ikili, ad, opt)
    return sonuc, ""


def fark_ozeti(a, b, etiket_a, etiket_b):
    import difflib
    satirlar = list(difflib.unified_diff(a.splitlines(), b.splitlines(),
                                         etiket_a, etiket_b, lineterm="", n=1))
    return "\n".join(satirlar[:30])


def paket_ozeti(yol, opt):
    """tests/*.test.tpr paketini -O<opt>'de kosar; (ozet satiri, rc)."""
    try:
        r = subprocess.run([TULPAR, yol], cwd=KOK, env=ortam(opt), capture_output=True,
                           text=True, errors="replace", timeout=300)
    except subprocess.TimeoutExpired:
        return None, "zaman-asimi"
    ozet = [l.strip() for l in r.stdout.splitlines() if l.strip().startswith("Tests:")]
    return (ozet[-1] if ozet else None), r.returncode


def paketler_iki_seviye(isci):
    """Her regresyon paketi -O0'da da gecmeli ve -O3 ile AYNI ozeti vermeli.
    Ornekler yalniz cikti basar; paketler kendi assert'leriyle olcer — yani
    -O0/-O3 karsilastirmasinin asil genisligi burada."""
    paketler = sorted(f for f in os.listdir(os.path.join(KOK, "tests")) if f.endswith(".test.tpr"))
    hatalar = []

    def bir(f):
        yol = os.path.join("tests", f)
        o3 = paket_ozeti(yol, 3)
        o0 = paket_ozeti(yol, 0)
        return f, o3, o0

    with cf.ThreadPoolExecutor(isci) as ex:
        for f, (oz3, rc3), (oz0, rc0) in ex.map(bir, paketler):
            if oz0 is None or rc0 != 0 or "Fail: 0" not in oz0:
                hatalar.append(f"{f}: -O0'da gecmedi (rc={rc0}, ozet={oz0!r}; -O3: {oz3!r})")
            elif oz3 != oz0:
                hatalar.append(f"{f}: -O3 ve -O0 ozetleri farkli ({oz3!r} / {oz0!r})")
    return len(paketler), hatalar


def kontrol_k1(gecici):
    """K1: -O0 ve -O3 ikilileri farkli mi (degisken okunuyor mu)."""
    ad = "02_basics" if os.path.exists(os.path.join(KOK, "examples", "02_basics.tpr")) else uygun_ornekler()[0]
    a, _ = derle(ad, 3, gecici)
    b, _ = derle(ad, 0, gecici)
    if not a or not b:
        return False, "K1: kontrol ornegi derlenemedi"
    if open(a, "rb").read() == open(b, "rb").read():
        return False, "K1: -O0 ve -O3 ikilileri AYNI — TULPAR_AOT_OPT okunmuyor, iki seviye karsilastirmasi bos"
    return True, ""


def main():
    argv = sys.argv[1:]
    yenile = "--yenile" in argv
    secili = [a for a in argv if not a.startswith("--")]
    if not os.path.exists(TULPAR):
        sys.exit("altin_cikti: ./tulpar yok (once ./build.sh)")
    os.makedirs(ALTIN, exist_ok=True)
    uygun = uygun_ornekler()
    kararsiz = kararsiz_oku()
    hedef = secili or uygun
    isci = max(1, (os.cpu_count() or 2) // 2)
    hatalar = []

    with tempfile.TemporaryDirectory(prefix="altin_") as gecici:
        ok, msg = kontrol_k1(gecici)
        if not ok:
            print("FAIL: altin_cikti —", msg)
            return 1

        if yenile:
            yeni_kararsiz = {}
            with cf.ThreadPoolExecutor(isci) as ex:
                gorevler = {ex.submit(ornek_kos, ad, gecici, True): ad for ad in hedef}
                for g in cf.as_completed(gorevler):
                    ad = gorevler[g]
                    sonuc, hata = g.result()
                    altin_yol = os.path.join(ALTIN, ad + ".out")
                    if sonuc is None:
                        hatalar.append(f"{ad}: {hata}")
                        continue
                    if sonuc[3] != sonuc["3b"]:
                        yeni_kararsiz[ad] = kararsiz.get(ad) or "iki ayni -O3 kosusu farkli cikti verdi (olculdu)"
                        if os.path.exists(altin_yol):
                            os.remove(altin_yol)
                        continue
                    if sonuc[3] != sonuc[0]:
                        hatalar.append(f"{ad}: -O3 ile -O0 FARKLI (altin yazilmadi)\n" +
                                       fark_ozeti(sonuc[3], sonuc[0], "-O3", "-O0"))
                        continue
                    open(altin_yol, "w", encoding="utf-8", newline="").write(sonuc[3])
                    kararsiz.pop(ad, None)
            if not secili:
                kararsiz = yeni_kararsiz
            else:
                kararsiz.update(yeni_kararsiz)
            with open(KARARSIZ, "w", encoding="utf-8") as f:
                f.write("# Altin cikti kapsami disindaki ornekler: ad + gerekce.\n"
                        "# `tests/altin_cikti.py --yenile` iki -O3 kosusunu karsilastirip\n"
                        "# kendisi yazar; gerekce elle iyilestirilebilir (korunur).\n")
                for ad in sorted(kararsiz):
                    f.write(f"{ad} {kararsiz[ad]}\n")
            for h in hatalar:
                print("HATA:", h)
            print(f"altin yenilendi: {len([a for a in hedef if a not in kararsiz]) - len(hatalar)} altin, "
                  f"{len(kararsiz)} kararsiz, {len(hatalar)} hata")
            return 1 if hatalar else 0

        # --- kapi ---
        eksik = [ad for ad in uygun if ad not in kararsiz
                 and not os.path.exists(os.path.join(ALTIN, ad + ".out"))]
        for ad in eksik:
            hatalar.append(f"{ad}: ne altin dosyasi ne KARARSIZ satiri var "
                           "(yeni ornek? `tests/altin_cikti.py --yenile {ad}`)")
        olu = [f[:-4] for f in os.listdir(ALTIN) if f.endswith(".out") and f[:-4] not in uygun]
        for ad in olu:
            hatalar.append(f"{ad}: altin dosyasi var ama ornek artik uygun degil/yok — sil")

        kos = [ad for ad in hedef if ad not in kararsiz and os.path.exists(os.path.join(ALTIN, ad + ".out"))]
        karsilastirilan = 0
        with cf.ThreadPoolExecutor(isci) as ex:
            gorevler = {ex.submit(ornek_kos, ad, gecici): ad for ad in kos}
            for g in cf.as_completed(gorevler):
                ad = gorevler[g]
                sonuc, hata = g.result()
                if sonuc is None:
                    hatalar.append(f"{ad}: {hata}")
                    continue
                altin = open(os.path.join(ALTIN, ad + ".out"), encoding="utf-8", newline="").read()
                for opt in (3, 0):
                    if sonuc[opt] != altin:
                        hatalar.append(f"{ad}: -O{opt} ciktisi altindan farkli\n" +
                                       fark_ozeti(altin, sonuc[opt], "altin", f"-O{opt}"))
                karsilastirilan += 1

        # K2: kapinin KENDISI bozuk bir altini yakaliyor mu — ucdan uca: bu
        # betik tek ornekle, altini bozulmus bir kopya dizinle yeniden kosar
        # ve kirmizi + "altindan farkli" bekler.
        if kos and not secili and not os.environ.get("TULPAR_ALTIN_DIZIN"):
            import shutil
            ornek = sorted(kos)[0]
            kopya = os.path.join(gecici, "k2_altin")
            shutil.copytree(ALTIN, kopya)
            yol = os.path.join(kopya, ornek + ".out")
            metin = open(yol, encoding="utf-8", newline="").read()
            open(yol, "w", encoding="utf-8", newline="").write("BOZUK\n" + metin)
            r = subprocess.run([sys.executable, os.path.abspath(__file__), ornek],
                               env=dict(os.environ, TULPAR_ALTIN_DIZIN=kopya),
                               capture_output=True, text=True)
            if r.returncode == 0 or "altindan farkli" not in r.stdout:
                hatalar.append(f"K2: bozulmus altin ({ornek}) kapiyi kirmizi yapmadi:\n{r.stdout[-800:]}")

    paket_n = 0
    if not secili:
        paket_n, ph = paketler_iki_seviye(max(1, os.cpu_count() or 2))
        hatalar += ph

    for h in hatalar:
        print("FAIL:", h)
    if hatalar:
        print(f"FAIL: altin_cikti — {len(hatalar)} sorun")
        return 1
    print(f"PASS: altin_cikti — {karsilastirilan} ornek -O3 ve -O0'da altinla ayni "
          f"({len(kararsiz)} kararsiz gerekceli disarida); K1 -O0/-O3 ikilileri farkli"
          + (f"; K2 bozuk altin kirmizi; {paket_n} paket -O0'da -O3 ile ayni ozet" if not secili else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())

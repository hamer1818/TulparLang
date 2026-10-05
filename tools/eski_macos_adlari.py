#!/usr/bin/env python3
"""ESKI macOS VARLIK ADLARI (`*-macos-universal`) KALDIRILABILIR MI?

2026-10-05'ten (v3.39.0, #463) beri macOS varliklari durust adla
(`tulpar-macos-arm64`, `libtulpar_runtime-macos-arm64.a`) yayinlaniyor; ayni
dosyalar gecis icin eski adla da kopyalaniyor (build.yml "Eski macOS adlari").
Eski adi indirenler:

  * `tulpar update` v3.38.5 ve ONCESI (src/cli/update_cmd.cpp; v3.39.0 ilk
    yeni adli guncelleyici). Eski ad kalkinca bunlar 404 ile, hicbir dosyaya
    dokunmadan duser (indir -> dogrula -> degistir sirasi) — kullanici
    install.sh ile yeniden kurmak zorunda kalir.
  * sitedeki install.sh (tulpar-lang-web; bu depodan DEGISTIRILMEZ).
  * motor CI'i (tulpar-engine tools/tulpar_indir.sh) — 2026-10-05'te yeni ada
    gecti.

Bu betik RELEASING.md'deki kaldirma kosulunu OLCER; tahmin etmez. Hepsi
saglaninca cikis 0 ("KALDIRILABILIR"), degilse 1. Ag gerekir (GitHub API +
site); CI'da kosmaz, kaldirma karari verilmeden once elle kosulur:

    python3 tools/eski_macos_adlari.py            # GITHUB_TOKEN varsa kullanir
"""
import datetime as dt
import json
import os
import sys
import urllib.request

DEPO = "hamer1818/TulparLang"
MOTOR = "hamer1818/tulpar-engine"
INSTALL_SH = "https://tulparlang.dev/install.sh"
ESKI = ("tulpar-macos-universal", "libtulpar_runtime-macos-universal.a")
YENI = ("tulpar-macos-arm64", "libtulpar_runtime-macos-arm64.a")
# v3.39.0 2026-10-05'te yayinlandi; en erken kaldirma tarihi +60 gun.
EN_ERKEN = dt.date(2026, 12, 4)
PENCERE_GUN = 30          # son N gunde yayinlanan surumlerin indirmeleri
ESIK = 0                  # eski adli indirme sayisi bu ve alti


def getir(url, ham=False):
    istek = urllib.request.Request(url, headers={"User-Agent": "tulpar-eski-ad-olcum"})
    tok = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if tok and url.startswith("https://api.github.com/"):
        istek.add_header("Authorization", "Bearer " + tok)
    if ham:
        istek.add_header("Accept", "application/vnd.github.raw")
    with urllib.request.urlopen(istek, timeout=30) as y:
        veri = y.read()
    return veri.decode("utf-8", "replace") if ham else json.loads(veri)


def main():
    bugun = dt.datetime.now(dt.timezone.utc)
    sinir = bugun - dt.timedelta(days=PENCERE_GUN)
    surumler = []
    for sayfa in range(1, 6):
        s = getir(f"https://api.github.com/repos/{DEPO}/releases?per_page=100&page={sayfa}")
        if not s:
            break
        surumler += s
    eski_top = yeni_top = 0
    satirlar = []
    for r in surumler:
        t = dt.datetime.fromisoformat(r["published_at"].replace("Z", "+00:00"))
        sayac = {a["name"]: a["download_count"] for a in r.get("assets", [])}
        e = sum(sayac.get(n, 0) for n in ESKI)
        y = sum(sayac.get(n, 0) for n in YENI)
        # Yalniz IKI adi birden tasiyan surumler (v3.39.0+): eski surumlerin
        # eski adli dosyalari kalici, onlari indirenler kaldirmadan etkilenmez.
        # Burada eski adi indiren = `latest`i eski adla isteyen guncelleyici
        # ya da install.sh.
        if t >= sinir and YENI[0] in sayac:
            eski_top += e
            yeni_top += y
        if e or y:
            satirlar.append(f"  {r['tag_name']:10s} {t.date()}  eski={e:<4d} yeni={y}")
    print(f"Son {PENCERE_GUN} gunde yayinlanan, iki adi da tasiyan surumler: eski ad {eski_top} indirme, yeni ad {yeni_top}")
    print("Indirmesi olan surumler (eski = iki eski varligin toplami):")
    print("\n".join(satirlar[:20]) or "  (yok)")

    kosul = []
    kosul.append((bugun.date() >= EN_ERKEN, f"tarih {bugun.date()} >= {EN_ERKEN} (v3.39.0 + 60 gun)"))
    kosul.append((eski_top <= ESIK, f"son {PENCERE_GUN} gunde eski ad indirmesi {eski_top} <= {ESIK}"))
    try:
        sh = getir(INSTALL_SH, ham=True)
        ok = YENI[0] in sh and ESKI[0] not in sh
        kosul.append((ok, f"{INSTALL_SH} yeni adi kullaniyor, eskiyi icermiyor"))
    except Exception as hata:  # noqa: BLE001 — olcum yoksa kosul saglanmamis sayilir
        kosul.append((False, f"{INSTALL_SH} okunamadi ({hata})"))
    try:
        mt = getir(f"https://api.github.com/repos/{MOTOR}/contents/tools/tulpar_indir.sh", ham=True)
        ok = YENI[0] in mt and f"ikili={ESKI[0]}" not in mt
        kosul.append((ok, "motor tools/tulpar_indir.sh (main) yeni adi indiriyor"))
    except Exception as hata:  # noqa: BLE001
        kosul.append((False, f"motor tulpar_indir.sh okunamadi ({hata})"))

    print("\nKaldirma kosulu (RELEASING.md 'Eski macOS adlari'):")
    for ok, metin in kosul:
        print(f"  [{'x' if ok else ' '}] {metin}")
    if all(ok for ok, _ in kosul):
        print("\nKALDIRILABILIR — RELEASING.md'deki listeyi uygula.")
        return 0
    print("\nHENUZ DEGIL — eski adlar yayinlanmaya devam eder.")
    return 1


if __name__ == "__main__":
    sys.exit(main())

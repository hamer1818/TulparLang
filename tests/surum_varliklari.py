#!/usr/bin/env python3
"""Sürüm varlıkları ↔ `tulpar update` tutarlılığı (kaynak denetimi).

NEDEN
-----
`tulpar update` Windows'ta `tulpar-windows-x64.exe` ve MinGW DLL'lerini AYRI
sürüm varlıkları olarak indiriyordu; sürümler yalnız `tulpar-windows-x64.zip`
yayınlıyordu. Yani komut Windows'ta indirme adımında düşüyordu — ve sitedeki
`install.ps1` aynı adları beklediği için tek satırlık kurulum da
("Release ... icinde 'tulpar-windows-x64.exe' bulunamadi"). Üstelik
güncelleyicinin DLL listesi, ikilinin gerçekten içe aktardığı beş DLL'den
ikisini (OpenSSL) içermiyordu. Ölçüldü 2026-09-27 (v3.15.6 / v3.16.1 varlık
listeleri). Üç liste üç ayrı yerde elle tutuluyordu ve hiçbir şey onları
karşılaştırmıyordu.

Bu kapı karşılaştırıyor:
  1. update_cmd.cpp'nin indirdiği HER varlık, build.yml'in yayınladığı
     (`files:`) listede VE SHA256SUMS döngüsünde var.
  2. Güncelleyicinin Windows DLL'leri == CI DLL kapısının `$bundledDlls`'ı
     (ikilinin içe aktardığı ve paketlenen DLL'ler — tek doğruluk kaynağı).
  3. Yayınlanan her dosya SHA256SUMS'ta (doğrulanamayan varlık olmasın).
  4. GEÇİŞ (2026-11-22): tulpar.exe Windows'ta tamamen statik oldu ve güncelleyici
     artık DLL indirmiyor; ama ÖNCEKİ sürümlerin güncelleyicileri beş DLL'i adıyla
     ZORUNLU indiriyor. O adlar yayında ve SHA256SUMS'ta kalmalı — yoksa eski
     kurulumların `tulpar update`'i 404'te durur.

POZİTIF KONTROL (her koşumda): listeye sahte bir varlık eklenince 1. denetim
onu adıyla eksik saymalı; her ayrıştırma boş değil.
"""
import os
import re
import sys

# Bu surumden ONCEKI update_cmd.cpp'nin Windows DLL listesi (v3.47 ve oncesi). Silmek eski kurulumlarin guncellemesini kirar.
GECIS_WINDOWS = ["libwinpthread-1.dll", "zlib1.dll", "libzstd.dll", "libssl-3-x64.dll", "libcrypto-3-x64.dll"]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as fh:
        return fh.read()


def update_assets():
    src = read("src/cli/update_cmd.cpp")
    m = re.search(r"assets_for_platform\(\)\s*\{(.*?)\n\}", src, re.S)
    if not m:
        return None, None
    body = m.group(1)
    all_names = re.findall(r'\{\s*"([^"]+)"\s*,\s*"[^"]+"\s*,\s*(?:true|false)\s*\}', body)
    win = re.search(r"#if defined\(_WIN32\)(.*?)#elif", body, re.S)
    win_names = re.findall(r'\{\s*"([^"]+)"\s*,', win.group(1)) if win else []
    # Android kiti assets_for_platform disinda (ayri kurulur, update_cmd.cpp
    # install_android_kit) ama ayni kurala tabi: yayinda VE SHA256SUMS'ta olmali.
    kit = re.search(r'kAndroidKit\s*=\s*"([^"]+)"', src)
    if kit:
        all_names.append(kit.group(1))
    return all_names, win_names


def workflow_lists():
    wf = read(".github/workflows/build.yml")
    files = []
    m = re.search(r"\n\s*files: \|\n((?:\s{12}\S.*\n)+)", wf)
    if m:
        files = [ln.strip() for ln in m.group(1).splitlines() if ln.strip()]
    sums = []
    m = re.search(r": > SHA256SUMS\.txt\s*\n\s*for f in (.*?); do", wf, re.S)
    if m:
        sums = [t for t in re.split(r"[\s\\]+", m.group(1)) if t]
    dlls = None   # None = liste BULUNAMADI (ayristirma bozuk); [] = bildirilmis bos liste (tamamen statik ikili)
    m = re.search(r"\$bundledDlls\s*=\s*@\((.*?)\)", wf, re.S)
    if m:
        dlls = re.findall(r"'([^']+)'", m.group(1))
    return files, sums, dlls


def check(update_all, update_win, files, sums, dlls):
    errs = []
    published = set(files)
    for a in update_all:
        if a not in published:
            errs.append("`tulpar update` indiriyor ama surumde YAYINLANMIYOR: %s" % a)
        if a not in sums:
            errs.append("`tulpar update` indiriyor ama SHA256SUMS'ta YOK: %s" % a)
    upd_dlls = {a for a in update_win if a.lower().endswith(".dll")}
    if upd_dlls != set(dlls):
        errs.append("Windows DLL listesi ayrisik — update_cmd: %s | CI $bundledDlls: %s"
                    % (sorted(upd_dlls), sorted(dlls)))
    for g in GECIS_WINDOWS:
        if g not in published or g not in sums:
            errs.append("eski guncelleyicilerin indirdigi gecis varligi yayinda/SHA256SUMS'ta YOK: %s" % g)
    for f in files:
        if f.startswith("SHA256SUMS"):
            continue
        if f not in sums:
            errs.append("yayinlaniyor ama SHA256SUMS'ta YOK (dogrulanamaz): %s" % f)
    return errs


def main():
    update_all, update_win = update_assets()
    files, sums, dlls = workflow_lists()
    if not update_all or not update_win or not files or not sums or dlls is None:
        print("surum varlik denetimi: ayristirma BOS dondu (update=%s win=%s files=%s "
              "sums=%s dlls=%s) — kapi hicbir sey olcmuyor"
              % (len(update_all or []), len(update_win or []), len(files), len(sums), -1 if dlls is None else len(dlls)))
        return 1
    # Pozitif kontrol: sahte varlik eksik sayilmali.
    probe = check(update_all + ["__olmayan_varlik__.bin"], update_win, files, sums, dlls)
    if not any("__olmayan_varlik__.bin" in e for e in probe):
        print("surum varlik denetimi: pozitif kontrol dustu — sahte varlik eksik sayilmadi")
        return 1
    # Pozitif kontrol (gecis): bir gecis adi yayindan cikarilinca 4. denetim onu adiyla saymali.
    probe = check(update_all, update_win, [f for f in files if f != GECIS_WINDOWS[1]], sums, dlls)
    if not any(GECIS_WINDOWS[1] in e and "gecis" in e for e in probe):
        print("surum varlik denetimi: gecis pozitif kontrolu dustu — yayindan cikan %s sayilmadi" % GECIS_WINDOWS[1])
        return 1
    errs = check(update_all, update_win, files, sums, dlls)
    if errs:
        for e in errs:
            print("HATA: " + e)
        return 1
    print("surum varlik denetimi temiz (%d update varligi, %d yayin dosyasi, %d Windows DLL, %d gecis varligi; "
          "pozitif kontroller gecti)" % (len(update_all), len(files), len(dlls), len(GECIS_WINDOWS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""MSYS2 mingw64 paketlerinden yerel bir WINDOWS SYSROOT kurar.

Neden bu var
------------
Tulpar'i Windows'a capraz derlemek icin HEDEF platformun LLVM gelistirme
agacina (basliklar + libLLVM*.a + lib/cmake/llvm) ihtiyac var. Bunu uretmenin
iki yolu var: LLVM'i capraz derlemek (~1 saat) ya da MSYS2'nin HAZIR paketini
kullanmak. Ikincisi secildi cunku surumler birebir tutuyor:

  host LLVM 22.1.8  ==  MSYS2 mingw-w64-x86_64-llvm 22.1.8
  Arch mingw GCC 16.2.0  ==  MSYS2 mingw64 GCC 16.2.0
  Arch mingw-w64-crt  ve  MSYS2 mingw64  =>  ikisi de MSVCRT (ucrt DEGIL)

ABI karismiyor. UCRT tabanli bir toolchain (MSYS2 ucrt64, llvm-mingw) buraya
KARISTIRILMAZ: ayni programda iki C calisma zamani sessiz bozulma demektir.

Ne indiriyor
------------
  * llvm + baglantili kutuphaneler (zlib/zstd/libxml2/...): DERLEME icin
  * openssl: TLS (TULPAR_HAS_TLS) icin
  * gcc + binutils + crt/headers/winpthreads: WINE tarafinda calisacak
    LINKLEYICI SURUCUSU icin (uretilen .o dosyasini .exe'ye baglayan adim
    Windows tarafinda kosar; bkz. windows/wine_env.sh).

Surum sabitleme
---------------
Ilk kosuda cozulmus paket kumesi `windows/packages.lock` dosyasina
(ad + surum + sha256) yazilir ve SONRAKI kosular o kilidi kullanir. MSYS2
deposu hareketli bir hedeftir; kilit olmadan "bir gun calisiyordu" sinifi
hatalar gelir (ayni sinifin depodaki ikizleri: Tuzaklar 8am / 8ao).
`--refresh` kilidi yeniden cozer.

Kullanim
--------
    python3 windows/setup_sysroot.py            # kilide gore kur
    python3 windows/setup_sysroot.py --refresh  # depodan yeniden coz
    python3 windows/setup_sysroot.py --list     # ne kurulacak, indirmeden
"""

import argparse
import hashlib
import io
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request

REPO = "https://repo.msys2.org/mingw/mingw64"
DB_URL = f"{REPO}/mingw64.db"
PREFIX = "mingw-w64-x86_64-"

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(HERE, "dist")          # sysroot koku -> dist/mingw64/...
CACHE = os.path.join(HERE, "cache")        # indirilen .pkg.tar.zst
LOCK = os.path.join(HERE, "packages.lock")

# Kok paketler. Bagimliliklar depo veritabanindan OZYINELEMELI cozulur --
# elle tutulan bir liste bir gun eksik kalir ve hata link asamasinda cikar.
ROOTS = [
    PREFIX + "llvm",      # hedef LLVM gelistirme agaci (statik bilesenler dahil)
    PREFIX + "openssl",   # TLS
    PREFIX + "gcc",       # Wine tarafindaki linkleyici surucusu (g++ .exe)
]


def log(msg):
    print(f"[sysroot] {msg}", flush=True)


def fetch(url):
    with urllib.request.urlopen(url) as r:
        return r.read()


def decompress(blob):
    """mingw64.db sikistirmasi SABIT DEGIL: uzun sure gzip'ti, bugun zstd.
    Sihirli sayiya bak — uzantiya ya da gecmise guvenme."""
    if blob[:4] == b"\x28\xb5\x2f\xfd":      # zstd
        from compression import zstd
        return zstd.decompress(blob)
    if blob[:2] == b"\x1f\x8b":              # gzip
        import gzip
        return gzip.decompress(blob)
    if blob[:6] == b"\xfd7zXZ\x00":          # xz
        import lzma
        return lzma.decompress(blob)
    return blob                               # ciplak tar


def parse_db(blob):
    """mingw64.db (sikistirilmis tar) -> {paket adi: desc alanlari}."""
    pkgs = {}
    with tarfile.open(fileobj=io.BytesIO(decompress(blob)), mode="r:") as tf:
        for m in tf.getmembers():
            if not m.name.endswith("/desc"):
                continue
            text = tf.extractfile(m).read().decode("utf-8", "replace")
            fields, key = {}, None
            for line in text.splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line.strip() and key:
                    fields[key].append(line.strip())
            name = fields.get("NAME", [None])[0]
            if name:
                pkgs[name] = fields
    return pkgs


def dep_name(spec):
    """'foo>=1.2' / 'foo=1.2' / 'foo' -> 'foo'."""
    for sep in (">=", "<=", "=", ">", "<"):
        if sep in spec:
            return spec.split(sep, 1)[0]
    return spec


def resolve(pkgs, roots):
    """Koklerden ozyinelemeli bagimlilik kapanisi (PROVIDES eslemesiyle)."""
    provides = {}
    for name, f in pkgs.items():
        for p in f.get("PROVIDES", []):
            provides.setdefault(dep_name(p), name)
    out, stack, missing = {}, list(roots), []
    while stack:
        want = stack.pop()
        real = want if want in pkgs else provides.get(want)
        if real is None:
            missing.append(want)
            continue
        if real in out:
            continue
        f = pkgs[real]
        out[real] = f
        stack.extend(dep_name(d) for d in f.get("DEPENDS", []))
    if missing:
        log(f"UYARI: depoda bulunamayan bagimlilik: {sorted(set(missing))}")
    return out


def read_lock():
    if not os.path.exists(LOCK):
        return None
    entries = []
    with open(LOCK, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) == 3:
                entries.append({"filename": parts[0], "version": parts[1], "sha256": parts[2]})
    return entries or None


def write_lock(entries):
    with open(LOCK, "w", encoding="utf-8") as fh:
        fh.write("# MSYS2 mingw64 paketleri — SURUM SABITLENMISTIR.\n")
        fh.write("# Uretim: python3 windows/setup_sysroot.py --refresh\n")
        fh.write("# Bicim: <dosya adi> <surum> <sha256>\n")
        for e in sorted(entries, key=lambda x: x["filename"]):
            fh.write(f"{e['filename']} {e['version']} {e['sha256']}\n")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def download(entry):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, entry["filename"])
    if os.path.exists(path) and sha256_file(path) == entry["sha256"]:
        return path, False
    log(f"indiriliyor: {entry['filename']}")
    data = fetch(f"{REPO}/{entry['filename']}")
    with open(path, "wb") as fh:
        fh.write(data)
    got = sha256_file(path)
    if got != entry["sha256"]:
        # Kilit ile depo ayrismis: SESSIZ gecmez.
        raise SystemExit(
            f"[sysroot] HATA: sha256 tutmuyor: {entry['filename']}\n"
            f"  beklenen {entry['sha256']}\n  gelen     {got}\n"
            f"  (paket depoda guncellenmis olabilir; --refresh ile kilidi tazele)")
    return path, True


def extract(path):
    # .pkg.tar.zst -> dist/ (paketin icinde zaten 'mingw64/' oneki var).
    # bsdtar tercih: zstd'yi kendi cozer, MSYS2'nin .PKGINFO/.MTREE gibi
    # meta dosyalari --exclude ile atilir.
    os.makedirs(DIST, exist_ok=True)
    subprocess.run(
        ["bsdtar", "-xf", path, "-C", DIST,
         "--exclude", ".PKGINFO", "--exclude", ".BUILDINFO",
         "--exclude", ".MTREE", "--exclude", ".INSTALL"],
        check=True)


# --- HOST capraz GCC (sudo'suz) --------------------------------------------
# Tercih edilen host derleyicisi Arch'in mingw-w64-gcc'sidir: MSYS2'nin
# LLVM'ini derleyen GCC ile AYNI surum (16.2.0) ve ayni MSVCRT. Bu ONEMLI bir
# tercih degil, OLCULMUS bir gereklilik: host clang ile derlenen nesneler
# GCC'nin libstdc++'iyla linklenince "duplicate section has different size"
# uyarilari cikiyor ve `catch` icinden atilan bir hata `call()` sinirini
# gecerken surec cokuyor (olculdu 2026-09-18; ayni program GCC ile derlenmis
# ikilide dogru calisiyor). Bkz. Tuzaklar 9f.
#
# `sudo pacman -S mingw-w64-gcc` en temiz yol. Sudo yoksa bu mod paketleri
# kullanicinin KENDI aynalarindan (pacman -Sp) indirip windows/dist/host/
# altina acar; GCC yollarini surucunun konumundan turettigi icin oradan
# calisir. Sistem geneline hicbir sey yazilmaz.
HOST_PKGS = ["mingw-w64-gcc", "mingw-w64-binutils", "mingw-w64-crt",
             "mingw-w64-headers", "mingw-w64-winpthreads"]
HOST_DIR = os.path.join(HERE, "dist", "host")


def setup_host_gcc():
    if shutil.which("x86_64-w64-mingw32-g++"):
        log("sistemde capraz GCC zaten var (pacman ile kurulmus) — atlaniyor")
        return 0
    log("capraz GCC paketleri cozuluyor (pacman -Sp, sudo gerekmez)")
    try:
        out = subprocess.run(["pacman", "-Sp", *HOST_PKGS], check=True,
                             capture_output=True, text=True).stdout
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        raise SystemExit(f"[sysroot] HATA: pacman -Sp basarisiz: {e}")
    urls = [u.strip() for u in out.splitlines() if u.startswith(("http://", "https://"))]
    if not urls:
        raise SystemExit("[sysroot] HATA: pacman hicbir URL vermedi")
    os.makedirs(CACHE, exist_ok=True)
    os.makedirs(HOST_DIR, exist_ok=True)
    for i, url in enumerate(urls, 1):
        name = url.rsplit("/", 1)[-1]
        path = os.path.join(CACHE, name)
        if not os.path.exists(path):
            log(f"indiriliyor: {name}")
            with open(path, "wb") as fh:
                fh.write(fetch(url))
        subprocess.run(["bsdtar", "-xf", path, "-C", HOST_DIR,
                        "--exclude", ".PKGINFO", "--exclude", ".BUILDINFO",
                        "--exclude", ".MTREE", "--exclude", ".INSTALL"], check=True)
        log(f"({i}/{len(urls)}) acildi: {name}")
    gpp = os.path.join(HOST_DIR, "usr", "bin", "x86_64-w64-mingw32-g++")
    log(f"capraz g++: {'VAR' if os.path.exists(gpp) else 'YOK (!)'} -> {gpp}")
    if os.path.exists(gpp):
        v = subprocess.run([gpp, "-dumpversion"], capture_output=True, text=True)
        log(f"surum: {v.stdout.strip() or v.stderr.strip()}")
    return 0


def main():
    ap = argparse.ArgumentParser(description="Windows capraz derleme sysroot'u kurar")
    ap.add_argument("--refresh", action="store_true", help="kilidi depodan yeniden coz")
    ap.add_argument("--list", action="store_true", help="paket kumesini yaz, indirme")
    ap.add_argument("--host-gcc", action="store_true",
                    help="capraz GCC'yi windows/dist/host/ altina kur (sudo'suz)")
    args = ap.parse_args()

    if args.host_gcc:
        return setup_host_gcc()

    lock = None if args.refresh else read_lock()
    if lock is None:
        log(f"depo veritabani cekiliyor: {DB_URL}")
        pkgs = parse_db(fetch(DB_URL))
        log(f"depoda {len(pkgs)} paket")
        chosen = resolve(pkgs, ROOTS)
        lock = [{
            "filename": f["FILENAME"][0],
            "version": f["VERSION"][0],
            "sha256": f["SHA256SUM"][0],
        } for f in chosen.values()]
        log(f"cozulen kapanis: {len(lock)} paket")
        if not args.list:
            write_lock(lock)
            log(f"kilit yazildi: {os.path.relpath(LOCK, os.getcwd())}")
    else:
        log(f"kilit kullaniliyor: {len(lock)} paket ({os.path.relpath(LOCK, os.getcwd())})")

    if args.list:
        for e in sorted(lock, key=lambda x: x["filename"]):
            print(f"  {e['filename']}")
        return 0

    if not shutil.which("bsdtar"):
        raise SystemExit("[sysroot] HATA: bsdtar gerekli (pacman -S libarchive)")

    total = len(lock)
    for i, e in enumerate(sorted(lock, key=lambda x: x["filename"]), 1):
        path, fresh = download(e)
        extract(path)
        log(f"({i}/{total}) {'indirildi+' if fresh else 'onbellek+'}acildi: {e['filename']}")

    root = os.path.join(DIST, "mingw64")
    llvm_cmake = os.path.join(root, "lib", "cmake", "llvm", "LLVMConfig.cmake")
    gpp = os.path.join(root, "bin", "g++.exe")
    log("---")
    log(f"sysroot: {root}")
    log(f"LLVMConfig.cmake : {'VAR' if os.path.exists(llvm_cmake) else 'YOK (!)'}")
    log(f"g++.exe (Wine)   : {'VAR' if os.path.exists(gpp) else 'YOK (!)'}")
    static = os.path.join(root, "lib", "libLLVMCore.a")
    log(f"libLLVMCore.a    : {'VAR' if os.path.exists(static) else 'YOK (!)'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

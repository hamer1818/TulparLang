#!/usr/bin/env python3
"""Gömülü stdlib TAZELİĞİ: sınanan ikili bu ağacın lib/*.tpr'sini mi taşıyor?

NEDEN
-----
`lib/*.tpr` derleyiciye `cmake/EmbedLibraries.cmake` ile gömülüyor ve içerik
YAPILANDIRMA anında okunuyordu. `cmake --build` bu dosyaları izlemediği için
bir stdlib düzeltmesi (ör. `lib/test.tpr`) derleme "başarılı" bittiği hâlde
ikiliye hiç girmiyordu: süitler ESKİ kütüphaneyi koşturuyor, düzeltme "işe
yaramamış" görünüyordu. Ölçüldü (2026-09-27): lib/test.tpr'ye işaret eklenip
`cmake --build build-linux` koşuldu — "[100%] Built target tulpar", ama işaret
ne src/embedded_libs.h'de ne ikilide vardı. (Tuzaklar 7; motor deposu
docs/TUZAKLAR.md 8be.)

Düzeltme EmbedLibraries.cmake'te: her gömülü dosya CMAKE_CONFIGURE_DEPENDS'e
giriyor. Bu kapı onu İKİ ayakla ölçüyor, çünkü her ayak tek başına boşa
çıkabilir:

  1) MEKANİZMA — derleme dizininin ürettiği derleme sistemi (Makefile ya da
     Ninja) her gömülü lib/*.tpr'yi yeniden yapılandırma bağımlılığı olarak
     listeliyor mu? Satır silinirse burası kızarır, ikili taze olsa bile.
     Derleme dizini yoksa (ör. elle kopyalanmış ikili) GÖRÜNÜR atlanır.
  2) ETKİ — sınanan ikili (varsayılan ./tulpar) her lib dosyasının baytlarını
     AYNEN taşıyor mu? Gömülü metin tek bir bitişik C dizgisine derleniyor,
     yani doğru ikilide dosyanın tamamı bir alt dizi olarak bulunur. Bayat bir
     ikiliyle `./build.sh suites` koşmak ESKİ stdlib'i sınamak demek; burası
     onu paketlerden ÖNCE söyler.

POZİTİF KONTROL (her koşumda): aramanın "yok" diyebildiği gösteriliyor —
ilk lib dosyasının sonuna benzersiz bir işaret eklenmiş hâli ikilide
BULUNMAMALI; mekanizma ayağında da gömülmeyen bir yol listede bulunmamalı.
İkisi de bulunursa kapı hiçbir şey ölçmüyordur ve kırmızı döner.

Kullanım: python3 tests/gomulu_stdlib_tazelik.py [ikili] [derleme_dizini]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def embedded_files():
    """EmbedLibraries.cmake'teki embed_library("ad" "dosya.tpr" ...) satırları."""
    path = os.path.join(ROOT, "cmake", "EmbedLibraries.cmake")
    with open(path, encoding="utf-8") as fh:
        src = fh.read()
    files = re.findall(r'^\s*embed_library\(\s*"[^"]+"\s+"([^"]+)"', src, re.M)
    if not files:
        print("gomulu stdlib denetimi: EmbedLibraries.cmake'te embed_library satiri "
              "okunamadi — kapi hicbir sey olcmezdi", file=sys.stderr)
        sys.exit(1)
    return files


def default_build_dir():
    for name in ("build-linux", "build-macos", "build"):
        d = os.path.join(ROOT, name)
        if os.path.isdir(d):
            return d
    return None


def reconfigure_deps_text(build_dir):
    """Derleme sisteminin yeniden yapılandırma bağımlılık listesi (ham metin)."""
    cands = [
        os.path.join(build_dir, "CMakeFiles", "Makefile.cmake"),  # Unix Makefiles
        os.path.join(build_dir, "build.ninja"),                     # Ninja
    ]
    for c in cands:
        if os.path.isfile(c):
            with open(c, encoding="utf-8", errors="replace") as fh:
                return c, fh.read()
    return None, None


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "tulpar")
    if not os.path.isfile(binary) and os.path.isfile(binary + ".exe"):
        binary += ".exe"
    build_dir = sys.argv[2] if len(sys.argv) > 2 else default_build_dir()
    files = embedded_files()
    failed = False

    # --- 1) MEKANİZMA -------------------------------------------------------
    dep_file, deps = (None, None)
    if build_dir:
        dep_file, deps = reconfigure_deps_text(build_dir)
    if deps is None:
        print("gomulu stdlib MEKANIZMA ayagi ATLANDI: derleme dizininde "
              "Makefile.cmake / build.ninja yok (%s)" % (build_dir or "dizin yok"))
    else:
        # Pozitif kontrol: gömülmeyen bir yol listede OLMAMALI.
        if "lib/__gomulu_olmayan__.tpr" in deps:
            print("gomulu stdlib denetimi: pozitif kontrol dustu — sahte yol "
                  "bagimlilik listesinde bulundu", file=sys.stderr)
            return 1
        missing = [f for f in files
                   if ("lib/" + f) not in deps.replace("\\", "/")]
        if missing:
            failed = True
            print("HATA: %s su gomulu dosyalari YENIDEN YAPILANDIRMA bagimliligi "
                  "olarak listelemiyor: %s" % (os.path.relpath(dep_file, ROOT),
                                              ", ".join(missing)))
            print("  `cmake --build` bu dosyalardaki degisikligi GORMEZ; ikili eski "
                  "stdlib'i tasir. cmake/EmbedLibraries.cmake'teki "
                  "CMAKE_CONFIGURE_DEPENDS satirina bak.")

    # --- 2) ETKİ --------------------------------------------------------------
    if not os.path.isfile(binary):
        print("gomulu stdlib denetimi: ikili yok: %s" % binary, file=sys.stderr)
        return 1
    with open(binary, "rb") as fh:
        blob = fh.read()
    contents = []
    for f in files:
        p = os.path.join(ROOT, "lib", f)
        with open(p, "rb") as fh:
            # CRLF -> LF: CMake'in okuma/yazma zinciri CR'yi düşürüyor (ölçüldü
            # 2026-09-27: router/http_utils/async/middleware/socket CRLF'li ve
            # ikilide LF'li duruyor). Bu bayatlık değil, gömme biçimi.
            contents.append((f, fh.read().replace(b"\r\n", b"\n")))
    # Pozitif kontrol: işaretli içerik BULUNMAMALI. Bulunursa arama her şeye
    # "var" diyordur ve aşağıdaki yeşil hiçbir şey kanıtlamaz.
    probe_name, probe = contents[0]
    marked = probe + b"\n// __gomulu_stdlib_tazelik_isareti__\n"
    if blob.find(marked) >= 0:
        print("gomulu stdlib denetimi: pozitif kontrol dustu — degistirilmis "
              "%s ikilide 'bulundu'" % probe_name, file=sys.stderr)
        return 1
    stale = [f for f, data in contents if blob.find(data) < 0]
    if stale:
        failed = True
        print("HATA: %s bu agacin lib/ dosyalarini TASIMIYOR (bayat gomulu "
              "stdlib): %s" % (os.path.relpath(binary, ROOT), ", ".join(stale)))
        print("  Bu ikiliyle kosulan testler ESKI kutuphaneyi sinar. Yeniden derle: "
              "./build.sh  (ya da cmake --build <dizin> && cp <dizin>/tulpar .)")

    if failed:
        return 1
    mech = "mekanizma: %s" % os.path.relpath(dep_file, ROOT) if deps is not None \
        else "mekanizma ATLANDI"
    print("gomulu stdlib taze — %d dosya ikilide aynen var (%s; pozitif kontrol "
          "gecti)" % (len(files), mech))
    return 0


if __name__ == "__main__":
    sys.exit(main())

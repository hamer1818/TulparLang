#!/usr/bin/env python3
"""CPU-GPU yerlesim denetimi + KONTROLU — bagimsiz kosulabilir kapi.

NE OLCUYOR
----------
`engine/rhi/shaders/*.{vert,frag,comp}` icindeki her `uniform` / `push_constant`
/ SSBO blogunun std140/std430 yerlesimi ile, o blogu CPU'dan dolduran C++
struct'inin bayt yerlesimi. Alan alan: ofset, boyut, hizalama, dizi adimi,
matris adimi.

NEDEN AYRI BIR KAPI GEREKIYOR
-----------------------------
Bu sinif hata SESSIZ. Derleyici, linker ve Vulkan dogrulama katmani bir ofset
kaymasini gormez — GPU baska bir ofsetten okur, goruntu "biraz yanlis" olur.
Depoda bu is bugune kadar ELLE yazilmis birkac `static_assert` ile tutuluyordu
ve cogu yalniz `sizeof`'a bakiyordu. `sizeof` kalibinin KOR NOKTASI olculdu
(asagidaki 2. kontrol): ayni boyutta, alan sirasi degismis bir struct o
assert'ten YESIL gecer.

KONTROL OLMADAN BU DENETIM BIR SEY SOYLEMEZ
-------------------------------------------
Bu yuzden kapi, gercek agaci olcmekle yetinmez; bilerek bozulmus IKI tanim
uretip denetimin KIRMIZI dondugunu de olcer:
  1. bir alani 4 bayt kaydiran tanim,
  2. AYNI BOYUTTA, alan sirasi degismis tanim (ve `static_assert(sizeof)`in
     o tanimda hala GECTIGI derleyiciye sorularak kanitlanir).
Kontrollerden biri kirmizi donmezse kapi "BOZUK" deyip 2 ile cikar — sessiz
yesil yok.

Kosum:  python3 tests/layout_audit.py
Cikis:  0 temiz | 1 uyusmazlik | 2 kapinin kendisi olcmuyor | 3 arac yok
"""
import os
import shutil
import sys

# WINDOWS KODLAMA SOZLESMESI (bkz. tests/silent_failure_probe.py): konsol
# cp1254, bu denetimin ciktisi UTF-8 ("—", "≠", "→"). Kodlama soylenmezse
# gercek bir UYUSMAZLIK satirini BASARKEN UnicodeEncodeError ile cokulur ve
# bulgu kaybolur.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "engine", "tools")
sys.path.insert(0, TOOLS)


def engine_tree_present():
    """`engine/` submodule'u klonlanmis mi (dizin var VE bos degil)."""
    d = os.path.join(ROOT, "engine")
    try:
        return os.path.isdir(d) and any(os.scandir(d))
    except OSError:
        return False


def main():
    # SUBMODULE SINIRINI GECMEYEN SEY: olculen iki taraftan biri
    # (`engine/rhi/shaders/*.{vert,frag}`) ve olcen arac
    # (`engine/tools/layout_check.py`) artik `engine/` submodule'unun icinde.
    # Baslatilmamis bir agacta ikisi de yoktur. Eski hal bu durumda tek satir
    # basip `return 0` diyordu: kapinin KENDI pozitif kontrolleri (rc1/rc2
    # KIRMIZI olmali) dahil hicbir sey kosmuyor, ama cikis kodu YESIL.
    # Betigin docstring'i zaten "3 = arac yok" diyor; bu dal onu kullanmiyordu.
    # Artik 3 donuyor ve cagiran (build.sh) bunu GORUNUR + SAYILAN atlama
    # olarak isliyor — yesile de kirmiziya da yazmadan.
    eksik = [p for p in ("engine/rhi/shaders", "engine/tools/layout_check.py")
             if not os.path.exists(os.path.join(ROOT, p))]
    if eksik and not engine_tree_present():
        print("yerlesim denetimi ATLANDI: %s yok — olculecek iki taraftan biri "
              "burada degil, demek ki HICBIR SEY olculmedi (pozitif kontroller "
              "dahil)" % ", ".join(eksik))
        print("  engine/ bir git submodule ve klonlanmamis gorunuyor. Cozum:")
        print("  git submodule update --init --recursive")
        return 3
    if eksik:
        # Agac YERINDE ama parcasi yok: bu bir yokluk degil, bir kirik.
        # (dist_archive_audit.py'deki motor SPEC dali ile ayni ayrim.)
        print("yerlesim denetimi KOSULAMADI: engine/ agaci YERINDE ama %s YOK "
              "— kapi kapsamini kaybetmis" % ", ".join(eksik))
        return 2
    if not (os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")):
        print("yerlesim denetimi ATLANDI: C++ derleyici (g++/clang++) yok")
        print("  UYARI: C++ struct yerlesimi DERLEYICIYE olcturuluyor; derleyici "
              "olmadan bu denetim hicbir sey olcmez")
        return 3

    import layout_check as lc

    print("=== CPU-GPU yerlesim denetimi (Faz 8.4) ===")
    rc, lines = lc.audit(verbose="--ayrinti" in sys.argv)
    print("\n".join(lines))

    print()
    print("--- Uretilmis SPIR-V tazeligi (bayat *_spv.h bu denetimi de yaniltir) ---")
    if lc.freshness():
        rc = 1

    cands = lc.cpp_candidates()
    print()
    print("--- POZITIF KONTROL 1: bir alan 4 bayt kaydirildi ---")
    rc1, l1 = lc.audit(overrides=lc.bozuk_kaydirma(cands))
    print("\n".join(x for x in l1 if "KIRMIZI" in x))
    print("  -> cikis %d (KIRMIZI bekleniyor)" % rc1)

    print()
    print("--- POZITIF KONTROL 2: ayni boyut, alan sirasi degisti ---")
    bozuk = lc.bozuk_sira(cands)
    print(lc.assert_kor_noktasi(cands, "MaterialUbo", bozuk))
    rc2, l2 = lc.audit(overrides=bozuk)
    print("\n".join(x for x in l2 if "KIRMIZI" in x))
    print("  -> cikis %d (KIRMIZI bekleniyor)" % rc2)

    if rc1 == 0 or rc2 == 0:
        print("\nyerlesim denetimi KAPISI BOZUK: pozitif kontrol kirmizi olmadi — "
              "bu denetim hicbir sey olcmuyor")
        return 2

    print("\npozitif kontroller: 2/2 KIRMIZI (denetim gercekten olcuyor)")
    print("yerlesim denetimi: " + ("UYUSMAZLIK VAR (yukariya bak)" if rc else "temiz"))
    return rc


if __name__ == "__main__":
    sys.exit(main())

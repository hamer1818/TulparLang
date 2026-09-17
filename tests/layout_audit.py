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

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.join(ROOT, "engine", "tools")
sys.path.insert(0, TOOLS)


def main():
    if not os.path.isdir(os.path.join(ROOT, "engine", "rhi", "shaders")):
        print("yerlesim denetimi ATLANDI: engine/rhi/shaders yok")
        return 0
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

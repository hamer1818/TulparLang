#!/usr/bin/env python3
"""Iki ikilinin is payini AYNI TURDA olcup oranin medyanini verir.

Neden bu dosya var (2026-09-11, PR #316 CI'i): `build.sh`'in ackermann
gerileme kapisi her kolu AYRI blokta olcuyordu — once zincirli uc kez, sonra
zincirsiz uc kez, her birinin en iyisi. Bir gurultu patlamasi YALNIZ BIR kola
denk geldiginde oran cope donuyor ve kapi, olctugu ozellik kusursuz
calisirken kirmizi veriyor.

Olculdu: ayni kodda, ayni gun
    yerel (10 deneme)  oran 0,873 - 0,925   (zincirli DAHA HIZLI, yayilim 0,05)
    CI Linux           oran 1,262           -> kapi dustu
~%40'lik bu kayma kucuk gurultuyle aciklanamaz; olcum TASARIMI kaymaya aciktir.

Bu betigin yaptigi:

  1. TUR ESLEMESI. Her turda dort olcum de alinir (on_hi, off_hi, on_lo,
     off_lo). Iki kol birbirine dakikalar degil MILISANIYELER uzaklikta
     olculur, yani bir yavaslama ikisini de ayni anda vurur ve ORANDA
     SADELESIR. Ayri bloklarda olcmek bu sadelesmeyi yok ediyordu.

  2. ACILIS CIKARILIR. Is payi = t(hi) - t(lo), AYNI ikiliden. Surec acilisi
     (fork+exec+dyld) Linux'ta ~0,2 ms, macOS arm64'te ~10 ms; ayni sabit her
     iki tarafa eklendiginde orani 1,0'a dogru ezer ve esigi platforma bagli
     yapar (bkz. Tuzaklar 6z).

  3. MEDYAN, ortalama DEGIL. Ortalama tek bir bozuk turu icine alir; medyan
     onu tanim geregi disarida birakir. Es sayida tur verilse bile alt
     medyan aliniyor (muhafazakar taraf: daha kucuk oran degil, ORTADAKI tur).

  4. DEJENERASYON BILDIRILIR. Is payi = t(hi) - t(lo) NEGATIFE dusebilir:
     eger is, surec acilisinin gurultusunun altinda kalirsa cikarma sifirin
     altina iner ve 1'e kelepcelenir. O an olcum HICBIR SEY olcmuyordur, ama
     kelepcelenmis 1 bir esikten rahatca gecer. Olculdu (2026-09-11, CI macOS
     arm64): fib zincir kapisi "is: 9835us -> 1us, acilis ~16329us" bastı ve
     `9835 > 1*2` ile YESIL verdi — zincirli kol hic olculmemisti.
     Bu betik kac turda kelepcelendigini SAYAR ve cagirana bildirir; karar
     cagiranin, ama artik "olctum" ile "olcemedim" ayirt edilebilir.

Cikti: tek satir, "oran_yuzde is_hi_us is_lo_us dejenere_tur" — medyan turun
degerleri + kelepcelenen tur sayisi. Cagiran esigi kendi koyar; bu betik
KARAR VERMEZ, yalnizca olcer.

Kullanim:
    python3 tests/perf_pair.py <A_ikili> <B_ikili> <hi_N> <lo_N> [tur]
    -> "oran(A/B yuzde) A_is_us B_is_us"

Ikililer `SR_N` ortam degiskeninden is miktarini okur.
"""
import os
import subprocess
import sys
import time

ENV_BASE = dict(os.environ, DISPLAY="", WAYLAND_DISPLAY="")


def run_us(binary, n):
    """Bir kosumun duvar saati suresi (mikrosaniye)."""
    t0 = time.perf_counter_ns()
    subprocess.run([binary], env=dict(ENV_BASE, SR_N=str(n)),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (time.perf_counter_ns() - t0) // 1000


def main():
    if len(sys.argv) < 5:
        print("kullanim: perf_pair.py <A> <B> <hi_N> <lo_N> [tur]",
              file=sys.stderr)
        return 2
    a_bin, b_bin = sys.argv[1], sys.argv[2]
    hi, lo = int(sys.argv[3]), int(sys.argv[4])
    turlar = int(sys.argv[5]) if len(sys.argv) > 5 else 5

    rounds = []
    dejenere = 0
    for _ in range(turlar):
        # Tur ici sira: is olculeri yan yana, acilis olculeri yan yana.
        a_hi, b_hi = run_us(a_bin, hi), run_us(b_bin, hi)
        a_lo, b_lo = run_us(a_bin, lo), run_us(b_bin, lo)
        a_raw, b_raw = a_hi - a_lo, b_hi - b_lo
        if a_raw < 1 or b_raw < 1:
            # Is, acilis gurultusunun altinda kaldi: bu tur OLCMEDI.
            dejenere += 1
        a_w, b_w = max(a_raw, 1), max(b_raw, 1)
        rounds.append((a_w / b_w, a_w, b_w))

    rounds.sort(key=lambda r: r[0])
    ratio, a_w, b_w = rounds[(len(rounds) - 1) // 2]
    print("%d %d %d %d" % (round(ratio * 100), a_w, b_w, dejenere))
    return 0


if __name__ == "__main__":
    sys.exit(main())

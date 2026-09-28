"""Tulpar kodu <-> C++ runtime arasında LTO (modüller arası satır içi alma):
kazanç var mı? (envanter K096; tulpar-engine PLAN L5 "whole-program optimization")

Tulpar modülleri zaten TEK bir LLVM modülüne derleniyor (import edilenler
dahil, O3 bütün programda) — yani Tulpar içi modüller arası satır içi alma
yapısal olarak var. Açık soru, Tulpar kodu ile `libtulpar_runtime.a`
(C++) ARASI: `aot_*` çağrıları opak arşive gidiyor. Bu betik onu ölçer.

Üç varyant, AYNI Tulpar IR'ı (`TULPAR_AOT_EMIT_LL`, O3 sonrası .ll):
  A = normal `tulpar build` (gcc runtime, obje link)       — bilgi için
  B = clang++ -O3 -march=native x.ll + clang runtime (LTO'suz)
  C = clang++ -O3 -march=native -flto=full x.ll + clang LTO bitcode runtime (lld)
B ile C'nin farkı YALNIZ LTO. Tur eşli: her turda üç ikili arka arkaya,
ortanca. A ile B farklı: B .ll'yi clang'ın O3'ünden BİR DAHA geçiriyor
(Tulpar'ın bilerek kapattığı döngü açmayı açıyor — intloop'taki ~%7).

ÖLÇÜM TUZAĞI (yakalandı): `-march=native` olmadan B jenerik x86-64 için,
C ise IR'daki `target-cpu` özniteliğiyle LTO'da yerel CPU için kod üretir;
matmul'da "LTO 1,77x hızlandırıyor" çıktı — tamamı -march farkıydı.

Sonuç (2026-09-28, Ryzen 7 9800X3D, LLVM 22, 5 tur): C/B = intloop 1,01 ·
fib 0,96 · sieve 1,06 · strcat 0,97 · arrayiter 0,83 (2 ms, gürültü) ·
mandelbrot 0,97 · matmul 1,03 · nbody 0,96. Yani runtime ile LTO bu
çekirdeklerde ÖLÇÜLEBİLİR bir kazanç vermiyor (±%6): sıcak yolların
runtime çağrıları zaten IR'da satır içi (Performance.md "Elek", "i32
eleman"), kalan maliyet başka yerde (kayan nokta dizilerinde kutulama).
Karar: LTO altyapısı (runtime'ı bitcode dağıtmak, lld zorunluluğu) şimdi
YAPILMAZ.

Kullanım: python3 benchmarks/lto_olcum.py <tulpar> <repo_kökü> <tur>
Önce iki clang runtime'ı derle (betiğin başındaki RT_DUZ / RT_LTO yolları):
  CC=clang CXX=clang++ cmake -S . -B <dizin> -G Ninja -DCMAKE_BUILD_TYPE=Release \
     [-DCMAKE_C_FLAGS=-flto=full -DCMAKE_CXX_FLAGS=-flto=full] \
     -DCMAKE_AR=$(which llvm-ar) -DCMAKE_RANLIB=$(which llvm-ranlib)
  cmake --build <dizin> --target tulpar_runtime"""
import os
import statistics
import subprocess
import sys
import time

tulpar, repo, rounds = sys.argv[1], sys.argv[2], int(sys.argv[3])
SP = os.environ.get('LTO_OLCUM_DIZIN', 'build-lto-olcum')
RT_DUZ = SP + '/b_duz/libtulpar_runtime.a'
RT_LTO = SP + '/b_lto/libtulpar_runtime.a'
LIBS = ['-rdynamic', '-no-pie', '-lm', '-lpthread', '-ldl', '-lssl', '-lcrypto']
BENCH = {'intloop': '50000000', 'fib': '32', 'sieve': '5000000', 'strcat': '2000000',
         'arrayiter': '5000000', 'mandelbrot': '2000', 'matmul': '640', 'nbody': '3000000'}
work = SP + '/w'
os.makedirs(work, exist_ok=True)


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        raise SystemExit('HATA %s\n%s' % (' '.join(cmd), (r.stdout + r.stderr)[-800:]))
    return r


def timeit(binp, n):
    env = dict(os.environ, BENCH_N=n)
    t0 = time.perf_counter()
    r = subprocess.run([binp], capture_output=True, text=True, env=env)
    return (time.perf_counter() - t0) * 1000, r.stdout.strip()


print('%-11s %10s %10s %10s  %8s  %s' % ('kiyas', 'A gcc', 'B clang', 'C clang+LTO', 'C/B', 'cikti'))
for b, n in BENCH.items():
    src = os.path.join(repo, 'benchmarks/fair/%s.tpr' % b)
    a = os.path.join(work, b + '_A')
    for f in (a, a + '.ll'):
        if os.path.exists(f):
            os.remove(f)
    run([tulpar, 'build', src, a], env=dict(os.environ, TULPAR_AOT_EMIT_LL='1', TULPAR_AOT_NOCACHE='1'))
    ll = a + '.ll'
    bb = os.path.join(work, b + '_B')
    cc = os.path.join(work, b + '_C')
    run(['clang++', '-O3', '-march=native', '-Wno-override-module', ll, RT_DUZ, '-o', bb] + LIBS)
    run(['clang++', '-O3', '-march=native', '-flto=full', '-fuse-ld=lld', '-Wno-override-module', ll, RT_LTO, '-o', cc] + LIBS)
    ta, tb, tc = [], [], []
    outs = set()
    for _ in range(rounds):
        for binp, acc in ((a, ta), (bb, tb), (cc, tc)):
            t, o = timeit(binp, n)
            acc.append(t)
            outs.add(o)
    ma, mb, mc = statistics.median(ta), statistics.median(tb), statistics.median(tc)
    print('%-11s %8.1fms %8.1fms %9.1fms  %7.2fx  %s' % (
        b, ma, mb, mc, mc / mb, (list(outs)[0] if len(outs) == 1 else 'AYRISIYOR %s' % outs)[:24]))

import os, sys
sys.setrecursionlimit(10000)
def qs(a, lo, hi):
    i = lo; j = hi; p = a[(lo + hi) // 2]
    while i <= j:
        while a[i] < p: i += 1
        while a[j] > p: j -= 1
        if i <= j:
            a[i], a[j] = a[j], a[i]; i += 1; j -= 1
    if lo < j: qs(a, lo, j)
    if i < hi: qs(a, i, hi)
n = int(os.environ.get("BENCH_N", "0") or 0)
if n <= 0: n = 1000000
a = [0] * n
seed = 42
for i in range(n):
    seed = (seed * 48271) % 2147483647
    a[i] = seed % 1000000
qs(a, 0, n - 1)
bad = sum(1 for i in range(1, n) if a[i - 1] > a[i])
cs = 0
for i in range(n): cs = (cs + a[i] * (i % 1000)) % 1000000007
print(a[0], a[n // 2], a[n - 1], cs, bad)

import os
def f(x): return (x * 31 + 7) % 1000003
def g(x): return (x * 17 + 3) % 1000003
n = int(os.environ.get("BENCH_N", "0") or 0)
if n <= 0: n = 20000000
tab = [f, g]
acc = 1
for _ in range(n): acc = tab[acc & 1](acc)
print(acc)

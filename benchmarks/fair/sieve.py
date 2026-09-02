import os
n = int(os.environ.get("BENCH_N") or 0) or 5000000
f = [0] * (n + 1)
c = 0
for i in range(2, n + 1):
    if f[i] == 0:
        c += 1
        for k in range(i * i, n + 1, i):
            f[k] = 1
print(c)

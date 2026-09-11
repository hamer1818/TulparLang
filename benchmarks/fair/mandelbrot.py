import os
n = int(os.environ.get("BENCH_N") or 2000)
if n <= 0:
    n = 2000
maxit = 50
total = 0
for y in range(n):
    ci = 2.0 * y / n - 1.0
    for x in range(n):
        cr = 2.0 * x / n - 1.5
        zr = 0.0
        zi = 0.0
        i = 0
        while i < maxit and zr * zr + zi * zi <= 4.0:
            t = zr * zr - zi * zi + cr
            zi = 2.0 * zr * zi + ci
            zr = t
            i += 1
        total += i
print(total)

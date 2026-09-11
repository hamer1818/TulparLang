# N x N kayan nokta matris carpimi. Saf Python listeleri — Python'un float'i
# KUTULU bir nesne; bu satir "kutulu kayan nokta" referansi.
import os
n = int(os.environ.get("BENCH_N") or 256)
if n <= 0:
    n = 256
a = [0.0] * (n * n)
b = [0.0] * (n * n)
c = [0.0] * (n * n)
for i in range(n):
    for j in range(n):
        a[i * n + j] = float((i + j) % 10)
        b[i * n + j] = float((i * j) % 7)
for i in range(n):
    for k in range(n):
        av = a[i * n + k]
        base = k * n
        row = i * n
        for j in range(n):
            c[row + j] += av * b[base + j]
t = 0.0
for i in range(n * n):
    t += c[i]
print(int(t))

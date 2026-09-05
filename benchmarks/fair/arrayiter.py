import os
n = int(os.environ.get("BENCH_N") or 0) or 5000000
a = [0] * n
t = 0
for i in range(len(a)):
    a[i] = i
for i in range(len(a)):
    t += a[i]
print(t)

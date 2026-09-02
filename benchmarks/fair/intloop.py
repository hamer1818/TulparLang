import os
n = int(os.environ.get("BENCH_N") or 0) or 50000000
t = 1
for i in range(n):
    t = (t * 31 + i) % 1000000007
print(t)

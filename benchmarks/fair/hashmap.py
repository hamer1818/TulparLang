import os
n = int(os.environ.get("BENCH_N", "0") or 0)
if n <= 0: n = 1000000
m = {}
for i in range(n): m["k" + str(i)] = i
s = 0
for i in range(n): s += m["k" + str((i * 7) % n)]
print(len(m), s)

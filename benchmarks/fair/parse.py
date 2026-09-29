import os
n = int(os.environ.get("BENCH_N", "0") or 0)
if n <= 0: n = 1000000
s = ",".join(str((i * 7919) % 100000) for i in range(n))
parts = s.split(",")
total = 0
for p in parts: total += int(p)
print(len(parts), total)

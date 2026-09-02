import os
n = int(os.environ.get("BENCH_N") or 0) or 2000000
parts = []
for i in range(n):
    parts.append(str(i % 1000))
    parts.append(",")
s = "".join(parts)
print(str(len(s)) + " " + str(s.count(",")))

import os
class P:
    __slots__ = ("x", "y", "vx", "vy")
    def __init__(self, x, y, vx, vy):
        self.x = x; self.y = y; self.vx = vx; self.vy = vy
n = int(os.environ.get("BENCH_N", "0") or 0)
if n <= 0: n = 200000
ps = [P(float(i % 1000) * 0.5, float(i % 777) * 0.25, float((i % 13) - 6) * 0.1, float((i % 7) - 3) * 0.2) for i in range(n)]
dt = 0.01; g = 9.81; w = 500.0
for _ in range(50):
    for p in ps:
        p.vy = p.vy - g * dt
        p.x = p.x + p.vx * dt
        p.y = p.y + p.vy * dt
        if p.x < 0.0 or p.x > w: p.vx = 0.0 - p.vx
        if p.y < 0.0:
            p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9
s = 0.0
for p in ps: s = s + p.x + p.y
print(int(round(s * 1000.0)))

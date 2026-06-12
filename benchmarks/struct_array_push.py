n = 1_000_000
points = []
for i in range(n):
    points.append((i, i * 2, i * 3))
total = 0
for x, y, z in points:
    total += x + y + z
print(total)

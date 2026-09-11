# N-body. Saf Python listeleri: Python'un float'i KUTULU bir nesne.
import math, os
NB = 5
PI = 3.141592653589793
SOLAR_MASS = 4 * PI * PI
DPY = 365.24
x = [0.0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01]
y = [0.0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01]
z = [0.0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01]
vx = [0.0, 1.66007664274403694e-03*DPY, -2.76742510726862411e-03*DPY, 2.96460137564761618e-03*DPY, 2.68067772490389322e-03*DPY]
vy = [0.0, 7.69901118419740425e-03*DPY, 4.99852801234917238e-03*DPY, 2.37847173959480950e-03*DPY, 1.62824170038242295e-03*DPY]
vz = [0.0, -6.90460016972063023e-05*DPY, 2.30417297573763929e-05*DPY, -2.96589568540237556e-05*DPY, -9.51592254519715870e-05*DPY]
mass = [SOLAR_MASS, 9.54791938424326609e-04*SOLAR_MASS, 2.85885980666130812e-04*SOLAR_MASS, 4.36624404335156298e-05*SOLAR_MASS, 5.15138902046611451e-05*SOLAR_MASS]


def advance(dt):
    for i in range(NB):
        for j in range(i + 1, NB):
            dx = x[i] - x[j]
            dy = y[i] - y[j]
            dz = z[i] - z[j]
            d2 = dx * dx + dy * dy + dz * dz
            mag = dt / (d2 * math.sqrt(d2))
            vx[i] -= dx * mass[j] * mag
            vy[i] -= dy * mass[j] * mag
            vz[i] -= dz * mass[j] * mag
            vx[j] += dx * mass[i] * mag
            vy[j] += dy * mass[i] * mag
            vz[j] += dz * mass[i] * mag
    for i in range(NB):
        x[i] += dt * vx[i]
        y[i] += dt * vy[i]
        z[i] += dt * vz[i]


def energy():
    e = 0.0
    for i in range(NB):
        e += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i])
        for j in range(i + 1, NB):
            dx = x[i] - x[j]
            dy = y[i] - y[j]
            dz = z[i] - z[j]
            e -= (mass[i] * mass[j]) / math.sqrt(dx * dx + dy * dy + dz * dz)
    return e


n = int(os.environ.get("BENCH_N") or 1000000)
if n <= 0:
    n = 1000000
px = py = pz = 0.0
for i in range(NB):
    px += vx[i] * mass[i]
    py += vy[i] * mass[i]
    pz += vz[i] * mass[i]
vx[0] = -px / SOLAR_MASS
vy[0] = -py / SOLAR_MASS
vz[0] = -pz / SOLAR_MASS
for _ in range(n):
    advance(0.01)
print(int(round(energy() * 1e9)))

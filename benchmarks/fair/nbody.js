const NB = 5, PI = 3.141592653589793, SOLAR_MASS = 4 * PI * PI, DPY = 365.24;
const x = Float64Array.from([0.0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01]);
const y = Float64Array.from([0.0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01]);
const z = Float64Array.from([0.0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01]);
const vx = Float64Array.from([0.0, 1.66007664274403694e-03*DPY, -2.76742510726862411e-03*DPY, 2.96460137564761618e-03*DPY, 2.68067772490389322e-03*DPY]);
const vy = Float64Array.from([0.0, 7.69901118419740425e-03*DPY, 4.99852801234917238e-03*DPY, 2.37847173959480950e-03*DPY, 1.62824170038242295e-03*DPY]);
const vz = Float64Array.from([0.0, -6.90460016972063023e-05*DPY, 2.30417297573763929e-05*DPY, -2.96589568540237556e-05*DPY, -9.51592254519715870e-05*DPY]);
const mass = Float64Array.from([SOLAR_MASS, 9.54791938424326609e-04*SOLAR_MASS, 2.85885980666130812e-04*SOLAR_MASS, 4.36624404335156298e-05*SOLAR_MASS, 5.15138902046611451e-05*SOLAR_MASS]);

function advance(dt) {
  for (let i = 0; i < NB; i++)
    for (let j = i + 1; j < NB; j++) {
      const dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
      const d2 = dx * dx + dy * dy + dz * dz;
      const mag = dt / (d2 * Math.sqrt(d2));
      vx[i] -= dx * mass[j] * mag; vy[i] -= dy * mass[j] * mag; vz[i] -= dz * mass[j] * mag;
      vx[j] += dx * mass[i] * mag; vy[j] += dy * mass[i] * mag; vz[j] += dz * mass[i] * mag;
    }
  for (let i = 0; i < NB; i++) { x[i] += dt * vx[i]; y[i] += dt * vy[i]; z[i] += dt * vz[i]; }
}

function energy() {
  let e = 0.0;
  for (let i = 0; i < NB; i++) {
    e += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
    for (let j = i + 1; j < NB; j++) {
      const dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
      e -= (mass[i] * mass[j]) / Math.sqrt(dx * dx + dy * dy + dz * dz);
    }
  }
  return e;
}

const n = Math.max(1, parseInt(process.env.BENCH_N || "1000000", 10) || 1000000);
let px = 0, py = 0, pz = 0;
for (let i = 0; i < NB; i++) { px += vx[i] * mass[i]; py += vy[i] * mass[i]; pz += vz[i] * mass[i]; }
vx[0] = -px / SOLAR_MASS; vy[0] = -py / SOLAR_MASS; vz[0] = -pz / SOLAR_MASS;
for (let i = 0; i < n; i++) advance(0.01);
console.log(String(Math.round(energy() * 1e9)));

// N-body (nbody.c ile ayni algoritma ve veri yapisi).
const NB: usize = 5;
const PI: f64 = 3.141592653589793;
const SOLAR_MASS: f64 = 4.0 * PI * PI;
const DPY: f64 = 365.24;

struct W { x: [f64; NB], y: [f64; NB], z: [f64; NB],
           vx: [f64; NB], vy: [f64; NB], vz: [f64; NB], mass: [f64; NB] }

fn advance(w: &mut W, dt: f64) {
    for i in 0..NB {
        for j in i + 1..NB {
            let dx = w.x[i] - w.x[j];
            let dy = w.y[i] - w.y[j];
            let dz = w.z[i] - w.z[j];
            let d2 = dx * dx + dy * dy + dz * dz;
            let mag = dt / (d2 * d2.sqrt());
            w.vx[i] -= dx * w.mass[j] * mag;
            w.vy[i] -= dy * w.mass[j] * mag;
            w.vz[i] -= dz * w.mass[j] * mag;
            w.vx[j] += dx * w.mass[i] * mag;
            w.vy[j] += dy * w.mass[i] * mag;
            w.vz[j] += dz * w.mass[i] * mag;
        }
    }
    for i in 0..NB {
        w.x[i] += dt * w.vx[i];
        w.y[i] += dt * w.vy[i];
        w.z[i] += dt * w.vz[i];
    }
}

fn energy(w: &W) -> f64 {
    let mut e = 0.0;
    for i in 0..NB {
        e += 0.5 * w.mass[i] * (w.vx[i] * w.vx[i] + w.vy[i] * w.vy[i] + w.vz[i] * w.vz[i]);
        for j in i + 1..NB {
            let dx = w.x[i] - w.x[j];
            let dy = w.y[i] - w.y[j];
            let dz = w.z[i] - w.z[j];
            e -= (w.mass[i] * w.mass[j]) / (dx * dx + dy * dy + dz * dz).sqrt();
        }
    }
    e
}

fn main() {
    let n: i64 = std::env::var("BENCH_N").ok()
        .and_then(|v| v.parse().ok()).filter(|&x: &i64| x > 0).unwrap_or(1000000);
    let mut w = W {
        x: [0.0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01],
        y: [0.0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01],
        z: [0.0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01],
        vx: [0.0, 1.66007664274403694e-03 * DPY, -2.76742510726862411e-03 * DPY, 2.96460137564761618e-03 * DPY, 2.68067772490389322e-03 * DPY],
        vy: [0.0, 7.69901118419740425e-03 * DPY, 4.99852801234917238e-03 * DPY, 2.37847173959480950e-03 * DPY, 1.62824170038242295e-03 * DPY],
        vz: [0.0, -6.90460016972063023e-05 * DPY, 2.30417297573763929e-05 * DPY, -2.96589568540237556e-05 * DPY, -9.51592254519715870e-05 * DPY],
        mass: [SOLAR_MASS, 9.54791938424326609e-04 * SOLAR_MASS, 2.85885980666130812e-04 * SOLAR_MASS,
               4.36624404335156298e-05 * SOLAR_MASS, 5.15138902046611451e-05 * SOLAR_MASS],
    };
    let (mut px, mut py, mut pz) = (0.0, 0.0, 0.0);
    for i in 0..NB { px += w.vx[i] * w.mass[i]; py += w.vy[i] * w.mass[i]; pz += w.vz[i] * w.mass[i]; }
    w.vx[0] = -px / SOLAR_MASS; w.vy[0] = -py / SOLAR_MASS; w.vz[0] = -pz / SOLAR_MASS;
    for _ in 0..n { advance(&mut w, 0.01); }
    println!("{}", (energy(&w) * 1e9).round() as i64);
}

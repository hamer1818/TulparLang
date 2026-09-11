using System;
class nbody {
    const int NB = 5;
    const double PI = 3.141592653589793;
    const double SOLAR_MASS = 4 * PI * PI;
    const double DPY = 365.24;
    static double[] x = {0.0, 4.84143144246472090e+00, 8.34336671824457987e+00, 1.28943695621391310e+01, 1.53796971148509165e+01};
    static double[] y = {0.0, -1.16032004402742839e+00, 4.12479856412430479e+00, -1.51111514016986312e+01, -2.59193146099879641e+01};
    static double[] z = {0.0, -1.03622044471123109e-01, -4.03523417114321381e-01, -2.23307578892655734e-01, 1.79258772950371181e-01};
    static double[] vx = {0.0, 1.66007664274403694e-03*DPY, -2.76742510726862411e-03*DPY, 2.96460137564761618e-03*DPY, 2.68067772490389322e-03*DPY};
    static double[] vy = {0.0, 7.69901118419740425e-03*DPY, 4.99852801234917238e-03*DPY, 2.37847173959480950e-03*DPY, 1.62824170038242295e-03*DPY};
    static double[] vz = {0.0, -6.90460016972063023e-05*DPY, 2.30417297573763929e-05*DPY, -2.96589568540237556e-05*DPY, -9.51592254519715870e-05*DPY};
    static double[] mass = {SOLAR_MASS, 9.54791938424326609e-04*SOLAR_MASS, 2.85885980666130812e-04*SOLAR_MASS, 4.36624404335156298e-05*SOLAR_MASS, 5.15138902046611451e-05*SOLAR_MASS};

    static void Advance(double dt) {
        for (int i = 0; i < NB; i++)
            for (int j = i + 1; j < NB; j++) {
                double dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
                double d2 = dx * dx + dy * dy + dz * dz;
                double mag = dt / (d2 * Math.Sqrt(d2));
                vx[i] -= dx * mass[j] * mag; vy[i] -= dy * mass[j] * mag; vz[i] -= dz * mass[j] * mag;
                vx[j] += dx * mass[i] * mag; vy[j] += dy * mass[i] * mag; vz[j] += dz * mass[i] * mag;
            }
        for (int i = 0; i < NB; i++) { x[i] += dt * vx[i]; y[i] += dt * vy[i]; z[i] += dt * vz[i]; }
    }

    static double Energy() {
        double e = 0.0;
        for (int i = 0; i < NB; i++) {
            e += 0.5 * mass[i] * (vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
            for (int j = i + 1; j < NB; j++) {
                double dx = x[i] - x[j], dy = y[i] - y[j], dz = z[i] - z[j];
                e -= (mass[i] * mass[j]) / Math.Sqrt(dx * dx + dy * dy + dz * dz);
            }
        }
        return e;
    }

    static void Main() {
        int n = 3000000;
        var s = Environment.GetEnvironmentVariable("BENCH_N");
        if (s != null && int.TryParse(s, out int v) && v > 0) n = v;
        double px = 0, py = 0, pz = 0;
        for (int i = 0; i < NB; i++) { px += vx[i] * mass[i]; py += vy[i] * mass[i]; pz += vz[i] * mass[i]; }
        vx[0] = -px / SOLAR_MASS; vy[0] = -py / SOLAR_MASS; vz[0] = -pz / SOLAR_MASS;
        for (int i = 0; i < n; i++) Advance(0.01);
        Console.WriteLine((long)Math.Round(Energy() * 1e9));
    }
}

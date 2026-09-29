using System;
struct P { public double x, y, vx, vy; }
class particles {
    static void Main() {
        int n = 200000; var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out var v) && v > 0) n = v;
        var ps = new P[n];
        for (int i = 0; i < n; i++) { ps[i].x = (double)(i % 1000) * 0.5; ps[i].y = (double)(i % 777) * 0.25; ps[i].vx = (double)((i % 13) - 6) * 0.1; ps[i].vy = (double)((i % 7) - 3) * 0.2; }
        double dt = 0.01, g = 9.81, w = 500.0;
        for (int s = 0; s < 50; s++)
            for (int i = 0; i < n; i++) {
                ref P p = ref ps[i];
                p.vy = p.vy - g * dt;
                p.x = p.x + p.vx * dt;
                p.y = p.y + p.vy * dt;
                if (p.x < 0.0 || p.x > w) p.vx = 0.0 - p.vx;
                if (p.y < 0.0) { p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9; }
            }
        double sum = 0.0;
        for (int i = 0; i < n; i++) sum = sum + ps[i].x + ps[i].y;
        Console.WriteLine((long)Math.Round(sum * 1000.0));
    }
}

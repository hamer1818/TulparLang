using System;
class mandelbrot {
    static void Main() {
        int n = 2000;
        var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out int v) && v > 0) n = v;
        const int maxit = 50;
        long total = 0;
        for (int y = 0; y < n; y++) {
            double ci = 2.0 * y / n - 1.0;
            for (int x = 0; x < n; x++) {
                double cr = 2.0 * x / n - 1.5;
                double zr = 0.0, zi = 0.0;
                int i = 0;
                while (i < maxit && zr * zr + zi * zi <= 4.0) {
                    double t = zr * zr - zi * zi + cr;
                    zi = 2.0 * zr * zi + ci;
                    zr = t;
                    i++;
                }
                total += i;
            }
        }
        Console.WriteLine(total);
    }
}

using System;
class matmul {
    static void Main() {
        int n = 640;
        var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out int v) && v > 0) n = v;
        var a = new double[n * n];
        var b = new double[n * n];
        var c = new double[n * n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                a[i * n + j] = (i + j) % 10;
                b[i * n + j] = (i * j) % 7;
            }
        for (int i = 0; i < n; i++)
            for (int k = 0; k < n; k++) {
                double av = a[i * n + k];
                for (int j = 0; j < n; j++) c[i * n + j] += av * b[k * n + j];
            }
        double t = 0.0;
        for (int i = 0; i < n * n; i++) t += c[i];
        Console.WriteLine((long)t);
    }
}

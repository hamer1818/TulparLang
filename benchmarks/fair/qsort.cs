using System;
class qsort {
    static void Qs(int[] a, int lo, int hi) {
        int i = lo, j = hi, p = a[(lo + hi) / 2];
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
        }
        if (lo < j) Qs(a, lo, j);
        if (i < hi) Qs(a, i, hi);
    }
    static void Main() {
        int n = 1000000; var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out var v) && v > 0) n = v;
        var a = new int[n]; long seed = 42;
        for (int i = 0; i < n; i++) { seed = (seed * 48271) % 2147483647; a[i] = (int)(seed % 1000000); }
        Qs(a, 0, n - 1);
        long bad = 0; for (int i = 1; i < n; i++) if (a[i - 1] > a[i]) bad++;
        long cs = 0; for (int i = 0; i < n; i++) cs = (cs + (long)a[i] * (i % 1000)) % 1000000007;
        Console.WriteLine(a[0] + " " + a[n / 2] + " " + a[n - 1] + " " + cs + " " + bad);
    }
}

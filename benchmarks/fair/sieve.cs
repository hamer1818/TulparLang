using System;
class sieve {
    static void Main() {
        long n = 5000000;
        string e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && long.TryParse(e, out long v) && v > 0) n = v;
        int[] f = new int[n + 1];          // C'nin calloc'u / Rust'un vec![0i32] esi
        long c = 0;
        for (long i = 2; i <= n; i++) {
            if (f[i] == 0) { c++; for (long k = i * i; k <= n; k += i) f[k] = 1; }
        }
        Console.WriteLine(c);
    }
}

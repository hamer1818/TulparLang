using System;
class fib {
    static long F(long n) { return n <= 1 ? n : F(n - 1) + F(n - 2); }
    static void Main() {
        long n = 32;
        string e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && long.TryParse(e, out long v) && v > 0) n = v;
        Console.WriteLine(F(n));
    }
}

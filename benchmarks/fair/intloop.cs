using System;
class intloop {
    static void Main() {
        long n = 50000000;
        string e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && long.TryParse(e, out long v) && v > 0) n = v;
        long t = 1;
        for (long i = 0; i < n; i++) t = (t * 31 + i) % 1000000007L;
        Console.WriteLine(t);
    }
}

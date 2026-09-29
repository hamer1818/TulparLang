using System;
class callfn {
    static long F(long x) => (x * 31 + 7) % 1000003;
    static long G(long x) => (x * 17 + 3) % 1000003;
    static void Main() {
        int n = 20000000; var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out var v) && v > 0) n = v;
        Func<long, long>[] tab = { F, G };
        long acc = 1;
        for (int i = 0; i < n; i++) acc = tab[acc & 1](acc);
        Console.WriteLine(acc);
    }
}

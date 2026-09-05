using System;
// Dizi uzerinde deyimsel yineleme: doldur + topla. C#'ta uzunluk KAPTA
// (a.Length) — C'deki gibi elle tasinan `n` degil.
class arrayiter {
    static void Main() {
        long n = 5000000;
        string e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && long.TryParse(e, out long v) && v > 0) n = v;
        long[] a = new long[n];
        long t = 0;
        for (int i = 0; i < a.Length; i++) a[i] = i;
        for (int i = 0; i < a.Length; i++) t += a[i];
        Console.WriteLine(t);
    }
}

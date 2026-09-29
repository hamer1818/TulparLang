using System;
using System.Text;
class parse {
    static void Main() {
        int n = 1000000; var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out var v) && v > 0) n = v;
        var b = new StringBuilder();
        for (long i = 0; i < n; i++) { if (i > 0) b.Append(','); b.Append((i * 7919) % 100000); }
        var parts = b.ToString().Split(',');
        long sum = 0;
        foreach (var p in parts) sum += long.Parse(p);
        Console.WriteLine(parts.Length + " " + sum);
    }
}

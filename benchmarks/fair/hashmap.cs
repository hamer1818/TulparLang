using System;
using System.Collections.Generic;
class hashmap {
    static void Main() {
        int n = 1000000; var e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && int.TryParse(e, out var v) && v > 0) n = v;
        var m = new Dictionary<string, long>();
        for (int i = 0; i < n; i++) m["k" + i] = i;
        long s = 0;
        for (int i = 0; i < n; i++) s += m["k" + ((long)i * 7 % n)];
        Console.WriteLine(m.Count + " " + s);
    }
}

using System;
using System.Text;
// Deyimsel C# dizgi kurma: StringBuilder (Java StringBuilder, Go
// strings.Builder, C++ std::string += ile ayni rol).
class strcat {
    static void Main() {
        long n = 2000000;
        string e = Environment.GetEnvironmentVariable("BENCH_N");
        if (e != null && long.TryParse(e, out long v) && v > 0) n = v;
        StringBuilder b = new StringBuilder();
        for (long i = 0; i < n; i++) { b.Append(i % 1000); b.Append(','); }
        string s = b.ToString();
        long cnt = 0;
        for (int i = 0; i < s.Length; i++) if (s[i] == ',') cnt++;
        Console.WriteLine(s.Length + " " + cnt);
    }
}

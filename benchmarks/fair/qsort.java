public class qsort {
  static void qs(int[] a, int lo, int hi) {
    int i = lo, j = hi, p = a[(lo + hi) / 2];
    while (i <= j) {
      while (a[i] < p) i++;
      while (a[j] > p) j--;
      if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (lo < j) qs(a, lo, j);
    if (i < hi) qs(a, i, hi);
  }
  public static void main(String[] x) {
    int n = 1000000; String e = System.getenv("BENCH_N");
    if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception z) {} }
    int[] a = new int[n]; long seed = 42;
    for (int i = 0; i < n; i++) { seed = (seed * 48271) % 2147483647; a[i] = (int) (seed % 1000000); }
    qs(a, 0, n - 1);
    long bad = 0; for (int i = 1; i < n; i++) if (a[i - 1] > a[i]) bad++;
    long cs = 0; for (int i = 0; i < n; i++) cs = (cs + (long) a[i] * (i % 1000)) % 1000000007;
    System.out.println(a[0] + " " + a[n / 2] + " " + a[n - 1] + " " + cs + " " + bad);
  }
}

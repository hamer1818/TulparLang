public class parse {
  public static void main(String[] a) {
    int n = 1000000; String e = System.getenv("BENCH_N");
    if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception z) {} }
    StringBuilder b = new StringBuilder();
    for (long i = 0; i < n; i++) { if (i > 0) b.append(','); b.append((i * 7919) % 100000); }
    String[] parts = b.toString().split(",");
    long sum = 0;
    for (String p : parts) sum += Long.parseLong(p);
    System.out.println(parts.length + " " + sum);
  }
}

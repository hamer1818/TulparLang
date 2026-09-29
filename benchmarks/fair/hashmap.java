import java.util.HashMap;
public class hashmap {
  public static void main(String[] a) {
    int n = 1000000; String e = System.getenv("BENCH_N");
    if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception x) {} }
    HashMap<String, Long> m = new HashMap<>();
    for (int i = 0; i < n; i++) m.put("k" + i, (long) i);
    long s = 0;
    for (int i = 0; i < n; i++) s += m.get("k" + ((long) i * 7 % n));
    System.out.println(m.size() + " " + s);
  }
}

import java.util.function.LongUnaryOperator;
public class callfn {
  static long f(long x) { return (x * 31 + 7) % 1000003; }
  static long g(long x) { return (x * 17 + 3) % 1000003; }
  public static void main(String[] a) {
    int n = 20000000; String e = System.getenv("BENCH_N");
    if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception z) {} }
    LongUnaryOperator[] tab = { callfn::f, callfn::g };
    long acc = 1;
    for (int i = 0; i < n; i++) acc = tab[(int) (acc & 1)].applyAsLong(acc);
    System.out.println(acc);
  }
}

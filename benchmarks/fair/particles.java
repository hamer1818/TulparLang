public class particles {
  static final class P { double x, y, vx, vy; }
  public static void main(String[] a) {
    int n = 200000; String e = System.getenv("BENCH_N");
    if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception z) {} }
    P[] ps = new P[n];
    for (int i = 0; i < n; i++) { P p = new P(); p.x = (double) (i % 1000) * 0.5; p.y = (double) (i % 777) * 0.25; p.vx = (double) ((i % 13) - 6) * 0.1; p.vy = (double) ((i % 7) - 3) * 0.2; ps[i] = p; }
    double dt = 0.01, g = 9.81, w = 500.0;
    for (int s = 0; s < 50; s++)
      for (int i = 0; i < n; i++) {
        P p = ps[i];
        p.vy = p.vy - g * dt;
        p.x = p.x + p.vx * dt;
        p.y = p.y + p.vy * dt;
        if (p.x < 0.0 || p.x > w) p.vx = 0.0 - p.vx;
        if (p.y < 0.0) { p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9; }
      }
    double sum = 0.0;
    for (int i = 0; i < n; i++) sum = sum + ps[i].x + ps[i].y;
    System.out.println((long) Math.round(sum * 1000.0));
  }
}

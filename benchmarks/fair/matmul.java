public class matmul {
    public static void main(String[] args) {
        int n = 256;
        String e = System.getenv("BENCH_N");
        if (e != null) { try { int v = Integer.parseInt(e); if (v > 0) n = v; } catch (Exception ex) {} }
        double[] a = new double[n * n];
        double[] b = new double[n * n];
        double[] c = new double[n * n];
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++) {
                a[i * n + j] = (i + j) % 10;
                b[i * n + j] = (i * j) % 7;
            }
        for (int i = 0; i < n; i++)
            for (int k = 0; k < n; k++) {
                double av = a[i * n + k];
                for (int j = 0; j < n; j++) c[i * n + j] += av * b[k * n + j];
            }
        double t = 0.0;
        for (int i = 0; i < n * n; i++) t += c[i];
        System.out.println((long) t);
    }
}

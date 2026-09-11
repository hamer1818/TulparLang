// N x N kayan nokta matris carpimi. Degerler KUCUK TAM SAYI (double icinde
// birebir temsil edilir) — boylece gcc'nin FMA'ya kaynastirmasi sonucu
// DEGISTIRMIYOR ve diller arasi cikti mutabakati kayan nokta kaymasina
// takilmiyor. Olculen sey depolama + bant genisligi + carpma/toplama.
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(void) {
    const char *e = getenv("BENCH_N");
    int n = e ? atoi(e) : 256;
    if (n <= 0) n = 256;
    std::vector<double> a(n*n), b(n*n), c(n*n, 0.0);
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            a[i * n + j] = (double)((i + j) % 10);
            b[i * n + j] = (double)((i * j) % 7);
            c[i * n + j] = 0.0;
        }
    for (int i = 0; i < n; i++)
        for (int k = 0; k < n; k++) {
            double av = a[i * n + k];
            for (int j = 0; j < n; j++) c[i * n + j] += av * b[k * n + j];
        }
    double t = 0.0;
    for (int i = 0; i < n * n; i++) t += c[i];
    printf("%lld\n", (long long)t);
    return 0;
}

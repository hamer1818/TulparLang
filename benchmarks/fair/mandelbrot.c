// Mandelbrot yineleme sayimi. DIZI YOK — olculen sey saf kayan nokta
// ARITMETIGI (yerel degiskenler, carpma/toplama/karsilastirma). matmul'un
// tamamlayicisi: orada depolama, burada deger temsili olculuyor.
// Cikti TAM SAYI (toplam yineleme) — kayan nokta bicimlendirmesi devrede degil.
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    const char *e = getenv("BENCH_N");
    int n = e ? atoi(e) : 2000;
    if (n <= 0) n = 2000;
    const int maxit = 50;
    long long total = 0;
    for (int y = 0; y < n; y++) {
        double ci = 2.0 * (double)y / (double)n - 1.0;
        for (int x = 0; x < n; x++) {
            double cr = 2.0 * (double)x / (double)n - 1.5;
            double zr = 0.0, zi = 0.0;
            int i = 0;
            while (i < maxit && zr * zr + zi * zi <= 4.0) {
                double t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                i++;
            }
            total += i;
        }
    }
    printf("%lld\n", total);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
static void qs(int *a, long lo, long hi) {
    long i = lo, j = hi; int p = a[(lo + hi) / 2];
    while (i <= j) {
        while (a[i] < p) i++;
        while (a[j] > p) j--;
        if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    if (lo < j) qs(a, lo, j);
    if (i < hi) qs(a, i, hi);
}
int main(void) {
    const char *e = getenv("BENCH_N"); long n = e ? atol(e) : 0; if (n <= 0) n = 1000000;
    int *a = malloc(n * sizeof(int)); long seed = 42;
    for (long i = 0; i < n; i++) { seed = (seed * 48271) % 2147483647; a[i] = (int)(seed % 1000000); }
    qs(a, 0, n - 1);
    long bad = 0; for (long i = 1; i < n; i++) if (a[i - 1] > a[i]) bad++;
    long cs = 0; for (long i = 0; i < n; i++) cs = (cs + (long)a[i] * (i % 1000)) % 1000000007;
    printf("%d %d %d %ld %ld\n", a[0], a[n / 2], a[n - 1], cs, bad);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
static long f(long x) { return (x * 31 + 7) % 1000003; }
static long g(long x) { return (x * 17 + 3) % 1000003; }
/* static DEGIL, const DEGIL: derleyici icerigi derleme zamaninda varsayamaz,
   cagri gercekten dolayli kalir (Rust'taki black_box'in karsiligi). */
long (*tab[2])(long) = {f, g};
int main(void) {
    const char *e = getenv("BENCH_N"); long n = e ? atol(e) : 0; if (n <= 0) n = 20000000;
    long acc = 1;
    for (long i = 0; i < n; i++) acc = tab[acc & 1](acc);
    printf("%ld\n", acc);
    return 0;
}

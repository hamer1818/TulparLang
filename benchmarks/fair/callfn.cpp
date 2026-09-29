#include <cstdio>
#include <cstdlib>
static long f(long x) { return (x * 31 + 7) % 1000003; }
static long g(long x) { return (x * 17 + 3) % 1000003; }
long (*tab[2])(long) = {f, g};
int main() {
    const char *e = std::getenv("BENCH_N"); long n = e ? std::atol(e) : 0; if (n <= 0) n = 20000000;
    long acc = 1;
    for (long i = 0; i < n; i++) acc = tab[acc & 1](acc);
    std::printf("%ld\n", acc);
    return 0;
}

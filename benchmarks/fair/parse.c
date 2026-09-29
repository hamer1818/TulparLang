// C'de split yok: tampon elle buyutulur, sayilar strtol ile yerinde yurunur
// (C programcisinin deyimsel yolu — parca dizisi ayirmaz).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    const char *e = getenv("BENCH_N"); long n = e ? atol(e) : 0; if (n <= 0) n = 1000000;
    size_t cap = 1024, len = 0; char *s = malloc(cap);
    for (long i = 0; i < n; i++) {
        if (len + 16 >= cap) { cap *= 2; s = realloc(s, cap); }
        if (i > 0) s[len++] = ',';
        long v = (i * 7919) % 100000; char t[16]; int k = 0;
        if (v == 0) t[k++] = '0';
        while (v > 0) { t[k++] = (char)('0' + v % 10); v /= 10; }
        while (k > 0) s[len++] = t[--k];
    }
    s[len] = 0;
    long cnt = 0, sum = 0; char *p = s, *end;
    for (;;) { sum += strtol(p, &end, 10); cnt++; if (*end != ',') break; p = end + 1; }
    printf("%ld %ld\n", cnt, sum);
    return 0;
}

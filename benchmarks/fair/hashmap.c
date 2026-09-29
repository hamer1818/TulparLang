// C'de stdlib sozlugu yok: acik adresleme (dogrusal yoklama) + FNV-1a,
// kapasite 2'nin kuvveti >= 2N. Anahtarlar kopyalanip saklaniyor; sayi ->
// dizgi elle (snprintf, strcat kiyasinda C'yi 2,8x yavaslatmisti).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static int kfmt(char *b, long v) {
    char t[24]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0) { t[n++] = (char)('0' + v % 10); v /= 10; }
    b[0] = 'k';
    for (int i = 0; i < n; i++) b[1 + i] = t[n - 1 - i];
    b[1 + n] = 0;
    return 1 + n;
}
static uint64_t fnv(const char *s, int len) {
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < len; i++) { h ^= (unsigned char)s[i]; h *= 1099511628211ULL; }
    return h;
}
int main(void) {
    const char *e = getenv("BENCH_N"); long n = e ? atol(e) : 0; if (n <= 0) n = 1000000;
    size_t cap = 1; while (cap < (size_t)n * 2) cap <<= 1;
    char **keys = calloc(cap, sizeof(char *)); long *vals = malloc(cap * sizeof(long));
    long count = 0; char b[32];
    for (long i = 0; i < n; i++) {
        int len = kfmt(b, i); size_t h = fnv(b, len) & (cap - 1);
        while (keys[h] && strcmp(keys[h], b) != 0) h = (h + 1) & (cap - 1);
        if (!keys[h]) { keys[h] = malloc(len + 1); memcpy(keys[h], b, len + 1); count++; }
        vals[h] = i;
    }
    long s = 0;
    for (long i = 0; i < n; i++) {
        int len = kfmt(b, (i * 7) % n); size_t h = fnv(b, len) & (cap - 1);
        while (strcmp(keys[h], b) != 0) h = (h + 1) & (cap - 1);
        s += vals[h];
    }
    printf("%ld %ld\n", count, s);
    return 0;
}

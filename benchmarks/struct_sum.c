#include <stdio.h>

typedef struct {
    long long x;
    long long y;
    long long z;
} V3;

static long long sum_v3(long long n) {
    V3 acc = {0, 0, 0};
    for (long long i = 0; i < n; i++) {
        V3 v = { i, i * 2, i * 3 };
        acc.x += v.x;
        acc.y += v.y;
        acc.z += v.z;
    }
    return acc.x + acc.y + acc.z;
}

int main(void) {
    printf("%lld\n", sum_v3(10000000LL));
    return 0;
}

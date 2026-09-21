#include <stdio.h>
#include <stdlib.h>

typedef struct {
    double x;
    double y;
    double z;
} Vec3;

static double sum_vec3(long long n) {
    Vec3 acc = {0.0, 0.0, 0.0};
    for (long long i = 0; i < n; i++) {
        double f = i * 0.5;
        acc.x += f;
        acc.y += f * 2.0;
        acc.z += f * 3.0;
    }
    return acc.x + acc.y + acc.z;
}

int main(void) {
    const char *e = getenv("TULPAR_BENCH_N");
    long long n = e ? atoll(e) : 0;
    if (n <= 0) n = 10000000LL;
    printf("%g\n", sum_vec3(n));
    return 0;
}

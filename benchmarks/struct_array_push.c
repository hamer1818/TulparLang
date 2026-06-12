// C reference for struct_array_push: 1M typed struct push + readback.
// Same shape as the Tulpar version; uses dynamic array to mirror the
// `array` resize path Tulpar's runtime takes (ObjArray's items vector
// is realloc'd on capacity overflow). int64 fields match Tulpar's V3.

#include <stdio.h>
#include <stdlib.h>

typedef struct { long long x, y, z; } V3;

static V3 make_v3(long long a, long long b, long long c) {
    V3 v = {a, b, c};
    return v;
}

int main(void) {
    const int n = 1000000;
    V3 *points = (V3 *)malloc((size_t)n * sizeof(V3));
    if (!points) return 1;

    for (int i = 0; i < n; i++) {
        points[i] = make_v3(i, (long long)i * 2, (long long)i * 3);
    }

    long long total = 0;
    for (int i = 0; i < n; i++) {
        V3 p = points[i];
        total += p.x + p.y + p.z;
    }

    printf("%lld\n", total);
    free(points);
    return 0;
}

#include <stdio.h>

int main(void) {
    long long total = 0;
    for (long long i = 0; i < 10000000LL; i++) {
        total += i;
    }
    printf("%lld\n", total);
    return 0;
}

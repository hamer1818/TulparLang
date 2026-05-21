#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sieve(int n) {
    char *flags = (char *)calloc(n + 1, 1);
    int count = 0;
    for (int i = 2; i <= n; i++) {
        if (!flags[i]) {
            count++;
            for (long long k = (long long)i * i; k <= n; k += i) {
                flags[k] = 1;
            }
        }
    }
    free(flags);
    return count;
}

int main(void) {
    printf("%d\n", sieve(100000));
    return 0;
}

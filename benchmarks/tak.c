#include <stdio.h>

int tak(int x, int y, int z) {
    if (y < x) {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    return z;
}

int main(void) {
    printf("%d\n", tak(18, 12, 6));
    return 0;
}

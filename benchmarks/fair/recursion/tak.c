#include <stdio.h>
#include <stdlib.h>
static long long tak(long long x,long long y,long long z){
    if(y>=x) return z;
    return tak(tak(x-1,y,z),tak(y-1,z,x),tak(z-1,x,y));
}
int main(void){const char*e=getenv("BENCH_N");long long n=e?atoll(e):0;if(n<=0)n=18;
    printf("%lld\n",tak(n,n/2,0));return 0;}

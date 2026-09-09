#include <stdio.h>
#include <stdlib.h>
static long long fib(long long n){ return n<=1?n:fib(n-1)+fib(n-2); }
int main(void){
    const char*e=getenv("BENCH_N"); long long n=e?atoll(e):0; if(n<=0)n=32;
    printf("%lld\n",fib(n)); return 0;
}

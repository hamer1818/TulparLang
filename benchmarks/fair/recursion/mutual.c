#include <stdio.h>
#include <stdlib.h>
static long long odd_(long long n);
static long long even_(long long n){ return n==0?1:odd_(n-1); }
static long long odd_(long long n){ return n==0?0:even_(n-1); }
int main(void){const char*e=getenv("BENCH_N");long long n=e?atoll(e):0;if(n<=0)n=30000000;
    long long s=0; for(long long i=0;i<n;i+=1000) s+=even_(i%1000);
    printf("%lld\n",s);return 0;}

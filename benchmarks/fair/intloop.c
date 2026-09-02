#include <stdio.h>
#include <stdlib.h>
int main(void){
    const char*e=getenv("BENCH_N"); long long n=e?atoll(e):0; if(n<=0)n=50000000LL;
    long long t=1;
    for(long long i=0;i<n;i++) t=(t*31+i)%1000000007LL;
    printf("%lld\n",t); return 0;
}

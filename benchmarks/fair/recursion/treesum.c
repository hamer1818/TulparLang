#include <stdio.h>
#include <stdlib.h>
static long long tsum(long long node,long long depth){
    if(depth==0) return node;
    return node + tsum(node*2,depth-1) + tsum(node*2+1,depth-1);
}
int main(void){const char*e=getenv("BENCH_N");long long n=e?atoll(e):0;if(n<=0)n=24;
    printf("%lld\n",tsum(1,n));return 0;}

#include <stdio.h>
#include <stdlib.h>
int main(void){
    const char*e=getenv("BENCH_N"); long n=e?atol(e):0; if(n<=0)n=5000000;
    int*f=(int*)calloc(n+1,sizeof(int)); long c=0;
    for(long i=2;i<=n;i++){ if(!f[i]){ c++; for(long k=i*i;k<=n;k+=i) f[k]=1; } }
    free(f); printf("%ld\n",c); return 0;
}

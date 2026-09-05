/* Dizi uzerinde deyimsel yineleme: doldur + topla.
   Her dil kendi deyimsel uzunluk erisimini kullaniyor (C'de uzunluk
   dilde yok, n tasiniyor; otekilerde len()/.len()/.length). */
#include <stdio.h>
#include <stdlib.h>
int main(void){
    const char*e=getenv("BENCH_N"); long n=e?atol(e):0; if(n<=0)n=5000000;
    long long*a=(long long*)malloc(n*sizeof(long long)); long long t=0;
    for(long i=0;i<n;i++) a[i]=i;
    for(long i=0;i<n;i++) t+=a[i];
    free(a); printf("%lld\n",t); return 0;
}

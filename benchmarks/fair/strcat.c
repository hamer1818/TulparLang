#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    const char*e=getenv("BENCH_N"); long n=e?atol(e):0; if(n<=0)n=2000000;
    size_t cap=16, len=0; char*b=(char*)malloc(cap);
    b[0]=0;
    for(long i=0;i<n;i++){
        char t[24]; int m=snprintf(t,sizeof t,"%ld,",i%1000);
        if(len+m+1>cap){ while(len+m+1>cap) cap*=2; b=(char*)realloc(b,cap); }
        memcpy(b+len,t,m); len+=m; b[len]=0;
    }
    long cnt=0; for(size_t i=0;i<len;i++) if(b[i]==',') cnt++;
    printf("%zu %ld\n",len,cnt); free(b); return 0;
}

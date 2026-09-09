/* strcat.c ile AYNI algoritma, tek fark: snprintf yerine elle itoa.
   Kullanicinin ongorusunu sinar: "C ayni itoa'yi alinca strcat'i geri alir." */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int u2s(char*p,long v){            /* v >= 0 */
    char t[24]; int i=0;
    do { t[i++]=(char)('0'+v%10); v/=10; } while(v);
    for(int j=0;j<i;j++) p[j]=t[i-1-j];
    return i;
}
int main(void){
    const char*e=getenv("BENCH_N"); long n=e?atol(e):0; if(n<=0)n=2000000;
    size_t cap=16,len=0; char*b=(char*)malloc(cap); b[0]=0;
    for(long i=0;i<n;i++){
        char t[24]; int m=u2s(t,i%1000); t[m++]=',';
        if(len+m+1>cap){ while(len+m+1>cap) cap*=2; b=(char*)realloc(b,cap); }
        memcpy(b+len,t,m); len+=m; b[len]=0;
    }
    long cnt=0; for(size_t i=0;i<len;i++) if(b[i]==',') cnt++;
    printf("%zu %ld\n",len,cnt); free(b); return 0;
}

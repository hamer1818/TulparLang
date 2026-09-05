#include <cstdio>
#include <cstdlib>
int main(){
    const char*e=std::getenv("BENCH_N"); long long n=e?std::atoll(e):0; if(n<=0)n=50000000LL;
    long long t=1;
    for(long long i=0;i<n;i++) t=(t*31+i)%1000000007LL;
    std::printf("%lld\n",t); return 0;
}

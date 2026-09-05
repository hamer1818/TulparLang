#include <cstdio>
#include <cstdlib>
static long long fib(long long n){ return n<=1?n:fib(n-1)+fib(n-2); }
int main(){
    const char*e=std::getenv("BENCH_N"); long long n=e?std::atoll(e):0; if(n<=0)n=32;
    std::printf("%lld\n",fib(n)); return 0;
}

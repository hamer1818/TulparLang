// C'nin calloc'u yerine deyimsel kap: std::vector<int> (Rust'un vec![0i32] esi).
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(){
    const char*e=std::getenv("BENCH_N"); long n=e?std::atol(e):0; if(n<=0)n=5000000;
    std::vector<int> f(n+1,0); long c=0;
    for(long i=2;i<=n;i++){ if(!f[i]){ c++; for(long k=i*i;k<=n;k+=i) f[k]=1; } }
    std::printf("%ld\n",c); return 0;
}

// Dizi uzerinde deyimsel yineleme: doldur + topla. C++'ta uzunluk KAPTA
// (a.size()) — C'deki gibi elle tasinan `n` degil; kural bu: her dil kendi
// deyimsel uzunluk erisimini kullaniyor.
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(){
    const char*e=std::getenv("BENCH_N"); long n=e?std::atol(e):0; if(n<=0)n=5000000;
    std::vector<long long> a(n); long long t=0;
    for(size_t i=0;i<a.size();i++) a[i]=(long long)i;
    for(size_t i=0;i<a.size();i++) t+=a[i];
    std::printf("%lld\n",t); return 0;
}

// Deyimsel C++ dizgi kurma: std::string += (Java StringBuilder, Go
// strings.Builder, Rust String::push_str ile ayni rol).
#include <cstdio>
#include <cstdlib>
#include <string>
int main(){
    const char*e=std::getenv("BENCH_N"); long n=e?std::atol(e):0; if(n<=0)n=2000000;
    std::string b;
    for(long i=0;i<n;i++){ b += std::to_string(i%1000); b += ','; }
    long cnt=0; for(char ch : b) if(ch==',') cnt++;
    std::printf("%zu %ld\n",b.size(),cnt); return 0;
}

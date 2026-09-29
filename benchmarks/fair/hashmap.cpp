#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
int main() {
    const char *e = std::getenv("BENCH_N"); long n = e ? std::atol(e) : 0; if (n <= 0) n = 1000000;
    std::unordered_map<std::string, long> m;
    for (long i = 0; i < n; i++) m["k" + std::to_string(i)] = i;
    long s = 0;
    for (long i = 0; i < n; i++) s += m["k" + std::to_string((i * 7) % n)];
    std::printf("%zu %ld\n", m.size(), s);
    return 0;
}

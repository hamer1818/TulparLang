#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
int main() {
    const char *e = std::getenv("BENCH_N"); long n = e ? std::atol(e) : 0; if (n <= 0) n = 1000000;
    std::string s;
    for (long i = 0; i < n; i++) { if (i > 0) s += ','; s += std::to_string((i * 7919) % 100000); }
    std::string_view v(s); long cnt = 0, sum = 0; size_t pos = 0;
    for (;;) {
        size_t c = v.find(',', pos); size_t stop = c == std::string_view::npos ? v.size() : c;
        long x = 0; std::from_chars(v.data() + pos, v.data() + stop, x); sum += x; cnt++;
        if (c == std::string_view::npos) break; pos = c + 1;
    }
    std::printf("%ld %ld\n", cnt, sum);
    return 0;
}

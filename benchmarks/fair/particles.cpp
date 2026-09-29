#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct P { double x, y, vx, vy; };
int main() {
    const char *e = std::getenv("BENCH_N"); long n = e ? std::atol(e) : 0; if (n <= 0) n = 200000;
    std::vector<P> ps(n);
    for (long i = 0; i < n; i++) ps[i] = P{(double)(i % 1000) * 0.5, (double)(i % 777) * 0.25, (double)((i % 13) - 6) * 0.1, (double)((i % 7) - 3) * 0.2};
    const double dt = 0.01, g = 9.81, w = 500.0;
    for (int s = 0; s < 50; s++)
        for (auto &p : ps) {
            p.vy = p.vy - g * dt;
            p.x = p.x + p.vx * dt;
            p.y = p.y + p.vy * dt;
            if (p.x < 0.0 || p.x > w) p.vx = 0.0 - p.vx;
            if (p.y < 0.0) { p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9; }
        }
    double sum = 0.0;
    for (auto &p : ps) sum = sum + p.x + p.y;
    std::printf("%lld\n", (long long)std::llround(sum * 1000.0));
    return 0;
}

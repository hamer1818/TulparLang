// Tepe bellek olcer: hedefi fork+exec eder, wait4'un ru_maxrss'ini (KB)
// stderr'e "RSS_KB=<n>" diye basar. NEDEN AYRI BIR SARMALAYICI: Python'dan
// dogrudan olculen ru_maxrss, exec'ten ONCEKI catallanmis Python kopyasini da
// sayiyor (~17 MB taban) — kucuk programlarin hepsi 17 MB gorunuyordu. Bu
// sarmalayicinin kendi kopyasi ~1 MB, taban oraya iniyor.
#include <stdio.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    pid_t p = fork();
    if (p == 0) { execvp(argv[1], argv + 1); _exit(127); }
    int st = 0; struct rusage ru;
    if (wait4(p, &st, 0, &ru) < 0) return 3;
    fprintf(stderr, "RSS_KB=%ld\n", ru.ru_maxrss);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

// loadtest.c — minimal but accurate HTTP/1.1 keep-alive load generator.
//
// Closed-loop model: `concurrency` threads, each owns ONE keep-alive
// connection and fires request→response back-to-back until the duration
// elapses. Effective concurrency == thread count. Records per-request
// latency into a 1µs-bucket histogram (up to 200ms) and reports
// RPS + p50/p90/p99/p99.9/max + throughput + status tallies.
//
// Build:  gcc -O2 -pthread -o loadtest loadtest.c
// Usage:  ./loadtest <host> <port> <conc> <dur_s> <method> <path> [body] [keepalive|close]
//   keepalive (default): one persistent connection per thread, requests
//                        fired back-to-back (peak per-connection throughput).
//   close:               new connection per request (Connection: close) —
//                        models many short-lived web clients.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define HIST_MAX 200000          // microseconds (200 ms), 1µs buckets
#define BUFCAP   16384

static const char *g_host;
static int   g_port;
static double g_stop_at;          // CLOCK_MONOTONIC deadline (seconds)
static char  g_request[8192];
static int   g_request_len;
static int   g_close;             // 1 = new connection per request

typedef struct {
    long *hist;                   // [HIST_MAX+1]
    long c2xx, cother, cerr;
    long bytes;
} TStat;

static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static long now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000;
}

static int connect_one(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    struct sockaddr_in sa; memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_port);
    inet_pton(AF_INET, g_host, &sa.sin_addr);
    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) { close(fd); return -1; }
    return fd;
}

static int write_all(int fd, const char *p, int n) {
    int off = 0;
    while (off < n) {
        int w = send(fd, p + off, n - off, 0);
        if (w <= 0) return -1;
        off += w;
    }
    return 0;
}

// Read exactly one HTTP response. `buf`/`*have` carry leftover bytes between
// calls. Returns status code (>=0) and sets *resp_sz to total response bytes;
// -1 on connection error.
static int read_response(int fd, char *buf, int *have, int *resp_sz) {
    int hdr_end = -1, cl = -1, status = -1;
    for (;;) {
        if (hdr_end < 0) {
            for (int i = 3; i < *have; i++) {
                if (buf[i-3]=='\r' && buf[i-2]=='\n' && buf[i-1]=='\r' && buf[i]=='\n') {
                    hdr_end = i - 3; break;
                }
            }
            if (hdr_end >= 0) {
                if (*have >= 12 && memcmp(buf, "HTTP/1.", 7) == 0)
                    status = atoi(buf + 9);
                // case-insensitive Content-Length within the header block
                for (int i = 0; i + 15 < hdr_end; i++) {
                    if (strncasecmp(buf + i, "Content-Length:", 15) == 0) {
                        cl = atoi(buf + i + 15);
                        break;
                    }
                }
                if (cl < 0) cl = 0;     // no body
            }
        }
        if (hdr_end >= 0 && cl >= 0) {
            int total = hdr_end + 4 + cl;
            if (*have >= total) {
                int leftover = *have - total;
                if (leftover > 0) memmove(buf, buf + total, leftover);
                *have = leftover;
                *resp_sz = total;
                return status;
            }
        }
        if (*have >= BUFCAP) return -1;   // response too large for buffer
        int n = recv(fd, buf + *have, BUFCAP - *have, 0);
        if (n <= 0) return -1;
        *have += n;
    }
}

static void *worker(void *arg) {
    TStat *st = (TStat *)arg;
    st->hist = (long *)calloc(HIST_MAX + 1, sizeof(long));
    char *buf = (char *)malloc(BUFCAP);
    int fd = connect_one();
    int have = 0;
    while (now_sec() < g_stop_at) {
        if (fd < 0) { fd = connect_one(); have = 0; if (fd < 0) { st->cerr++; continue; } }
        long t0 = now_us();
        if (write_all(fd, g_request, g_request_len) < 0) { close(fd); fd = -1; st->cerr++; continue; }
        int rsz = 0;
        int status = read_response(fd, buf, &have, &rsz);
        if (status < 0) { close(fd); fd = -1; st->cerr++; continue; }
        long dt = now_us() - t0;
        if (dt < 0) dt = 0; if (dt > HIST_MAX) dt = HIST_MAX;
        st->hist[dt]++;
        if (status >= 200 && status < 300) st->c2xx++; else st->cother++;
        st->bytes += rsz;
        if (g_close) { close(fd); fd = -1; have = 0; }
    }
    if (fd >= 0) close(fd);
    free(buf);
    return NULL;
}

static long pct(const long *hist, long total, double p) {
    long target = (long)(total * p);
    long acc = 0;
    for (long i = 0; i <= HIST_MAX; i++) {
        acc += hist[i];
        if (acc >= target) return i;
    }
    return HIST_MAX;
}

int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr, "usage: %s host port conc dur_s method path [body]\n", argv[0]);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    g_host = argv[1];
    g_port = atoi(argv[2]);
    int conc = atoi(argv[3]);
    int dur = atoi(argv[4]);
    const char *method = argv[5];
    const char *path = argv[6];
    const char *body = argc > 7 ? argv[7] : "";
    g_close = (argc > 8 && strcmp(argv[8], "close") == 0) ? 1 : 0;
    const char *conn = g_close ? "close" : "keep-alive";
    int blen = (int)strlen(body);

    if (blen > 0) {
        g_request_len = snprintf(g_request, sizeof(g_request),
            "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: %s\r\n"
            "Content-Type: application/json\r\nContent-Length: %d\r\n\r\n%s",
            method, path, g_host, g_port, conn, blen, body);
    } else {
        g_request_len = snprintf(g_request, sizeof(g_request),
            "%s %s HTTP/1.1\r\nHost: %s:%d\r\nConnection: %s\r\n\r\n",
            method, path, g_host, g_port, conn);
    }

    TStat *stats = (TStat *)calloc(conc, sizeof(TStat));
    pthread_t *th = (pthread_t *)malloc(conc * sizeof(pthread_t));
    double start = now_sec();
    g_stop_at = start + dur;
    for (int i = 0; i < conc; i++) pthread_create(&th[i], NULL, worker, &stats[i]);
    for (int i = 0; i < conc; i++) pthread_join(th[i], NULL);
    double elapsed = now_sec() - start;

    long *agg = (long *)calloc(HIST_MAX + 1, sizeof(long));
    long total = 0, c2xx = 0, cother = 0, cerr = 0, bytes = 0;
    for (int i = 0; i < conc; i++) {
        if (stats[i].hist) for (long j = 0; j <= HIST_MAX; j++) agg[j] += stats[i].hist[j];
        c2xx += stats[i].c2xx; cother += stats[i].cother;
        cerr += stats[i].cerr; bytes += stats[i].bytes;
    }
    total = c2xx + cother;
    double sum_us = 0;
    for (long j = 0; j <= HIST_MAX; j++) sum_us += (double)agg[j] * j;

    printf("conc=%d dur=%.1fs req=%ld 2xx=%ld other=%ld err=%ld\n",
           conc, elapsed, total, c2xx, cother, cerr);
    printf("  RPS        : %.0f\n", total / elapsed);
    printf("  throughput : %.1f MB/s\n", bytes / elapsed / 1e6);
    if (total > 0) {
        printf("  latency us : avg=%.0f p50=%ld p90=%ld p99=%ld p99.9=%ld max=%ld\n",
               sum_us / total, pct(agg, total, 0.50), pct(agg, total, 0.90),
               pct(agg, total, 0.99), pct(agg, total, 0.999), pct(agg, total, 1.0) );
    }
    return 0;
}

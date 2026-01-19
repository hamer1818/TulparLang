// ============================================
// C PERFORMANS BENCHMARK TESTİ
// ============================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
double get_time_ms() {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)freq.QuadPart;
}
#else
#include <sys/time.h>
double get_time_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

// TEST 1: Fibonacci (Recursive)
long long fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// TEST 2: Factorial (Recursive)
long long factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// TEST 3: Loop Test (volatile to prevent optimization)
long long loop_test(int iterations) {
    volatile long long sum = 0;
    for (volatile int i = 0; i < iterations; i++) {
        sum += i;
    }
    return sum;
}

// TEST 4: Nested Loop (volatile to prevent optimization)
long long nested_loop(int size) {
    volatile long long count = 0;
    for (volatile int i = 0; i < size; i++) {
        for (volatile int j = 0; j < size; j++) {
            count++;
        }
    }
    return count;
}

// TEST 5: String Concatenation
int string_test(int iterations) {
    char *result = malloc(iterations + 1);
    result[0] = '\0';
    for (int i = 0; i < iterations; i++) {
        result[i] = 'a';
    }
    result[iterations] = '\0';
    int len = strlen(result);
    free(result);
    return len;
}

void print_time(double ms) {
    if (ms < 0.001) {
        printf("  Sure: %.3f us\n", ms * 1000);
    } else if (ms < 1.0) {
        printf("  Sure: %.3f ms\n", ms);
    } else {
        printf("  Sure: %.3f ms\n", ms);
    }
}

int main() {
    printf("=======================================================\n");
    printf("              C PERFORMANS BENCHMARK\n");
    printf("=======================================================\n\n");

    double total_time = 0.0;
    double start, end, elapsed;

    // Test 1: Fibonacci
    printf("Test 1: Fibonacci(30)\n");
    start = get_time_ms();
    long long fib_result = fibonacci(30);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", fib_result);
    print_time(elapsed);
    printf("\n");

    // Test 2: Factorial
    printf("Test 2: Factorial(20)\n");
    start = get_time_ms();
    long long fact_result = factorial(20);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", fact_result);
    print_time(elapsed);
    printf("\n");

    // Test 3: Loop
    printf("Test 3: Loop 1.000.000 iterasyon\n");
    start = get_time_ms();
    long long loop_result = loop_test(1000000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", loop_result);
    print_time(elapsed);
    printf("\n");

    // Test 4: Nested Loop
    printf("Test 4: Nested Loop 1000x1000\n");
    start = get_time_ms();
    long long nested_result = nested_loop(1000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", nested_result);
    print_time(elapsed);
    printf("\n");

    // Test 5: String
    printf("Test 5: String concatenation 100000\n");
    start = get_time_ms();
    int str_result = string_test(100000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", str_result);
    print_time(elapsed);
    printf("\n");

    printf("=======================================================\n");
    printf("TOPLAM SURE: %.3f ms\n", total_time);
    printf("=======================================================\n");

    return 0;
}

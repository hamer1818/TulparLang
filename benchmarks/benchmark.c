// ============================================
// C PERFORMANS BENCHMARK TESTİ
// Tulpar ile uyumlu testler
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

// TEST 3: Ackermann (Deep Recursion)
long long ackermann(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

// TEST 4: Tak (Takeuchi) - Heavy Recursion
int tak(int x, int y, int z) {
    if (y < x) {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    return z;
}

// TEST 5: Loop Test
long long loop_test(int iterations) {
    long long sum = 0;
    for (int i = 0; i < iterations; i++) {
        sum += i;
    }
    return sum;
}

// TEST 6: Sieve of Eratosthenes
int sieve(int n) {
    int *flags = (int*)calloc(n + 1, sizeof(int));
    int count = 0;
    for (int i = 2; i <= n; i++) {
        if (flags[i] == 0) {
            count++;
            for (int k = i * i; k <= n; k += i) {
                flags[k] = 1;
            }
        }
    }
    free(flags);
    return count;
}

// TEST 7: Bubble Sort - with full checksum to prevent dead code elimination
int bubble_sort(int size) {
    int *arr = (int*)malloc(size * sizeof(int));
    int seed = 12345;
    for (int i = 0; i < size; i++) {
        seed = (seed * 1103515245 + 12345) % 32768;
        arr[i] = seed;
    }
    
    for (int i = 0; i < size; i++) {
        for (int j = 0; j < size - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                int temp = arr[j];
                arr[j] = arr[j + 1];
                arr[j + 1] = temp;
            }
        }
    }
    
    // Compute checksum of ALL elements to prevent optimization
    long long checksum = 0;
    for (int i = 0; i < size; i++) {
        checksum += arr[i];
    }
    free(arr);
    return (int)checksum;
}

// TEST 8: String Concatenation
int string_concat(int iterations) {
    char *result = (char*)malloc(iterations + 1);
    result[0] = '\0';
    for (int i = 0; i < iterations; i++) {
        result[i] = 'a';
        result[i + 1] = '\0';
    }
    int len = strlen(result);
    free(result);
    return len;
}

// TEST 9: Array Memory
long long array_memory(int size) {
    int *arr = (int*)malloc(size * sizeof(int));
    for (int i = 0; i < size; i++) {
        arr[i] = i;
    }
    long long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += arr[i];
    }
    free(arr);
    return sum;
}

// TEST 10: String Allocation (Heavy)
int string_allocation(int iterations) {
    // Pre-allocate for efficiency
    int str_len = 14; // "Merhaba Dunya "
    char *result = (char*)malloc(iterations * str_len + 1);
    result[0] = '\0';
    for (int i = 0; i < iterations; i++) {
        strcat(result, "Merhaba Dunya ");
    }
    int len = strlen(result);
    free(result);
    return len;
}

// TEST 11: JSON Build (Simulated with structs)
typedef struct { int id; int value; int active; } JsonItem;
long long json_build_test(int size) {
    JsonItem *data = (JsonItem*)malloc(size * sizeof(JsonItem));
    for (int i = 0; i < size; i++) {
        data[i].id = i;
        data[i].value = i * 10;
        data[i].active = 1;
    }
    long long sum = 0;
    for (int i = 0; i < size; i++) {
        sum += data[i].value;
    }
    free(data);
    return sum;
}

void print_time(double ms) {
    if (ms < 1.0) {
        printf("  Sure: %.2f us\n", ms * 1000);
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

    // Test 3: Ackermann
    printf("Test 3: Ackermann(3, 8)\n");
    start = get_time_ms();
    long long ack_result = ackermann(3, 8);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", ack_result);
    print_time(elapsed);
    printf("\n");

    // Test 4: Tak
    printf("Test 4: Tak(18, 12, 6)\n");
    start = get_time_ms();
    int tak_result = tak(18, 12, 6);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", tak_result);
    print_time(elapsed);
    printf("\n");

    // Test 5: Loop
    printf("Test 5: Loop 1M\n");
    start = get_time_ms();
    long long loop_result = loop_test(1000000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", loop_result);
    print_time(elapsed);
    printf("\n");

    // Test 6: Sieve
    printf("Test 6: Sieve(10000)\n");
    start = get_time_ms();
    int sieve_result = sieve(10000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", sieve_result);
    print_time(elapsed);
    printf("\n");

    // Test 7: Bubble Sort
    printf("Test 7: BubbleSort(1000)\n");
    start = get_time_ms();
    int bubble_result = bubble_sort(100);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", bubble_result);
    print_time(elapsed);
    printf("\n");

    // Test 8: String Concat
    printf("Test 8: StringConcat(1000)\n");
    start = get_time_ms();
    int str_result = string_concat(1000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", str_result);
    print_time(elapsed);
    printf("\n");

    // Test 9: Array Memory
    printf("Test 9: ArrayMemory(10000)\n");
    start = get_time_ms();
    long long arr_result = array_memory(10000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", arr_result);
    print_time(elapsed);
    printf("\n");
    // Test 10: String Allocation
    printf("Test 10: StringAllocation(1000)\n");
    start = get_time_ms();
    int str_alloc_result = string_allocation(1000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %d\n", str_alloc_result);
    print_time(elapsed);
    printf("\n");

    // Test 11: JSON Build
    printf("Test 11: JSONBuild(1000)\n");
    start = get_time_ms();
    long long json_result = json_build_test(1000);
    end = get_time_ms();
    elapsed = end - start;
    total_time += elapsed;
    printf("  Sonuc: %lld\n", json_result);
    print_time(elapsed);
    printf("\n");
    printf("=======================================================\n");
    printf("TOPLAM SURE: %.3f ms\n", total_time);
    printf("=======================================================\n");

    return 0;
}

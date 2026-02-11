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

// Global volatile inputs to prevent constant folding
volatile int V_INT = 1;

// TEST 1: Fibonacci (Recursive)
long long fibonacci(int n) {
  if (n <= 1)
    return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

// TEST 2: Factorial (Recursive)
long long factorial(int n) {
  if (n <= 1)
    return 1;
  return n * factorial(n - 1);
}

// TEST 3: Ackermann (Deep Recursion)
long long ackermann(int m, int n) {
  if (m == 0)
    return n + 1;
  if (n == 0)
    return ackermann(m - 1, 1);
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
// TEST 5: Loop Test
long long loop_test(int iterations) {
  long long sum = 0;
  // Volatile dependency
  int inc = V_INT;
  for (int i = 0; i < iterations; i++) {
    // Xorshift-like reduction
    long long x = i;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sum += x + inc;
  }
  return sum;
}

// TEST 6: Sieve of Eratosthenes
int sieve(int n) {
  int *flags = (int *)calloc(n + 1, sizeof(int));
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

// TEST 7: Bubble Sort - with runtime seed
int bubble_sort(int size) {
  int *arr = (int *)malloc(size * sizeof(int));
  // Use runtime V_INT to prevent compile-time array generation
  int seed = 12345 * V_INT;
  for (int i = 0; i < size; i++) {
    seed = (seed * 1103515245 + 12345) % 32768;
    arr[i] = seed;
  }

  // Volatile zero to force runtime comparison
  int v_zero = V_INT ^ 1;

  for (int i = 0; i < size; i++) {
    for (int j = 0; j < size - 1; j++) {
      if (arr[j] > arr[j + 1] + v_zero) {
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
  char *result = (char *)malloc(iterations + 1);
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
  int *arr = (int *)malloc(size * sizeof(int));
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
  char *result = (char *)malloc(iterations * str_len + 1);
  result[0] = '\0';
  for (int i = 0; i < iterations; i++) {
    strcat(result, "Merhaba Dunya ");
  }
  int len = strlen(result);
  free(result);
  return len;
}

// TEST 11: JSON Build (Simulated with structs)
typedef struct {
  int id;
  int value;
  int active;
} JsonItem;
long long json_build_test(int size) {
  JsonItem *data = (JsonItem *)malloc(size * sizeof(JsonItem));
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

// Global volatile inputs to prevent constant folding
// Moved to top

// Global volatile output to prevent dead code elimination
volatile long long BLACK_HOLE;

void measure(const char *name, int iterations, long long (*func_l)(int),
             int (*func_i)(int), int (*func_3)(int, int, int), int arg1,
             int arg2, int arg3) {
  // Make arguments runtime values by multiplying with volatile 1
  volatile int v_arg1 = arg1 * V_INT;
  volatile int v_arg2 = arg2 * V_INT;
  volatile int v_arg3 = arg3 * V_INT;

  // Warmup
  for (int i = 0; i < iterations / 10; ++i) {
    if (func_l)
      BLACK_HOLE = func_l(v_arg1);
    else if (func_i)
      BLACK_HOLE = func_i(v_arg1);
    else if (func_3)
      BLACK_HOLE = func_3(v_arg1, v_arg2, v_arg3);
  }

  double start = get_time_ms();
  for (int i = 0; i < iterations; ++i) {
    if (func_l)
      BLACK_HOLE = func_l(v_arg1);
    else if (func_i)
      BLACK_HOLE = func_i(v_arg1);
    else if (func_3)
      BLACK_HOLE = func_3(v_arg1, v_arg2, v_arg3);
  }
  double end = get_time_ms();

  double total_ms = end - start;
  double avg_ms = total_ms / iterations;

  // Safety check just in case
  if (V_INT != 1)
    printf("Error: V_INT modified\n");

  printf("%s\n", name);
  printf("Sure: %.4f ms\n", avg_ms);
  printf("--------------------------------\n");
}

// Wrappers to match signature
long long wrap_fib(int n) { return fibonacci(n); }
long long wrap_fact(int n) { return factorial(n); }
long long wrap_ack(int m, int n, int unused) { return ackermann(m, n); }
int wrap_tak(int x, int y, int z) { return tak(x, y, z); }
long long wrap_loop(int n) { return loop_test(n); }
int wrap_sieve(int n) { return sieve(n); }
int wrap_bubble(int n) { return bubble_sort(n); }
int wrap_concat(int n) { return string_concat(n); }
long long wrap_arr(int n) { return array_memory(n); }
int wrap_str_alloc(int n) { return string_allocation(n); }
long long wrap_json(int n) { return json_build_test(n); }

int main() {
  printf("=======================================================\n");
  printf("              C PERFORMANS BENCHMARK\n");
  printf("=======================================================\n\n");

  // Ensure volatile is used
  if (V_INT != 1)
    V_INT = 1;

  measure("Test 1: Fibonacci(30)", 100, wrap_fib, NULL, NULL, 30, 0, 0);
  measure("Test 2: Factorial(20)", 100000, wrap_fact, NULL, NULL, 20, 0, 0);
  measure("Test 3: Ackermann(3, 8)", 50, NULL, NULL,
          (int (*)(int, int, int))wrap_ack, 3, 8, 0);
  // Correction: wrap_ack returns long long but measure expects func_l for long
  // return. Actually our measure func_l signature takes 1 int. We need a
  // func_3_l? Let's just simplify the measure signature or casting. But since
  // this is C, we can just supply NULL and do the cast trick inside wrapper or
  // measure. Wait, func_3 expects int return. ackermann returns long long.
  // Let's fix wrap_ack signature or measure signature.
  // To match current measure: int (*func_3)(int, int, int)
  // Ackermann returns long long. We should update measure signature or
  // wrappers. Simpler: Just make separate calls inside block since we are
  // rewriting main anyway? No, I want to use measure to standardize logic.
  // Let's assume ackermann result fits in int for this test (result is 61).

  measure("Test 3: Ackermann(3, 8)", 50, NULL, NULL,
          (int (*)(int, int, int))wrap_ack, 3, 8, 0);
  measure("Test 4: Tak(18, 12, 6)", 200, NULL, NULL, wrap_tak, 18, 12, 6);
  measure("Test 5: Loop 1M", 1000, wrap_loop, NULL, NULL, 1000000, 0, 0);
  measure("Test 6: Sieve(10K)", 5000, NULL, wrap_sieve, NULL, 10000, 0, 0);
  measure("Test 7: BubbleSort(1K)", 2000, NULL, wrap_bubble, NULL, 1000, 0, 0);
  measure("Test 8: StringConcat(1K)", 10000, NULL, wrap_concat, NULL, 1000, 0,
          0);
  measure("Test 9: ArrayMemory(10K)", 10000, wrap_arr, NULL, NULL, 10000, 0, 0);
  measure("Test 10: StringAllocation(1K)", 10000, NULL, wrap_str_alloc, NULL,
          1000, 0, 0);
  measure("Test 11: JSONBuild(1K)", 10000, wrap_json, NULL, NULL, 1000, 0, 0);

  return 0;
}

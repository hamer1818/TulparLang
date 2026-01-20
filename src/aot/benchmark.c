#include <stdio.h>

long long fib(long long n) {
  if (n < 2)
    return n;
  return fib(n - 1) + fib(n - 2);
}

int main() {
  printf("Starting Benchmark (C O3 Fib 40)...\n");
  long long res = fib(40);
  printf("Result: %lld\n", res);
  return 0;
}

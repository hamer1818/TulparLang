import os, sys
sys.setrecursionlimit(100000)
def fib(n):
    return n if n <= 1 else fib(n-1) + fib(n-2)
n = int(os.environ.get("BENCH_N") or 0) or 32
print(fib(n))

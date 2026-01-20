import time

def fib(n):
    if n < 2:
        return n
    return fib(n-1) + fib(n-2)

print("Starting Benchmark (Python Fib 40)...")
start = time.time()
res = fib(40)
end = time.time()
print(f"Result: {res}")
print(f"Time: {end - start}s")

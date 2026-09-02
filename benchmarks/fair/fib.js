function fib(n){ return n<=1?n:fib(n-1)+fib(n-2); }
const n = Number(process.env.BENCH_N) > 0 ? Number(process.env.BENCH_N) : 32;
console.log(String(fib(n)));

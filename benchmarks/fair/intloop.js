const n = Number(process.env.BENCH_N) > 0 ? Number(process.env.BENCH_N) : 50000000;
let t = 1;
for (let i = 0; i < n; i++) t = (t * 31 + i) % 1000000007;
console.log(String(t));

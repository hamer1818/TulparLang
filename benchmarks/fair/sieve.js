const n = Number(process.env.BENCH_N) > 0 ? Number(process.env.BENCH_N) : 5000000;
const f = new Int32Array(n + 1);
let c = 0;
for (let i = 2; i <= n; i++) { if (f[i] === 0) { c++; for (let k = i * i; k <= n; k += i) f[k] = 1; } }
console.log(String(c));

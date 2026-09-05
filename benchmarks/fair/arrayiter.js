const n = Number(process.env.BENCH_N) > 0 ? Number(process.env.BENCH_N) : 5000000;
const a = new Float64Array(n);
let t = 0;
for (let i = 0; i < a.length; i++) a[i] = i;
for (let i = 0; i < a.length; i++) t += a[i];
console.log(String(t));

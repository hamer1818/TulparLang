// N x N kayan nokta matris carpimi. Float64Array: JS'in ham cift dizisi.
const n = Math.max(1, parseInt(process.env.BENCH_N || "256", 10) || 256);
const a = new Float64Array(n * n);
const b = new Float64Array(n * n);
const c = new Float64Array(n * n);
for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) {
    a[i * n + j] = (i + j) % 10;
    b[i * n + j] = (i * j) % 7;
  }
for (let i = 0; i < n; i++)
  for (let k = 0; k < n; k++) {
    const av = a[i * n + k];
    for (let j = 0; j < n; j++) c[i * n + j] += av * b[k * n + j];
  }
let t = 0.0;
for (let i = 0; i < n * n; i++) t += c[i];
console.log(String(Math.trunc(t)));

function qs(a, lo, hi) {
  let i = lo, j = hi; const p = a[(lo + hi) >> 1];
  while (i <= j) {
    while (a[i] < p) i++;
    while (a[j] > p) j--;
    if (i <= j) { const t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
  }
  if (lo < j) qs(a, lo, j);
  if (i < hi) qs(a, i, hi);
}
let n = parseInt(process.env.BENCH_N || "0", 10); if (!(n > 0)) n = 1000000;
const a = new Int32Array(n);
let seed = 42;
for (let i = 0; i < n; i++) { seed = (seed * 48271) % 2147483647; a[i] = seed % 1000000; }
qs(a, 0, n - 1);
let bad = 0; for (let i = 1; i < n; i++) if (a[i - 1] > a[i]) bad++;
let cs = 0; for (let i = 0; i < n; i++) cs = (cs + a[i] * (i % 1000)) % 1000000007;
console.log(a[0] + " " + a[n >> 1] + " " + a[n - 1] + " " + cs + " " + bad);

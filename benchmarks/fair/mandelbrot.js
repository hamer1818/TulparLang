const n = Math.max(1, parseInt(process.env.BENCH_N || "2000", 10) || 2000);
const maxit = 50;
let total = 0;
for (let y = 0; y < n; y++) {
  const ci = (2.0 * y) / n - 1.0;
  for (let x = 0; x < n; x++) {
    const cr = (2.0 * x) / n - 1.5;
    let zr = 0.0, zi = 0.0, i = 0;
    while (i < maxit && zr * zr + zi * zi <= 4.0) {
      const t = zr * zr - zi * zi + cr;
      zi = 2.0 * zr * zi + ci;
      zr = t;
      i++;
    }
    total += i;
  }
}
console.log(String(total));

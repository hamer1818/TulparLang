let n = parseInt(process.env.BENCH_N || "0", 10); if (!(n > 0)) n = 200000;
const ps = new Array(n);
for (let i = 0; i < n; i++) ps[i] = { x: (i % 1000) * 0.5, y: (i % 777) * 0.25, vx: ((i % 13) - 6) * 0.1, vy: ((i % 7) - 3) * 0.2 };
const dt = 0.01, g = 9.81, w = 500.0;
for (let s = 0; s < 50; s++) {
  for (let i = 0; i < n; i++) {
    const p = ps[i];
    p.vy = p.vy - g * dt;
    p.x = p.x + p.vx * dt;
    p.y = p.y + p.vy * dt;
    if (p.x < 0.0 || p.x > w) p.vx = 0.0 - p.vx;
    if (p.y < 0.0) { p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9; }
  }
}
let sum = 0.0;
for (let i = 0; i < n; i++) sum = sum + ps[i].x + ps[i].y;
console.log(String(Math.round(sum * 1000.0)));

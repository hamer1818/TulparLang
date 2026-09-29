let n = parseInt(process.env.BENCH_N || "0", 10); if (!(n > 0)) n = 1000000;
const m = new Map();
for (let i = 0; i < n; i++) m.set("k" + i, i);
let s = 0;
for (let i = 0; i < n; i++) s += m.get("k" + ((i * 7) % n));
console.log(m.size + " " + s);

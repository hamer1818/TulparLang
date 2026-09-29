let n = parseInt(process.env.BENCH_N || "0", 10); if (!(n > 0)) n = 1000000;
const b = [];
for (let i = 0; i < n; i++) b.push(String((i * 7919) % 100000));
const s = b.join(",");
const parts = s.split(",");
let sum = 0;
for (let i = 0; i < parts.length; i++) sum += parseInt(parts[i], 10);
console.log(parts.length + " " + sum);

const n = Number(process.env.BENCH_N) > 0 ? Number(process.env.BENCH_N) : 2000000;
const parts = [];
for (let i = 0; i < n; i++) { parts.push(String(i % 1000)); parts.push(","); }
const s = parts.join("");
let cnt = 0;
for (let i = 0; i < s.length; i++) if (s.charCodeAt(i) === 44) cnt++;
console.log(s.length + " " + cnt);

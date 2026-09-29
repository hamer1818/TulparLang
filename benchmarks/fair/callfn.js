function f(x) { return (x * 31 + 7) % 1000003; }
function g(x) { return (x * 17 + 3) % 1000003; }
let n = parseInt(process.env.BENCH_N || "0", 10); if (!(n > 0)) n = 20000000;
const tab = [f, g];
let acc = 1;
for (let i = 0; i < n; i++) acc = tab[acc & 1](acc);
console.log(String(acc));

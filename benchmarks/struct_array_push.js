const n = 1_000_000;
const points = [];
for (let i = 0; i < n; i++) {
    points.push({ x: i, y: i * 2, z: i * 3 });
}
let total = 0;
for (const p of points) {
    total += p.x + p.y + p.z;
}
console.log(total);

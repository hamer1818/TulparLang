let acc = { x: 0, y: 0, z: 0 };
const n = 10_000_000;
for (let i = 0; i < n; i++) {
    acc.x += i;
    acc.y += i * 2;
    acc.z += i * 3;
}
console.log(acc.x + acc.y + acc.z);

function sieve(n) {
    const flags = new Uint8Array(n + 1);
    let count = 0;
    for (let i = 2; i <= n; i++) {
        if (!flags[i]) {
            count++;
            for (let k = i * i; k <= n; k += i) {
                flags[k] = 1;
            }
        }
    }
    return count;
}

console.log(sieve(100000));

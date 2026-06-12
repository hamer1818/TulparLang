// ============================================
// JavaScript (Node.js) PERFORMANS BENCHMARK
// Standard workload — same parameters and inner-loop averaging methodology
// as benchmark.c / benchmark.rs / benchmark.go.
// ============================================

// TEST 1: Fibonacci (Recursive)
function fibonacci(n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// TEST 2: Factorial (Recursive). BigInt to keep parity with i64 in C/Rust/Go.
function factorial(n) {
    let r = 1n;
    for (let i = 2n; i <= BigInt(n); i++) r *= i;
    return r;
}

// TEST 3: Ackermann (Deep Recursion)
function ackermann(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

// TEST 4: Tak (Takeuchi)
function tak(x, y, z) {
    if (y < x) {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    return z;
}

// TEST 5: Loop summation 0..N-1
function loopTest(iterations) {
    let sum = 0;
    for (let i = 0; i < iterations; i++) sum += i;
    return sum;
}

// TEST 6: Sieve of Eratosthenes
function sieve(n) {
    const flags = new Uint8Array(n + 1);
    let count = 0;
    for (let i = 2; i <= n; i++) {
        if (flags[i] === 0) {
            count++;
            for (let k = i * i; k <= n; k += i) flags[k] = 1;
        }
    }
    return count;
}

// TEST 7: Bubble Sort with checksum (deterministic via fixed seed)
function bubbleSort(size) {
    const arr = new Array(size);
    let seed = 12345;
    for (let i = 0; i < size; i++) {
        seed = (seed * 1103515245 + 12345) >>> 0;
        seed = seed % 32768;
        arr[i] = seed;
    }
    for (let i = 0; i < size; i++) {
        for (let j = 0; j < size - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                const t = arr[j]; arr[j] = arr[j + 1]; arr[j + 1] = t;
            }
        }
    }
    let checksum = 0;
    for (let i = 0; i < size; i++) checksum += arr[i];
    return checksum;
}

// TEST 8: String Concatenation (single char per iter)
function stringConcat(iterations) {
    let result = "";
    for (let i = 0; i < iterations; i++) result = result + "a";
    return result.length;
}

// TEST 9: Array Memory (push then sum)
function arrayMemory(size) {
    const arr = [];
    for (let i = 0; i < size; i++) arr.push(i);
    let sum = 0;
    for (let i = 0; i < size; i++) sum = sum + arr[i];
    return sum;
}

// TEST 10: String Allocation (concat "Merhaba Dunya " repeatedly)
function stringAllocation(iterations) {
    let result = "";
    for (let i = 0; i < iterations; i++) result = result + "Merhaba Dunya ";
    return result.length;
}

// TEST 11: JSON Build (objects with id/name/value/active)
function jsonBuildTest(size) {
    const data = [];
    for (let i = 0; i < size; i++) {
        data.push({ id: i, name: "Item", value: i * 10, active: true });
    }
    let sum = 0;
    for (let i = 0; i < size; i++) sum += data[i].value;
    return sum;
}

// Black hole to prevent dead-code elimination of return values.
let BLACK_HOLE = 0;

// Run `iterations` warmup + measured calls of `fn`, return avg ms per call.
function measure(label, iterations, fn) {
    const warmup = Math.max(1, Math.floor(iterations / 10));
    for (let i = 0; i < warmup; i++) BLACK_HOLE = fn();
    const start = performance.now();
    for (let i = 0; i < iterations; i++) BLACK_HOLE = fn();
    const elapsed = performance.now() - start;
    const avgMs = elapsed / iterations;
    console.log(label);
    if (avgMs < 1) {
        console.log(`Sure: ${(avgMs * 1000).toFixed(4)} us`);
    } else {
        console.log(`Sure: ${avgMs.toFixed(4)} ms`);
    }
    console.log("--------------------------------");
    return avgMs;
}

console.log("=======================================================");
console.log("         JavaScript (Node.js) PERFORMANS BENCHMARK");
console.log("=======================================================\n");

let total = 0;
total += measure("Test 1: Fibonacci(30)",     100,    () => fibonacci(30));
total += measure("Test 2: Factorial(20)",     100000, () => factorial(20));
total += measure("Test 3: Ackermann(3, 8)",   50,     () => ackermann(3, 8));
total += measure("Test 4: Tak(18, 12, 6)",    200,    () => tak(18, 12, 6));
total += measure("Test 5: Loop 1M",           1000,   () => loopTest(1000000));
total += measure("Test 6: Sieve(10K)",        5000,   () => sieve(10000));
total += measure("Test 7: BubbleSort(1K)",    200,    () => bubbleSort(1000));
total += measure("Test 8: StringConcat(1K)",  10000,  () => stringConcat(1000));
total += measure("Test 9: ArrayMemory(10K)",  10000,  () => arrayMemory(10000));
total += measure("Test 10: StringAlloc(1K)",  10000,  () => stringAllocation(1000));
total += measure("Test 11: JSONBuild(1K)",    10000,  () => jsonBuildTest(1000));

console.log("=======================================================");
console.log(`TOPLAM SURE: ${total.toFixed(3)} ms`);
console.log("=======================================================");
// Touch BLACK_HOLE to keep V8 from optimizing it away.
if (BLACK_HOLE === undefined) console.log("");

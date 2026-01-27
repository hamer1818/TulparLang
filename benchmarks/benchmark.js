// ============================================
// JavaScript (Node.js) PERFORMANS BENCHMARK TESTİ
// Tulpar ile uyumlu testler
// ============================================

// TEST 1: Fibonacci (Recursive)
function fibonacci(n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

// TEST 2: Factorial (Recursive)
function factorial(n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

// TEST 3: Ackermann (Deep Recursion)
function ackermann(m, n) {
    if (m === 0) return n + 1;
    if (n === 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}

// TEST 4: Tak (Takeuchi) - Heavy Recursion
function tak(x, y, z) {
    if (y < x) {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    return z;
}

// TEST 5: Loop Test
function loopTest(iterations) {
    let sum = 0;
    for (let i = 0; i < iterations; i++) {
        sum += i;
    }
    return sum;
}

// TEST 6: Sieve of Eratosthenes
function sieve(n) {
    const flags = new Array(n + 1).fill(0);
    let count = 0;
    for (let i = 2; i <= n; i++) {
        if (flags[i] === 0) {
            count++;
            for (let k = i * i; k <= n; k += i) {
                flags[k] = 1;
            }
        }
    }
    return count;
}

// TEST 7: Bubble Sort - with full checksum
function bubbleSort(size) {
    const arr = [];
    let seed = 12345;
    for (let i = 0; i < size; i++) {
        seed = (seed * 1103515245 + 12345) % 32768;
        arr.push(seed);
    }
    
    for (let i = 0; i < size; i++) {
        for (let j = 0; j < size - 1; j++) {
            if (arr[j] > arr[j + 1]) {
                [arr[j], arr[j + 1]] = [arr[j + 1], arr[j]];
            }
        }
    }
    
    // Compute checksum of ALL elements
    let checksum = 0;
    for (let i = 0; i < size; i++) {
        checksum += arr[i];
    }
    return checksum;
}

// TEST 8: String Concatenation
function stringConcat(iterations) {
    let result = "";
    for (let i = 0; i < iterations; i++) {
        result = result + "a";
    }
    return result.length;
}

// TEST 9: Array Memory
function arrayMemory(size) {
    const arr = [];
    for (let i = 0; i < size; i++) {
        arr.push(i);
    }
    let sum = 0;
    for (let i = 0; i < size; i++) {
        sum = sum + arr[i];
    }
    return sum;
}

// TEST 10: String Allocation (Heavy)
function stringAllocation(iterations) {
    let result = "";
    for (let i = 0; i < iterations; i++) {
        result = result + "Merhaba Dunya ";
    }
    return result.length;
}

// TEST 11: JSON Build Test
function jsonBuildTest(size) {
    const data = [];
    for (let i = 0; i < size; i++) {
        data.push({
            id: i,
            name: "Item",
            value: i * 10,
            active: true
        });
    }
    let sum = 0;
    for (let i = 0; i < size; i++) {
        sum += data[i].value;
    }
    return sum;
}

function printTime(ms) {
    if (ms < 1) {
        console.log(`  Sure: ${(ms * 1000).toFixed(2)} us`);
    } else {
        console.log(`  Sure: ${ms.toFixed(3)} ms`);
    }
}

function main() {
    console.log("=======================================================");
    console.log("         JavaScript (Node.js) PERFORMANS BENCHMARK");
    console.log("=======================================================\n");

    let totalTime = 0;

    // Test 1: Fibonacci
    console.log("Test 1: Fibonacci(30)");
    let start = performance.now();
    let fibResult = fibonacci(30);
    let end = performance.now();
    let elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${fibResult}`);
    printTime(elapsed);
    console.log();

    // Test 2: Factorial
    console.log("Test 2: Factorial(20)");
    start = performance.now();
    let factResult = factorial(20);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${factResult}`);
    printTime(elapsed);
    console.log();

    // Test 3: Ackermann
    console.log("Test 3: Ackermann(3, 8)");
    start = performance.now();
    let ackResult = ackermann(3, 8);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${ackResult}`);
    printTime(elapsed);
    console.log();

    // Test 4: Tak
    console.log("Test 4: Tak(18, 12, 6)");
    start = performance.now();
    let takResult = tak(18, 12, 6);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${takResult}`);
    printTime(elapsed);
    console.log();

    // Test 5: Loop
    console.log("Test 5: Loop 1M");
    start = performance.now();
    let loopResult = loopTest(1000000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${loopResult}`);
    printTime(elapsed);
    console.log();

    // Test 6: Sieve
    console.log("Test 6: Sieve(10000)");
    start = performance.now();
    let sieveResult = sieve(10000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${sieveResult}`);
    printTime(elapsed);
    console.log();

    // Test 7: Bubble Sort
    console.log("Test 7: BubbleSort(100)");
    start = performance.now();
    let bubbleResult = bubbleSort(100);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${bubbleResult}`);
    printTime(elapsed);
    console.log();

    // Test 8: String Concat
    console.log("Test 8: StringConcat(1000)");
    start = performance.now();
    let strResult = stringConcat(1000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${strResult}`);
    printTime(elapsed);
    console.log();

    // Test 9: Array Memory
    console.log("Test 9: ArrayMemory(10000)");
    start = performance.now();
    let arrResult = arrayMemory(10000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${arrResult}`);
    printTime(elapsed);
    console.log();

    // Test 10: String Allocation
    console.log("Test 10: StringAllocation(1000)");
    start = performance.now();
    let strAllocResult = stringAllocation(1000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${strAllocResult}`);
    printTime(elapsed);
    console.log();

    // Test 11: JSON Build
    console.log("Test 11: JSONBuild(1000)");
    start = performance.now();
    let jsonResult = jsonBuildTest(1000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${jsonResult}`);
    printTime(elapsed);
    console.log();

    console.log("=======================================================");
    console.log(`TOPLAM SURE: ${totalTime.toFixed(3)} ms`);
    console.log("=======================================================");
}

main();

// ============================================
// JavaScript (Node.js) PERFORMANS BENCHMARK TESTİ
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

// TEST 3: Loop Test
function loopTest(iterations) {
    let sum = 0;
    for (let i = 0; i < iterations; i++) {
        sum += i;
    }
    return sum;
}

// TEST 4: Nested Loop
function nestedLoop(size) {
    let count = 0;
    for (let i = 0; i < size; i++) {
        for (let j = 0; j < size; j++) {
            count++;
        }
    }
    return count;
}

// TEST 5: String Concatenation
function stringTest(iterations) {
    let result = "";
    for (let i = 0; i < iterations; i++) {
        result += "a";
    }
    return result.length;
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

    // Test 3: Loop
    console.log("Test 3: Loop 1.000.000 iterasyon");
    start = performance.now();
    let loopResult = loopTest(1000000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${loopResult}`);
    printTime(elapsed);
    console.log();

    // Test 4: Nested Loop
    console.log("Test 4: Nested Loop 1000x1000");
    start = performance.now();
    let nestedResult = nestedLoop(1000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${nestedResult}`);
    printTime(elapsed);
    console.log();

    // Test 5: String
    console.log("Test 5: String concatenation 100000");
    start = performance.now();
    let strResult = stringTest(100000);
    end = performance.now();
    elapsed = end - start;
    totalTime += elapsed;
    console.log(`  Sonuc: ${strResult}`);
    printTime(elapsed);
    console.log();

    console.log("=======================================================");
    console.log(`TOPLAM SURE: ${totalTime.toFixed(3)} ms`);
    console.log("=======================================================");
}

main();

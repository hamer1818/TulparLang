<?php
// ============================================
// PHP PERFORMANS BENCHMARK TESTİ
// ============================================

// TEST 1: Fibonacci (Recursive)
function fibonacci($n) {
    if ($n <= 1) return $n;
    return fibonacci($n - 1) + fibonacci($n - 2);
}

// TEST 2: Factorial (Recursive)
function factorial($n) {
    if ($n <= 1) return 1;
    return $n * factorial($n - 1);
}

// TEST 3: Loop Test
function loopTest($iterations) {
    $sum = 0;
    for ($i = 0; $i < $iterations; $i++) {
        $sum += $i;
    }
    return $sum;
}

// TEST 4: Nested Loop
function nestedLoop($size) {
    $count = 0;
    for ($i = 0; $i < $size; $i++) {
        for ($j = 0; $j < $size; $j++) {
            $count++;
        }
    }
    return $count;
}

// TEST 5: String Concatenation
function stringTest($iterations) {
    $result = "";
    for ($i = 0; $i < $iterations; $i++) {
        $result .= "a";
    }
    return strlen($result);
}

function getTimeMs() {
    return microtime(true) * 1000;
}

function printTime($ms) {
    if ($ms < 1) {
        echo "  Sure: " . number_format($ms * 1000, 2) . " us\n";
    } else {
        echo "  Sure: " . number_format($ms, 3) . " ms\n";
    }
}

echo "=======================================================\n";
echo "              PHP PERFORMANS BENCHMARK\n";
echo "=======================================================\n\n";

$totalTime = 0;

// Test 1: Fibonacci
echo "Test 1: Fibonacci(30)\n";
$start = getTimeMs();
$fibResult = fibonacci(30);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $fibResult\n";
printTime($elapsed);
echo "\n";

// Test 2: Factorial
echo "Test 2: Factorial(20)\n";
$start = getTimeMs();
$factResult = factorial(20);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $factResult\n";
printTime($elapsed);
echo "\n";

// Test 3: Loop
echo "Test 3: Loop 1.000.000 iterasyon\n";
$start = getTimeMs();
$loopResult = loopTest(1000000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $loopResult\n";
printTime($elapsed);
echo "\n";

// Test 4: Nested Loop
echo "Test 4: Nested Loop 1000x1000\n";
$start = getTimeMs();
$nestedResult = nestedLoop(1000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $nestedResult\n";
printTime($elapsed);
echo "\n";

// Test 5: String
echo "Test 5: String concatenation 100000\n";
$start = getTimeMs();
$strResult = stringTest(100000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $strResult\n";
printTime($elapsed);
echo "\n";

echo "=======================================================\n";
echo "TOPLAM SURE: " . number_format($totalTime, 3) . " ms\n";
echo "=======================================================\n";
?>

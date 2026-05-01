<?php
// ============================================
// PHP PERFORMANS BENCHMARK
// Standard workload — same parameters and inner-loop averaging methodology
// as benchmark.c / benchmark.rs / benchmark.go.
// ============================================

function fibonacci($n) {
    if ($n <= 1) return $n;
    return fibonacci($n - 1) + fibonacci($n - 2);
}

function factorial($n) {
    if ($n <= 1) return 1;
    return $n * factorial($n - 1);
}

function ackermann($m, $n) {
    if ($m == 0) return $n + 1;
    if ($n == 0) return ackermann($m - 1, 1);
    return ackermann($m - 1, ackermann($m, $n - 1));
}

function tak($x, $y, $z) {
    if ($y < $x) {
        return tak(tak($x - 1, $y, $z), tak($y - 1, $z, $x), tak($z - 1, $x, $y));
    }
    return $z;
}

function loopTest($iterations) {
    $sum = 0;
    for ($i = 0; $i < $iterations; $i++) $sum += $i;
    return $sum;
}

function sieve($n) {
    $flags = array_fill(0, $n + 1, 0);
    $count = 0;
    for ($i = 2; $i <= $n; $i++) {
        if ($flags[$i] == 0) {
            $count++;
            for ($k = $i * $i; $k <= $n; $k += $i) $flags[$k] = 1;
        }
    }
    return $count;
}

function bubbleSort($size) {
    $arr = [];
    $seed = 12345;
    for ($i = 0; $i < $size; $i++) {
        $seed = ($seed * 1103515245 + 12345) % 32768;
        $arr[] = $seed;
    }
    for ($i = 0; $i < $size; $i++) {
        for ($j = 0; $j < $size - 1; $j++) {
            if ($arr[$j] > $arr[$j + 1]) {
                $t = $arr[$j]; $arr[$j] = $arr[$j + 1]; $arr[$j + 1] = $t;
            }
        }
    }
    $checksum = 0;
    for ($i = 0; $i < $size; $i++) $checksum += $arr[$i];
    return $checksum;
}

function stringConcat($iterations) {
    $result = "";
    for ($i = 0; $i < $iterations; $i++) $result = $result . "a";
    return strlen($result);
}

function arrayMemory($size) {
    $arr = [];
    for ($i = 0; $i < $size; $i++) $arr[] = $i;
    $sum = 0;
    for ($i = 0; $i < $size; $i++) $sum = $sum + $arr[$i];
    return $sum;
}

function stringAllocation($iterations) {
    $result = "";
    for ($i = 0; $i < $iterations; $i++) $result = $result . "Merhaba Dunya ";
    return strlen($result);
}

function jsonBuildTest($size) {
    $data = [];
    for ($i = 0; $i < $size; $i++) {
        $data[] = ['id' => $i, 'name' => 'Item', 'value' => $i * 10, 'active' => true];
    }
    $sum = 0;
    for ($i = 0; $i < $size; $i++) $sum += $data[$i]['value'];
    return $sum;
}

$BLACK_HOLE = 0;

// Run $iterations warmup + measured calls, report avg ms per call.
function measure($label, $iterations, $fn) {
    global $BLACK_HOLE;
    $warmup = max(1, intdiv($iterations, 10));
    for ($i = 0; $i < $warmup; $i++) $BLACK_HOLE = $fn();
    $start = microtime(true);
    for ($i = 0; $i < $iterations; $i++) $BLACK_HOLE = $fn();
    $elapsed_ms = (microtime(true) - $start) * 1000.0;
    $avg_ms = $elapsed_ms / $iterations;
    echo $label . "\n";
    if ($avg_ms < 1.0) {
        echo "Sure: " . number_format($avg_ms * 1000.0, 4, '.', '') . " us\n";
    } else {
        echo "Sure: " . number_format($avg_ms,           4, '.', '') . " ms\n";
    }
    echo "--------------------------------\n";
    return $avg_ms;
}

echo "=======================================================\n";
echo "              PHP PERFORMANS BENCHMARK\n";
echo "=======================================================\n\n";

$total = 0.0;
$total += measure("Test 1: Fibonacci(30)",      100,    function() { return fibonacci(30); });
$total += measure("Test 2: Factorial(20)",      100000, function() { return factorial(20); });
$total += measure("Test 3: Ackermann(3, 8)",    50,     function() { return ackermann(3, 8); });
$total += measure("Test 4: Tak(18, 12, 6)",     200,    function() { return tak(18, 12, 6); });
$total += measure("Test 5: Loop 1M",            1000,   function() { return loopTest(1000000); });
$total += measure("Test 6: Sieve(10K)",         500,    function() { return sieve(10000); });
$total += measure("Test 7: BubbleSort(1K)",     20,     function() { return bubbleSort(1000); });
$total += measure("Test 8: StringConcat(1K)",   2000,   function() { return stringConcat(1000); });
$total += measure("Test 9: ArrayMemory(10K)",   1000,   function() { return arrayMemory(10000); });
$total += measure("Test 10: StringAlloc(1K)",   1000,   function() { return stringAllocation(1000); });
$total += measure("Test 11: JSONBuild(1K)",     500,    function() { return jsonBuildTest(1000); });

echo "=======================================================\n";
echo "TOPLAM SURE: " . number_format($total, 3, '.', '') . " ms\n";
echo "=======================================================\n";
?>

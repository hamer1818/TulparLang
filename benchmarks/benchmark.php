<?php
// ============================================
// PHP PERFORMANS BENCHMARK TESTİ
// Tulpar ile uyumlu testler
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

// TEST 3: Ackermann (Deep Recursion)
function ackermann($m, $n) {
    if ($m == 0) return $n + 1;
    if ($n == 0) return ackermann($m - 1, 1);
    return ackermann($m - 1, ackermann($m, $n - 1));
}

// TEST 4: Tak (Takeuchi) - Heavy Recursion
function tak($x, $y, $z) {
    if ($y < $x) {
        return tak(tak($x - 1, $y, $z), tak($y - 1, $z, $x), tak($z - 1, $x, $y));
    }
    return $z;
}

// TEST 5: Loop Test
function loopTest($iterations) {
    $sum = 0;
    for ($i = 0; $i < $iterations; $i++) {
        $sum += $i;
    }
    return $sum;
}

// TEST 6: Sieve of Eratosthenes
function sieve($n) {
    $flags = array_fill(0, $n + 1, 0);
    $count = 0;
    for ($i = 2; $i <= $n; $i++) {
        if ($flags[$i] == 0) {
            $count++;
            for ($k = $i * $i; $k <= $n; $k += $i) {
                $flags[$k] = 1;
            }
        }
    }
    return $count;
}

// TEST 7: Bubble Sort - with full checksum
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
                $temp = $arr[$j];
                $arr[$j] = $arr[$j + 1];
                $arr[$j + 1] = $temp;
            }
        }
    }
    
    // Compute checksum of ALL elements
    $checksum = 0;
    for ($i = 0; $i < $size; $i++) {
        $checksum += $arr[$i];
    }
    return $checksum;
}

// TEST 8: String Concatenation
function stringConcat($iterations) {
    $result = "";
    for ($i = 0; $i < $iterations; $i++) {
        $result = $result . "a";
    }
    return strlen($result);
}

// TEST 9: Array Memory
function arrayMemory($size) {
    $arr = [];
    for ($i = 0; $i < $size; $i++) {
        $arr[] = $i;
    }
    $sum = 0;
    for ($i = 0; $i < $size; $i++) {
        $sum = $sum + $arr[$i];
    }
    return $sum;
}

// TEST 10: String Allocation (Heavy)
function stringAllocation($iterations) {
    $result = "";
    for ($i = 0; $i < $iterations; $i++) {
        $result = $result . "Merhaba Dunya ";
    }
    return strlen($result);
}

// TEST 11: JSON Build Test
function jsonBuildTest($size) {
    $data = [];
    for ($i = 0; $i < $size; $i++) {
        $data[] = [
            'id' => $i,
            'name' => 'Item',
            'value' => $i * 10,
            'active' => true
        ];
    }
    $sum = 0;
    for ($i = 0; $i < $size; $i++) {
        $sum += $data[$i]['value'];
    }
    return $sum;
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

// Test 3: Ackermann
echo "Test 3: Ackermann(3, 8)\n";
$start = getTimeMs();
$ackResult = ackermann(3, 8);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $ackResult\n";
printTime($elapsed);
echo "\n";

// Test 4: Tak
echo "Test 4: Tak(18, 12, 6)\n";
$start = getTimeMs();
$takResult = tak(18, 12, 6);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $takResult\n";
printTime($elapsed);
echo "\n";

// Test 5: Loop
echo "Test 5: Loop 1M\n";
$start = getTimeMs();
$loopResult = loopTest(1000000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $loopResult\n";
printTime($elapsed);
echo "\n";

// Test 6: Sieve
echo "Test 6: Sieve(10000)\n";
$start = getTimeMs();
$sieveResult = sieve(10000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $sieveResult\n";
printTime($elapsed);
echo "\n";

// Test 7: Bubble Sort
echo "Test 7: BubbleSort(100)\n";
$start = getTimeMs();
$bubbleResult = bubbleSort(100);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $bubbleResult\n";
printTime($elapsed);
echo "\n";

// Test 8: String Concat
echo "Test 8: StringConcat(1000)\n";
$start = getTimeMs();
$strResult = stringConcat(1000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $strResult\n";
printTime($elapsed);
echo "\n";

// Test 9: Array Memory
echo "Test 9: ArrayMemory(10000)\n";
$start = getTimeMs();
$arrResult = arrayMemory(10000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $arrResult\n";
printTime($elapsed);
echo "\n";

// Test 10: String Allocation
echo "Test 10: StringAllocation(1000)\n";
$start = getTimeMs();
$strAllocResult = stringAllocation(1000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $strAllocResult\n";
printTime($elapsed);
echo "\n";

// Test 11: JSON Build
echo "Test 11: JSONBuild(1000)\n";
$start = getTimeMs();
$jsonResult = jsonBuildTest(1000);
$end = getTimeMs();
$elapsed = $end - $start;
$totalTime += $elapsed;
echo "  Sonuc: $jsonResult\n";
printTime($elapsed);
echo "\n";

echo "=======================================================\n";
echo "TOPLAM SURE: " . number_format($totalTime, 3) . " ms\n";
echo "=======================================================\n";
?>

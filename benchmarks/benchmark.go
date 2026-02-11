package main

import (
	"fmt"
	"time"
)

// Global black hole to prevent optimization
var BlackHole interface{}

// Global volatile input (set dynamically)
var V_INT = 1

// Measure with loops
func measure(name string, iterations int, f func() interface{}) {
	// Warmup
	for i := 0; i < iterations/10; i++ {
		BlackHole = f()
	}

	start := time.Now()
	for i := 0; i < iterations; i++ {
		BlackHole = f()
	}
	elapsed := time.Since(start)

	// Average time
	totalNs := elapsed.Nanoseconds()
	avgNs := float64(totalNs) / float64(iterations)
	avgUs := avgNs / 1000.0
	avgMs := avgUs / 1000.0

	// Report time per ONE operation (avg)
	if avgUs < 1.0 {
		fmt.Printf("%s\nSure: %.4f ns\n", name, avgNs)
		fmt.Printf("Sure: %.6f µs\n", avgUs) 
	} else if avgMs < 1.0 {
		fmt.Printf("%s\nSure: %.4f µs\n", name, avgUs)
	} else {
		fmt.Printf("%s\nSure: %.4f ms\n", name, avgMs)
	}
	fmt.Println("--------------------------------")
}

func main() {
	// Initialize V_INT dynamically based on time to ensure it's not a compile-time constant
	if time.Now().Year() > 2000 {
		V_INT = 1
	} else {
		V_INT = 2
	}
	
	fmt.Println("Go Benchmark Basliyor...")
	fmt.Printf("Volatile Input Scale: %d\n", V_INT)
	fmt.Println("--------------------------------")

	v30 := 30 * V_INT
	measure("Test 1: Fibonacci(30)", 100, func() interface{} {
		return fibonacci(v30)
	})

	v20 := int64(20 * V_INT)
	measure("Test 2: Factorial(20)", 100000, func() interface{} {
		return factorial(v20)
	})

	v3 := 3 * V_INT
	v8 := 8 * V_INT
	measure("Test 3: Ackermann(3, 8)", 50, func() interface{} {
		return ackermann(v3, v8)
	})

	v18 := 18 * V_INT
	v12 := 12 * V_INT
	v6 := 6 * V_INT
	measure("Test 4: Tak(18, 12, 6)", 2000, func() interface{} {
		return tak(v18, v12, v6)
	})

	v1M := 1000000 * V_INT
	measure("Test 5: Loop 1M", 10000, func() interface{} {
		return loopTest(v1M)
	})

	v10K := 10000 * V_INT
	measure("Test 6: Sieve(10K)", 50000, func() interface{} {
		return sieve(v10K)
	})

	v1K := 1000 * V_INT
	measure("Test 7: BubbleSort(1K)", 20000, func() interface{} {
		return bubbleSort(v1K)
	})

	measure("Test 8: StringConcat(1K)", 100000, func() interface{} {
		return stringConcat(v1K)
	})

	measure("Test 9: ArrayMemory(10K)", 100000, func() interface{} {
		return arrayMemory(v10K)
	})

	measure("Test 10: StringAlloc(1K)", 10000, func() interface{} {
		return stringAlloc(v1K)
	})

	measure("Test 11: JSONBuild(1K)", 10000, func() interface{} {
		return jsonBuild(v1K)
	})
	
	// Force usage of BlackHole
	if BlackHole == nil {
		fmt.Print("")
	}
}

// 1. Fibonacci
func fibonacci(n int) int {
	if n <= 1 {
		return n
	}
	return fibonacci(n-1) + fibonacci(n-2)
}

// 2. Factorial
func factorial(n int64) int64 {
	if n <= 1 {
		return 1
	}
	return n * factorial(n-1)
}

// 3. Ackermann
func ackermann(m, n int) int {
	if m == 0 {
		return n + 1
	}
	if n == 0 {
		return ackermann(m-1, 1)
	}
	return ackermann(m-1, ackermann(m, n-1))
}

// 4. Tak
func tak(x, y, z int) int {
	if y < x {
		return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y))
	}
	return z
}

// 5. Loop used for summation (Loop 1M)
func loopTest(iterations int) int64 {
	var sum int64 = 0
	inc := int64(V_INT)
	if inc == 0 { inc = 1 } // Ensure non-zero
	for i := 0; i < iterations; i++ {
		// Xorshift mix
		x := int64(i)
		x ^= x << 13
		x ^= x >> 17
		x ^= x << 5
		sum += x + inc
	}
	return sum
}

// 6. Sieve
func sieve(n int) int {
	flags := make([]bool, n+1)
	count := 0
	for i := 2; i <= n; i++ {
		if !flags[i] {
			count++
			for k := i * i; k <= n; k += i {
				flags[k] = true
			}
		}
	}
	return count
}

// 7. Bubble Sort
func bubbleSort(size int) int64 {
	arr := make([]int, size)
	// Use runtime V_INT to prevent compile-time generation
	seed := 12345 * V_INT 
	for i := 0; i < size; i++ {
		seed = (seed * 1103515245 + 12345) % 32768
		arr[i] = seed
	}

	vZero := V_INT ^ 1 // 0
	for i := 0; i < size; i++ {
		for j := 0; j < size-1; j++ {
			// runtime check
			if arr[j] > arr[j+1] + vZero {
				arr[j], arr[j+1] = arr[j+1], arr[j]
			}
		}
	}

	var sum int64 = 0
	for _, v := range arr {
		sum += int64(v)
	}
	return sum
}

// 8. String Concat
func stringConcat(iterations int) int {
	result := ""
	for i := 0; i < iterations; i++ {
		result = result + "a"
	}
	return len(result)
}

// 9. Array Memory
func arrayMemory(size int) int {
	arr := make([]int, size)
	for i := 0; i < size; i++ {
		arr[i] = i
	}
	sum := 0
	for i := 0; i < size; i++ {
		sum = sum + arr[i]
	}
	return sum
}

// 10. String Alloc
func stringAlloc(iterations int) int {
	result := ""
	for i := 0; i < iterations; i++ {
		result = result + "Merhaba Dunya "
	}
	return len(result)
}

// 11. JSON Build (simulated using map[string]interface{})
type JSONItem struct {
	ID     int
	Name   string
	Value  int
	Active bool
}

func jsonBuild(size int) int {
	data := make([]JSONItem, size)
	for i := 0; i < size; i++ {
		data[i] = JSONItem{
			ID:     i,
			Name:   "Item",
			Value:  i * 10,
			Active: true,
		}
	}
	sum := 0
	for i := 0; i < size; i++ {
		sum += data[i].Value
	}
	return sum
}

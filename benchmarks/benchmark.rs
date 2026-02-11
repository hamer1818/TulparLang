use std::time::Instant;
use std::fmt::Write;

// Helper to prevent optimization
// Helper to prevent optimization
// This ensures that the compiler treats the value as used and unknown
fn black_box<T>(dummy: T) -> T {
    unsafe {
        let ret = std::ptr::read_volatile(&dummy);
        std::mem::forget(dummy);
        ret
    }
}

// Measure with loops for accuracy
fn measure<F>(name: &str, iterations: u32, f: F)
where
    F: Fn(),
{
    // Warmup
    for _ in 0..iterations / 10 {
        f();
    }

    let start = Instant::now();
    for _ in 0..iterations {
        f();
    }
    let duration = start.elapsed();
    
    // Average time per iteration
    let avg_ns = duration.as_nanos() as f64 / iterations as f64;
    let avg_us = avg_ns / 1000.0;
    let avg_ms = avg_us / 1000.0;

    println!("{}", name);
    if avg_us < 1.0 {
        println!("Sure: {:.4} ns", avg_ns); 
        println!("Sure: {:.6} µs", avg_us); 
    } else if avg_ms < 1.0 {
        println!("Sure: {:.4} ms", avg_ms);
        println!("Sure: {:.4} µs", avg_us);
    } else {
        println!("Sure: {:.4} ms", avg_ms);
    }
    println!("--------------------------------");
}

fn main() {
    println!("Rust Benchmark Basliyor...");
    println!("--------------------------------");

    // Arguments passed through black_box are treated as runtime values
    measure("Test 1: Fibonacci(30)", 100, || {
        black_box(fibonacci(black_box(30)));
    });

    measure("Test 2: Factorial(20)", 100000, || {
        black_box(factorial(black_box(20)));
    });

    measure("Test 3: Ackermann(3, 8)", 50, || {
        black_box(ackermann(black_box(3), black_box(8)));
    });

    measure("Test 4: Tak(18, 12, 6)", 200, || {
        black_box(tak(black_box(18), black_box(12), black_box(6)));
    });

    measure("Test 5: Loop 1M", 1000, || {
        black_box(loop_test(black_box(1000000)));
    });

    measure("Test 6: Sieve(10K)", 5000, || {
        black_box(sieve(black_box(10000)));
    });

    measure("Test 7: BubbleSort(1K)", 2000, || {
        black_box(bubble_sort(black_box(1000)));
    });

    measure("Test 8: StringConcat(1K)", 10000, || {
        black_box(string_concat(black_box(1000)));
    });

    measure("Test 9: ArrayMemory(10K)", 10000, || {
        black_box(array_memory(black_box(10000)));
    });

    measure("Test 10: StringAlloc(1K)", 10000, || {
        black_box(string_alloc(black_box(1000)));
    });

    measure("Test 11: JSONBuild(1K)", 10000, || {
        black_box(json_build(black_box(1000)));
    });
}

// 1. Fibonacci
fn fibonacci(n: i32) -> i32 {
    if n <= 1 {
        return n;
    }
    fibonacci(n - 1) + fibonacci(n - 2)
}

// 2. Factorial
fn factorial(n: i64) -> i64 {
    if n <= 1 {
        return 1;
    }
    n * factorial(n - 1)
}

// 3. Ackermann
fn ackermann(m: i32, n: i32) -> i32 {
    if m == 0 {
        return n + 1;
    }
    if n == 0 {
        return ackermann(m - 1, 1);
    }
    ackermann(m - 1, ackermann(m, n - 1))
}

// 4. Tak
fn tak(x: i32, y: i32, z: i32) -> i32 {
    if y < x {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    z
}

// 5. Loop 1M
fn loop_test(iterations: i64) -> i64 {
    let mut sum: i64 = 0;
    let inc = black_box(1); // Force runtime dependency
    for i in 0..iterations {
        // Xorshift mix
        let mut x = i;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        sum = sum.wrapping_add(x).wrapping_add(inc);
    }
    sum
}

// 6. Sieve
fn sieve(n: usize) -> usize {
    let mut flags = vec![false; n + 1];
    let mut count = 0;
    for i in 2..=n {
        if !flags[i] {
            count += 1;
            let mut k = i * i;
            while k <= n {
                flags[k] = true;
                k += i;
            }
        }
    }
    count
}

// 7. Bubble Sort
fn bubble_sort(size: usize) -> i64 {
    let mut arr = vec![0; size];
    // Use runtime black_box to prevent compile-time generation
    let mut seed: i64 = 12345 * black_box(1); 
    for i in 0..size {
        seed = (seed * 1103515245 + 12345) % 32768;
        arr[i] = seed;
    }

    let v_zero = black_box(0);
    for _ in 0..size {
        for j in 0..size - 1 {
            // Force runtime comparison
            if arr[j] > arr[j + 1] + v_zero {
                arr.swap(j, j + 1);
            }
        }
    }
    
    arr.iter().sum()
}

// 8. String Concat
fn string_concat(iterations: usize) -> usize {
    let mut result = String::new();
    for _ in 0..iterations {
        result.push_str("a"); // or push('a')
    }
    result.len()
}

// 9. Array Memory
fn array_memory(size: usize) -> usize {
    let mut arr = Vec::with_capacity(size);
    for i in 0..size {
        arr.push(i);
    }
    let mut sum = 0;
    for i in 0..size {
        sum += arr[i];
    }
    sum
}

// 10. String Alloc
fn string_alloc(iterations: usize) -> usize {
    let mut result = String::new();
    for _ in 0..iterations {
        result.push_str("Merhaba Dunya ");
    }
    result.len()
}

// 11. JSON Build (Struct emulation)
struct JsonItem {
    id: usize,
    name: String,
    value: usize,
    active: bool,
}

fn json_build(size: usize) -> usize {
    let mut data = Vec::with_capacity(size);
    for i in 0..size {
        data.push(JsonItem {
            id: i,
            name: "Item".to_string(),
            value: i * 10,
            active: true,
        });
    }
    
    let mut sum = 0;
    for item in &data {
        sum += item.value;
    }
    sum
}

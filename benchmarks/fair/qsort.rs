fn qs(a: &mut [i32], lo: i64, hi: i64) {
    let (mut i, mut j) = (lo, hi);
    let p = a[((lo + hi) / 2) as usize];
    while i <= j {
        while a[i as usize] < p { i += 1; }
        while a[j as usize] > p { j -= 1; }
        if i <= j { a.swap(i as usize, j as usize); i += 1; j -= 1; }
    }
    if lo < j { qs(a, lo, j); }
    if i < hi { qs(a, i, hi); }
}
fn main() {
    let n: i64 = std::env::var("BENCH_N").ok().and_then(|v| v.parse().ok()).filter(|&v| v > 0).unwrap_or(1000000);
    let mut a = vec![0i32; n as usize];
    let mut seed: i64 = 42;
    for i in 0..n as usize { seed = (seed * 48271) % 2147483647; a[i] = (seed % 1000000) as i32; }
    qs(&mut a, 0, n - 1);
    let mut bad = 0; for i in 1..n as usize { if a[i - 1] > a[i] { bad += 1; } }
    let mut cs: i64 = 0; for i in 0..n as usize { cs = (cs + a[i] as i64 * (i as i64 % 1000)) % 1000000007; }
    println!("{} {} {} {} {}", a[0], a[(n / 2) as usize], a[(n - 1) as usize], cs, bad);
}

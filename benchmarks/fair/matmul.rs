// N x N kayan nokta matris carpimi (matmul.c ile ayni algoritma + veri yapisi).
fn main() {
    let n: usize = std::env::var("BENCH_N").ok()
        .and_then(|v| v.parse().ok()).filter(|&x| x > 0).unwrap_or(256);
    let mut a = vec![0f64; n * n];
    let mut b = vec![0f64; n * n];
    let mut c = vec![0f64; n * n];
    for i in 0..n {
        for j in 0..n {
            a[i * n + j] = ((i + j) % 10) as f64;
            b[i * n + j] = ((i * j) % 7) as f64;
        }
    }
    for i in 0..n {
        for k in 0..n {
            let av = a[i * n + k];
            for j in 0..n { c[i * n + j] += av * b[k * n + j]; }
        }
    }
    let t: f64 = c.iter().sum();
    println!("{}", t as i64);
}

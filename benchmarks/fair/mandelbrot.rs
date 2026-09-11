fn main() {
    let n: i64 = std::env::var("BENCH_N").ok()
        .and_then(|v| v.parse().ok()).filter(|&x: &i64| x > 0).unwrap_or(2000);
    let maxit = 50;
    let mut total: i64 = 0;
    for y in 0..n {
        let ci = 2.0 * y as f64 / n as f64 - 1.0;
        for x in 0..n {
            let cr = 2.0 * x as f64 / n as f64 - 1.5;
            let (mut zr, mut zi) = (0.0f64, 0.0f64);
            let mut i = 0;
            while i < maxit && zr * zr + zi * zi <= 4.0 {
                let t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                i += 1;
            }
            total += i;
        }
    }
    println!("{}", total);
}

use std::fmt::Write;
fn main() {
    let n: i64 = std::env::var("BENCH_N").ok().and_then(|v| v.parse().ok()).filter(|&v| v > 0).unwrap_or(1000000);
    let mut s = String::new();
    for i in 0..n { if i > 0 { s.push(','); } write!(s, "{}", (i * 7919) % 100000).unwrap(); }
    let mut cnt = 0i64; let mut sum = 0i64;
    for p in s.split(',') { sum += p.parse::<i64>().unwrap(); cnt += 1; }
    println!("{} {}", cnt, sum);
}

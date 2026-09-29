fn f(x: i64) -> i64 { (x * 31 + 7) % 1000003 }
fn g(x: i64) -> i64 { (x * 17 + 3) % 1000003 }
fn main() {
    let n: i64 = std::env::var("BENCH_N").ok().and_then(|v| v.parse().ok()).filter(|&v| v > 0).unwrap_or(20000000);
    // black_box: tablonun icerigini derleyiciden gizle, yoksa LLVM dolayli
    // cagriyi iki dogrudan cagriya cevirip satir ici alir (C'de global dizi).
    let tab: [fn(i64) -> i64; 2] = std::hint::black_box([f, g]);
    let mut acc: i64 = 1;
    for _ in 0..n { acc = tab[(acc & 1) as usize](acc); }
    println!("{}", acc);
}

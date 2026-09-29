use std::collections::HashMap;
use std::fmt::Write;
fn main() {
    let n: i64 = std::env::var("BENCH_N").ok().and_then(|v| v.parse().ok()).filter(|&v| v > 0).unwrap_or(1000000);
    let mut m: HashMap<String, i64> = HashMap::new();
    let mut k = String::new();
    for i in 0..n { k.clear(); write!(k, "k{}", i).unwrap(); m.insert(k.clone(), i); }
    let mut s: i64 = 0;
    for i in 0..n { k.clear(); write!(k, "k{}", (i * 7) % n).unwrap(); s += m[&k]; }
    println!("{} {}", m.len(), s);
}

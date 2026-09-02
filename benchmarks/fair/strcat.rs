fn main(){
    let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(2000000);
    let mut b=String::new();
    for i in 0..n { b.push_str(&(i%1000).to_string()); b.push(','); }
    let cnt=b.bytes().filter(|&c|c==b',').count();
    println!("{} {}",b.len(),cnt);
}

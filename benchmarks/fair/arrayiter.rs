fn main(){
    let n:usize=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(5000000);
    let mut a=vec![0i64;n]; let mut t:i64=0;
    for i in 0..a.len() { a[i]=i as i64; }
    for i in 0..a.len() { t+=a[i]; }
    println!("{}",t);
}

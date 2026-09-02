fn main(){
    let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(50000000);
    let mut t:i64=1; let mut i:i64=0;
    while i<n { t=(t*31+i)%1000000007; i+=1; }
    println!("{}",t);
}

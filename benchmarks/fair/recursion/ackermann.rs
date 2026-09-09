fn ack(m:i64,n:i64)->i64{ if m==0 {n+1} else if n==0 {ack(m-1,1)} else {ack(m-1,ack(m,n-1))} }
fn main(){let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(9);println!("{}",ack(3,n));}

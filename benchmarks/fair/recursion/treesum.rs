fn tsum(node:i64,d:i64)->i64{ if d==0 {node} else {node+tsum(node*2,d-1)+tsum(node*2+1,d-1)} }
fn main(){let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(24);println!("{}",tsum(1,n));}

fn tak(x:i64,y:i64,z:i64)->i64{ if y>=x {z} else {tak(tak(x-1,y,z),tak(y-1,z,x),tak(z-1,x,y))} }
fn main(){let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(18);println!("{}",tak(n,n/2,0));}

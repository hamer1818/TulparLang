fn even_(n:i64)->i64{ if n==0 {1} else {odd_(n-1)} }
fn odd_(n:i64)->i64{ if n==0 {0} else {even_(n-1)} }
fn main(){let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(20000);
    let mut s=0i64; for i in 0..n { s+=even_(i%997); } println!("{}",s);}

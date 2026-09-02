fn fib(n:i64)->i64{ if n<=1 {n} else {fib(n-1)+fib(n-2)} }
fn main(){
    let n:i64=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(32);
    println!("{}",fib(n));
}

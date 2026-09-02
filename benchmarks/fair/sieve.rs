fn main(){
    let n:usize=std::env::var("BENCH_N").ok().and_then(|v|v.parse().ok()).filter(|&v|v>0).unwrap_or(5000000);
    let mut f=vec![0i32;n+1]; let mut c:i64=0;
    let mut i=2usize;
    while i<=n { if f[i]==0 { c+=1; let mut k=i*i; while k<=n { f[k]=1; k+=i; } } i+=1; }
    println!("{}",c);
}

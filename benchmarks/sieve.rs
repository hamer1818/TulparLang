fn sieve(n: usize) -> usize {
    let mut flags = vec![false; n + 1];
    let mut count = 0;
    for i in 2..=n {
        if !flags[i] {
            count += 1;
            let mut k = i * i;
            while k <= n {
                flags[k] = true;
                k += i;
            }
        }
    }
    count
}

fn main() {
    println!("{}", sieve(100_000));
}

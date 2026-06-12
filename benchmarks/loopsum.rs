fn main() {
    let mut total: i64 = 0;
    let mut i: i64 = 0;
    while i < 10_000_000 {
        total += i;
        i += 1;
    }
    println!("{}", total);
}

struct V3 {
    x: i64,
    y: i64,
    z: i64,
}

fn main() {
    let mut acc = V3 { x: 0, y: 0, z: 0 };
    let n: i64 = 10_000_000;
    for i in 0..n {
        acc.x += i;
        acc.y += i * 2;
        acc.z += i * 3;
    }
    println!("{}", acc.x + acc.y + acc.z);
}

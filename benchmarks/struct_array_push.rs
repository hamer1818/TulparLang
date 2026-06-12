struct V3 {
    x: i64,
    y: i64,
    z: i64,
}

fn main() {
    let n = 1_000_000;
    let mut points = Vec::with_capacity(n);
    for i in 0..n as i64 {
        points.push(V3 { x: i, y: i * 2, z: i * 3 });
    }
    let mut total: i64 = 0;
    for p in &points {
        total += p.x + p.y + p.z;
    }
    println!("{}", total);
}

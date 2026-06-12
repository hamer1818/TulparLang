fn tak(x: i32, y: i32, z: i32) -> i32 {
    if y < x {
        return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    }
    z
}

fn main() {
    println!("{}", tak(18, 12, 6));
}

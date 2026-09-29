#[derive(Clone, Copy)]
struct P { x: f64, y: f64, vx: f64, vy: f64 }
fn main() {
    let n: usize = std::env::var("BENCH_N").ok().and_then(|v| v.parse().ok()).filter(|&v| v > 0).unwrap_or(200000);
    let mut ps: Vec<P> = (0..n).map(|i| P { x: (i % 1000) as f64 * 0.5, y: (i % 777) as f64 * 0.25,
        vx: ((i % 13) as i64 - 6) as f64 * 0.1, vy: ((i % 7) as i64 - 3) as f64 * 0.2 }).collect();
    let (dt, g, w) = (0.01f64, 9.81f64, 500.0f64);
    for _ in 0..50 {
        for p in ps.iter_mut() {
            p.vy = p.vy - g * dt;
            p.x = p.x + p.vx * dt;
            p.y = p.y + p.vy * dt;
            if p.x < 0.0 || p.x > w { p.vx = 0.0 - p.vx; }
            if p.y < 0.0 { p.y = 0.0 - p.y; p.vy = (0.0 - p.vy) * 0.9; }
        }
    }
    let mut sum = 0.0f64;
    for p in &ps { sum = sum + p.x + p.y; }
    println!("{}", (sum * 1000.0).round() as i64);
}

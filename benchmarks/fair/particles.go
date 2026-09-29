package main

import (
	"fmt"
	"math"
	"os"
	"strconv"
)

type P struct{ x, y, vx, vy float64 }

func main() {
	n := 200000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	ps := make([]P, n)
	for i := 0; i < n; i++ {
		ps[i] = P{float64(i%1000) * 0.5, float64(i%777) * 0.25, float64((i%13)-6) * 0.1, float64((i%7)-3) * 0.2}
	}
	dt, g, w := 0.01, 9.81, 500.0
	for s := 0; s < 50; s++ {
		for i := range ps {
			p := &ps[i]
			p.vy = p.vy - g*dt
			p.x = p.x + p.vx*dt
			p.y = p.y + p.vy*dt
			if p.x < 0.0 || p.x > w {
				p.vx = 0.0 - p.vx
			}
			if p.y < 0.0 {
				p.y = 0.0 - p.y
				p.vy = (0.0 - p.vy) * 0.9
			}
		}
	}
	sum := 0.0
	for i := range ps {
		sum = sum + ps[i].x + ps[i].y
	}
	fmt.Println(int64(math.Round(sum * 1000.0)))
}

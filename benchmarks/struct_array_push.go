package main

import "fmt"

type V3 struct {
	x, y, z int64
}

func main() {
	n := 1_000_000
	points := make([]V3, 0, n)
	for i := 0; i < n; i++ {
		points = append(points, V3{int64(i), int64(i) * 2, int64(i) * 3})
	}
	var total int64
	for _, p := range points {
		total += p.x + p.y + p.z
	}
	fmt.Println(total)
}

package main

import (
	"fmt"
	"os"
	"strconv"
)

func main() {
	n := 256
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	a := make([]float64, n*n)
	b := make([]float64, n*n)
	c := make([]float64, n*n)
	for i := 0; i < n; i++ {
		for j := 0; j < n; j++ {
			a[i*n+j] = float64((i + j) % 10)
			b[i*n+j] = float64((i * j) % 7)
		}
	}
	for i := 0; i < n; i++ {
		for k := 0; k < n; k++ {
			av := a[i*n+k]
			for j := 0; j < n; j++ {
				c[i*n+j] += av * b[k*n+j]
			}
		}
	}
	t := 0.0
	for i := 0; i < n*n; i++ {
		t += c[i]
	}
	fmt.Println(int64(t))
}

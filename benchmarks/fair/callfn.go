package main

import (
	"fmt"
	"os"
	"strconv"
)

func f(x int64) int64 { return (x*31 + 7) % 1000003 }
func g(x int64) int64 { return (x*17 + 3) % 1000003 }

func main() {
	n := 20000000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	tab := []func(int64) int64{f, g}
	var acc int64 = 1
	for i := 0; i < n; i++ {
		acc = tab[acc&1](acc)
	}
	fmt.Println(acc)
}

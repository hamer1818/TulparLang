package main

import (
	"fmt"
	"os"
	"strconv"
)

func main() {
	n := 1000000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	m := make(map[string]int)
	for i := 0; i < n; i++ {
		m["k"+strconv.Itoa(i)] = i
	}
	s := 0
	for i := 0; i < n; i++ {
		s += m["k"+strconv.Itoa((i*7)%n)]
	}
	fmt.Println(len(m), s)
}

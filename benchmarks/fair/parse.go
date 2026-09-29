package main

import (
	"fmt"
	"os"
	"strconv"
	"strings"
)

func main() {
	n := 1000000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	var b strings.Builder
	for i := 0; i < n; i++ {
		if i > 0 {
			b.WriteByte(',')
		}
		b.WriteString(strconv.Itoa((i * 7919) % 100000))
	}
	parts := strings.Split(b.String(), ",")
	sum := 0
	for _, p := range parts {
		v, _ := strconv.Atoi(p)
		sum += v
	}
	fmt.Println(len(parts), sum)
}

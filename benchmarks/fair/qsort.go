package main

import (
	"fmt"
	"os"
	"strconv"
)

func qs(a []int32, lo, hi int) {
	i, j := lo, hi
	p := a[(lo+hi)/2]
	for i <= j {
		for a[i] < p {
			i++
		}
		for a[j] > p {
			j--
		}
		if i <= j {
			a[i], a[j] = a[j], a[i]
			i++
			j--
		}
	}
	if lo < j {
		qs(a, lo, j)
	}
	if i < hi {
		qs(a, i, hi)
	}
}

func main() {
	n := 1000000
	if v, err := strconv.Atoi(os.Getenv("BENCH_N")); err == nil && v > 0 {
		n = v
	}
	a := make([]int32, n)
	var seed int64 = 42
	for i := 0; i < n; i++ {
		seed = (seed * 48271) % 2147483647
		a[i] = int32(seed % 1000000)
	}
	qs(a, 0, n-1)
	bad := 0
	for i := 1; i < n; i++ {
		if a[i-1] > a[i] {
			bad++
		}
	}
	var cs int64 = 0
	for i := 0; i < n; i++ {
		cs = (cs + int64(a[i])*int64(i%1000)) % 1000000007
	}
	fmt.Println(a[0], a[n/2], a[n-1], cs, bad)
}

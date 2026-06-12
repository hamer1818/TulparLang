package main

import "fmt"

type V3 struct {
	x, y, z int64
}

func main() {
	var acc V3
	var n int64 = 10_000_000
	for i := int64(0); i < n; i++ {
		acc.x += i
		acc.y += i * 2
		acc.z += i * 3
	}
	fmt.Println(acc.x + acc.y + acc.z)
}

package main

import "fmt"

func main() {
	total := int64(0)
	for i := int64(0); i < 10_000_000; i++ {
		total += i
	}
	fmt.Println(total)
}

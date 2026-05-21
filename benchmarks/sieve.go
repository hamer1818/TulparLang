package main

import "fmt"

func sieve(n int) int {
	flags := make([]bool, n+1)
	count := 0
	for i := 2; i <= n; i++ {
		if !flags[i] {
			count++
			for k := i * i; k <= n; k += i {
				flags[k] = true
			}
		}
	}
	return count
}

func main() {
	fmt.Println(sieve(100000))
}

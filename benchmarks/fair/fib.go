package main
import ("fmt";"os";"strconv")
func fib(n int64) int64 { if n<=1 { return n }; return fib(n-1)+fib(n-2) }
func main(){
    n:=int64(32)
    if v,err:=strconv.ParseInt(os.Getenv("BENCH_N"),10,64); err==nil&&v>0 { n=v }
    fmt.Println(fib(n))
}

package main
import ("fmt";"os";"strconv")
func main(){
    n:=5000000
    if v,err:=strconv.Atoi(os.Getenv("BENCH_N")); err==nil&&v>0 { n=v }
    f:=make([]int32,n+1); c:=int64(0)
    for i:=2;i<=n;i++{ if f[i]==0 { c++; for k:=i*i;k<=n;k+=i { f[k]=1 } } }
    fmt.Println(c)
}

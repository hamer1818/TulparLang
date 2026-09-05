package main
import ("fmt";"os";"strconv")
func main(){
    n:=5000000
    if v,err:=strconv.Atoi(os.Getenv("BENCH_N")); err==nil&&v>0 { n=v }
    a:=make([]int64,n); t:=int64(0)
    for i:=0;i<len(a);i++ { a[i]=int64(i) }
    for i:=0;i<len(a);i++ { t+=a[i] }
    fmt.Println(t)
}

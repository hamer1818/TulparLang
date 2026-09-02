package main
import ("fmt";"os";"strconv")
func main(){
    n:=int64(50000000)
    if v,err:=strconv.ParseInt(os.Getenv("BENCH_N"),10,64); err==nil&&v>0 { n=v }
    var t int64=1
    for i:=int64(0);i<n;i++ { t=(t*31+i)%1000000007 }
    fmt.Println(t)
}

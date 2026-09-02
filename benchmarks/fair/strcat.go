package main
import ("fmt";"os";"strconv";"strings")
func main(){
    n:=int64(2000000)
    if v,err:=strconv.ParseInt(os.Getenv("BENCH_N"),10,64); err==nil&&v>0 { n=v }
    var b strings.Builder
    for i:=int64(0);i<n;i++{ b.WriteString(strconv.FormatInt(i%1000,10)); b.WriteByte(',') }
    s:=b.String()
    cnt:=strings.Count(s,",")
    fmt.Println(len(s),cnt)
}

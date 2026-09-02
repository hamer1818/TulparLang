public class fib{
  static long f(long n){ return n<=1?n:f(n-1)+f(n-2); }
  public static void main(String[] a){
    long n=32; String e=System.getenv("BENCH_N");
    if(e!=null){ try{ long v=Long.parseLong(e); if(v>0)n=v; }catch(Exception x){} }
    System.out.println(f(n));
  }
}
